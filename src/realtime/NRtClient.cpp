/*
 * Copyright 2019 The Nakama Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "NRtClient.h"
#include "DataHelper.h"
#include "nakama-cpp/NUtils.h"
#include "nakama-cpp/StrUtil.h"
#include "nakama-cpp/log/NLogger.h"
#include "realtime/NRtClientProtocol_Protobuf.h"
#include "realtime/NRtClientProtocol_Json.h"

#undef NMODULE_NAME
#define NMODULE_NAME "NRtClient"

namespace Nakama {

const RtReqId INVALID_REQ_ID = -1;

NRtClient::NRtClient(NRtTransportPtr transport, const std::string& host, int32_t port, bool ssl)
    : _host(host)
    , _port(port)
    , _ssl(ssl)
    , _transport(transport)
{
    NLOG_INFO("Created");

    if (_port == DEFAULT_PORT)
    {
        _port = _ssl ? 443 : 7350;

        NLOG(NLogLevel::Info, "using default port %d", _port);
    }

    _transport->setConnectCallback([this]()
    {
        NLOG_DEBUG("connected");

        if (_listener)
        {
            _listener->onConnect();
        }
    });

    _transport->setErrorCallback(std::bind(&NRtClient::onTransportError, this, std::placeholders::_1));
    _transport->setDisconnectCallback(std::bind(&NRtClient::onTransportDisconnected, this, std::placeholders::_1));
    _transport->setMessageCallback(std::bind(&NRtClient::onTransportMessage, this, std::placeholders::_1));
}

NRtClient::~NRtClient()
{
    sendBufferedMessages();

    if (_bufferedMessages.size() > 0)
    {
        NLOG(NLogLevel::Warn, "Not sent %u realtime buffered messages detected.", _bufferedMessages.size());
    }

    if (_reqContexts.size() > 0)
    {
        NLOG(NLogLevel::Warn, "Not handled %u realtime requests detected.", _reqContexts.size());
    }
}

void NRtClient::tick()
{
    if (_bufferedMessagesSize > 0 &&
        isEnabledBufferedSends() &&
        getUnixTimestampMs() >= (_flushedTimestamp + _bufferedSends->maxRetentionPeriodMs))
    {
        sendBufferedMessages();
    }

    _transport->tick();
}

void NRtClient::setListener(NRtClientListenerInterface * listener)
{
    _listener = listener;
}

void NRtClient::connect(NSessionPtr session, bool createStatus, NRtClientProtocol protocol)
{
    std::string url;
    NRtTransportType transportType;

    if (_ssl)
        url.append("wss://");
    else
        url.append("ws://");

    url.append(_host).append(":").append(std::to_string(_port)).append("/ws");
    url.append("?token=").append(urlEncode(session->getAuthToken()));
    url.append("&status=").append(createStatus ? "true" : "false");

    // by default server uses Json protocol
    if (protocol == NRtClientProtocol::Protobuf)
    {
        url.append("&format=protobuf");
        _protocol.reset(new NRtClientProtocol_Protobuf());
        transportType = NRtTransportType::Binary;
    }
    else
    {
        _protocol.reset(new NRtClientProtocol_Json());
        transportType = NRtTransportType::Text;
    }

    NLOG_INFO("...");
    _transport->connect(url, transportType);
}

bool NRtClient::isConnected() const
{
    return _transport->isConnected();
}

void NRtClient::disconnect()
{
    sendBufferedMessages();

    _transport->disconnect();
}

bool NRtClient::enableBufferedSends(const RtClientBufferedSendsParameters& params)
{
    if (params.bufferSize == 0)
    {
        NLOG_ERROR("Bad argument: bufferSize = 0");
        return false;
    }

    if (params.maxRetentionPeriodMs == 0)
    {
        NLOG_ERROR("Bad argument: maxRetentionPeriodMs = 0");
        return false;
    }

    NLOG_INFO("");

    if (_bufferedSends)
    {
        *_bufferedSends = params;
    }
    else
    {
        _bufferedSends.reset(new RtClientBufferedSendsParameters(params));
        _flushedTimestamp = getUnixTimestampMs();
    }
    return true;
}

void NRtClient::disableBufferedSends()
{
    NLOG_INFO("");
    sendBufferedMessages();
    _bufferedSends.reset();
}

bool NRtClient::sendBufferedMessages()
{
    if (isConnected() && !_bufferedMessages.empty())
    {
        while (!_bufferedMessages.empty())
        {
            auto& msg = _bufferedMessages.front();
            if (!send(msg.rid, msg.data, false))
            {
                return false;
            }

            _bufferedMessagesSize -= msg.data.size();
            _bufferedMessages.pop_front();
        }

        assert(_bufferedMessagesSize == 0);
        _bufferedMessagesSize = 0;
        _flushedTimestamp = getUnixTimestampMs();
        return true;
    }
    return false;
}

void NRtClient::clearBufferedMessages()
{
    auto requests = std::move(_bufferedMessages);

    while (!requests.empty())
    {
        reqInternalError(requests.front().rid, NRtError(RtErrorCode::DISCARDED_BY_USER, ""));
        requests.pop_front();
    }
}

void NRtClient::onTransportDisconnected(const NRtClientDisconnectInfo& info)
{
    NLOG(NLogLevel::Debug, "code: %u, %s", info.code, info.reason.c_str());

    if (_listener)
    {
        _listener->onDisconnect(info);
    }
}

void NRtClient::onTransportError(const std::string& description)
{
    NRtError error;

    error.message = description;
    error.code    = _transport->isConnected() ? RtErrorCode::TRANSPORT_ERROR : RtErrorCode::CONNECT_ERROR;

    NLOG_ERROR(toString(error));

    if (_listener)
    {
        _listener->onError(error);
    }
}

void NRtClient::onTransportMessage(const NBytes & data)
{
    ::nakama::realtime::Envelope msg;

    if (!_protocol->parse(data, msg))
    {
        onTransportError("parse message failed");
        return;
    }

    NRtError error;

    if (msg.has_error())
    {
        assign(error, msg.error());

        NLOG_ERROR(toString(error));
    }

    if (msg.cid().empty())
    {
        if (_listener)
        {
            if (msg.has_error())
            {
                _listener->onError(error);
            }
            else if (msg.has_channel_message())
            {
                NChannelMessage channelMessage;
                assign(channelMessage, msg.channel_message());
                _listener->onChannelMessage(channelMessage);
            }
            else if (msg.has_channel_presence_event())
            {
                NChannelPresenceEvent channelPresenceEvent;
                assign(channelPresenceEvent, msg.channel_presence_event());
                _listener->onChannelPresence(channelPresenceEvent);
            }
            else if (msg.has_match_data())
            {
                NMatchData matchData;
                assign(matchData, msg.match_data());
                _listener->onMatchData(matchData);
            }
            else if (msg.has_match_presence_event())
            {
                NMatchPresenceEvent matchPresenceEvent;
                assign(matchPresenceEvent, msg.match_presence_event());
                _listener->onMatchPresence(matchPresenceEvent);
            }
            else if (msg.has_matchmaker_matched())
            {
                NMatchmakerMatchedPtr matchmakerMatched(new NMatchmakerMatched());
                assign(*matchmakerMatched, msg.matchmaker_matched());
                _listener->onMatchmakerMatched(matchmakerMatched);
            }
            else if (msg.has_notifications())
            {
                NNotificationList list;
                assign(list, msg.notifications());
                _listener->onNotifications(list);
            }
            else if (msg.has_status_presence_event())
            {
                NStatusPresenceEvent event;
                assign(event, msg.status_presence_event());
                _listener->onStatusPresence(event);
            }
            else if (msg.has_stream_data())
            {
                NStreamData data;
                assign(data, msg.stream_data());
                _listener->onStreamData(data);
            }
            else if (msg.has_stream_presence_event())
            {
                NStreamPresenceEvent event;
                assign(event, msg.stream_presence_event());
                _listener->onStreamPresence(event);
            }
            else
            {
                onTransportError("Unknown message received");
            }
        }
        else
        {
            NLOG_ERROR("No listener. Received message has been ignored.");
        }
    }
    else
    {
        RtReqId rid = std::stoi(msg.cid());
        auto it = _reqContexts.find(rid);

        if (it != _reqContexts.end())
        {
            if (msg.has_error())
            {
                if (it->second->errorCallback)
                {
                    it->second->errorCallback(error);
                }
                else if (_listener)
                {
                    _listener->onError(error);
                }
                else
                {
                    NLOG_WARN("^ error not handled");
                }
            }
            else if (it->second->successCallback)
            {
                it->second->successCallback(msg);
            }

            _reqContexts.erase(it);
        }
        else
        {
            onTransportError("request context not found. cid: " + msg.cid());
        }
    }
}

void NRtClient::joinChat(
    const std::string & target,
    NChannelType type,
    const opt::optional<bool>& persistence,
    const opt::optional<bool>& hidden,
    std::function<void(NChannelPtr)> successCallback,
    RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;
    auto* channelJoin = msg.mutable_channel_join();

    channelJoin->set_target(target);
    channelJoin->set_type(static_cast<int32_t>(type));

    if (persistence) channelJoin->mutable_persistence()->set_value(*persistence);
    if (hidden) channelJoin->mutable_hidden()->set_value(*hidden);

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NChannelPtr channel(new NChannel());
            assign(*channel, msg.channel());
            successCallback(channel);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::leaveChat(const std::string & channelId, std::function<void()> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;

    msg.mutable_channel_leave()->set_channel_id(channelId);

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            successCallback();
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::writeChatMessage(const std::string & channelId, const std::string & content, std::function<void(const NChannelMessageAck&)> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;

    msg.mutable_channel_message_send()->set_channel_id(channelId);
    msg.mutable_channel_message_send()->set_content(content);

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NChannelMessageAck ack;
            assign(ack, msg.channel_message_ack());
            successCallback(ack);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::updateChatMessage(const std::string & channelId, const std::string & messageId, const std::string & content, std::function<void(const NChannelMessageAck&)> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;

    {
        auto *channel_message = msg.mutable_channel_message_update();

        channel_message->set_channel_id(channelId);
        channel_message->set_message_id(messageId);
        channel_message->set_content(content);
    }

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NChannelMessageAck ack;
            assign(ack, msg.channel_message_ack());
            successCallback(ack);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::removeChatMessage(const std::string & channelId, const std::string & messageId, std::function<void(const NChannelMessageAck&)> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;

    msg.mutable_channel_message_remove()->set_channel_id(channelId);
    msg.mutable_channel_message_remove()->set_message_id(messageId);

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NChannelMessageAck ack;
            assign(ack, msg.channel_message_ack());
            successCallback(ack);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::createMatch(std::function<void(const NMatch&)> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;

    msg.mutable_match_create();

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NMatch match;
            assign(match, msg.match());
            successCallback(match);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::joinMatch(
    const std::string & matchId,
    const NStringMap& metadata,
    std::function<void(const NMatch&)> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;
    auto match_join = msg.mutable_match_join();

    match_join->set_match_id(matchId);

    for (auto p : metadata)
    {
        match_join->mutable_metadata()->insert({ p.first, p.second });
    }

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NMatch match;
            assign(match, msg.match());
            successCallback(match);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::joinMatchByToken(const std::string & token, std::function<void(const NMatch&)> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;

    msg.mutable_match_join()->set_token(token);

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NMatch match;
            assign(match, msg.match());
            successCallback(match);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::leaveMatch(const std::string & matchId, std::function<void()> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;

    msg.mutable_match_leave()->set_match_id(matchId);

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            successCallback();
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::addMatchmaker(
    const opt::optional<int32_t>& minCount,
    const opt::optional<int32_t>& maxCount,
    const opt::optional<std::string>& query,
    const NStringMap & stringProperties,
    const NStringDoubleMap & numericProperties,
    std::function<void(const NMatchmakerTicket&)> successCallback,
    RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;
    auto* data = msg.mutable_matchmaker_add();

    if (minCount) data->set_min_count(*minCount);
    if (maxCount) data->set_max_count(*maxCount);
    if (query) data->set_query(*query);

    for (auto it : stringProperties)
    {
        (*data->mutable_string_properties())[it.first] = it.second;
    }

    for (auto it : numericProperties)
    {
        (*data->mutable_numeric_properties())[it.first] = it.second;
    }

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NMatchmakerTicket ticket;
            assign(ticket, msg.matchmaker_ticket());
            successCallback(ticket);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::removeMatchmaker(const std::string & ticket, std::function<void()> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;

    msg.mutable_matchmaker_remove()->set_ticket(ticket);

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            successCallback();
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::sendMatchData(const std::string & matchId, int64_t opCode, const NBytes & data, const std::vector<NUserPresence>& presences)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;
    auto* match_data = msg.mutable_match_data_send();

    match_data->set_match_id(matchId);
    match_data->set_op_code(opCode);
    match_data->set_data(data.data(), data.size());

    for (auto& presence : presences)
    {
        if (presence.userId.empty())
        {
            NLOG_ERROR("Please set 'userId' for user presence");
            continue;
        }

        if (presence.sessionId.empty())
        {
            NLOG_ERROR("Please set 'sessionId' for user presence");
            continue;
        }

        auto* presenceData = match_data->mutable_presences()->Add();

        presenceData->set_user_id(presence.userId);
        presenceData->set_session_id(presence.sessionId);

        if (!presence.username.empty())
            presenceData->set_username(presence.username);

        if (!presence.status.empty())
            presenceData->mutable_status()->set_value(presence.status);

        presenceData->set_persistence(presence.persistence);
    }

    // this message is without response
    // so we don't need request context
    send(INVALID_REQ_ID, msg);
}

void NRtClient::followUsers(const std::vector<std::string>& userIds, std::function<void(const NStatus&)> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;
    auto* data = msg.mutable_status_follow();

    for (auto& id : userIds)
    {
        data->add_user_ids(id);
    }

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NStatus status;
            assign(status, msg.status());
            successCallback(status);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::unfollowUsers(const std::vector<std::string>& userIds, std::function<void()> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;
    auto* data = msg.mutable_status_unfollow();

    for (auto& id : userIds)
    {
        data->add_user_ids(id);
    }

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            successCallback();
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::updateStatus(const std::string & status, std::function<void()> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;

    msg.mutable_status_update()->mutable_status()->set_value(status);

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            successCallback();
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

void NRtClient::rpc(const std::string & id, const opt::optional<std::string>& payload, std::function<void(const NRpc&)> successCallback, RtErrorCallback errorCallback)
{
    NLOG_INFO("...");

    ::nakama::realtime::Envelope msg;
    auto* data = msg.mutable_rpc();

    data->set_id(id);

    if (payload)
        data->set_payload(*payload);

    RtRequestContext * ctx = createReqContext(msg);

    if (successCallback)
    {
        ctx->successCallback = [successCallback](::nakama::realtime::Envelope& msg)
        {
            NRpc rpc;
            assign(rpc, msg.rpc());
            successCallback(rpc);
        };
    }
    ctx->errorCallback = errorCallback;

    send(ctx->id, msg);
}

RtRequestContext * NRtClient::createReqContext(::nakama::realtime::Envelope& msg)
{
    if (_reqContexts.empty() && _nextReqId > 9)
    {
        // reset just to be one digit
        // we can reset because there are no pending requests
        _nextReqId = 0;
    }

    RtReqId rid = _nextReqId++;
    RtRequestContext * ctx = new RtRequestContext(rid);

    _reqContexts.emplace(rid, std::unique_ptr<RtRequestContext>(ctx));
    msg.set_cid(std::to_string(rid));

    return ctx;
}

void NRtClient::reqInternalError(RtReqId rid, const NRtError & error)
{
    NLOG_ERROR(toString(error));

    if (rid == INVALID_REQ_ID)
        return;

    auto it = _reqContexts.find(rid);

    if (it != _reqContexts.end())
    {
        if (it->second->errorCallback)
        {
            it->second->errorCallback(error);
        }
        else if (_listener)
        {
            _listener->onError(error);
        }
        else
        {
            NLOG_WARN("error not handled");
        }

        _reqContexts.erase(it);
    }
    else
    {
        NLOG(NLogLevel::Error, "request context not found. id: %d", rid);
    }
}

void NRtClient::send(RtReqId rid, const ::nakama::realtime::Envelope & msg)
{
    if (isConnected())
    {
        NBytes bytes;

        if (_protocol->serialize(msg, bytes))
        {
            if (isEnabledBufferedSends())
            {
                if (_bufferedMessagesSize + bytes.size() > _bufferedSends->bufferSize)
                {
                    sendBufferedMessages();
                }

                _bufferedMessagesSize += bytes.size();
                _bufferedMessages.push_back({ rid, std::move(bytes) });

                if (_bufferedMessagesSize >= _bufferedSends->bufferSize)
                {
                    sendBufferedMessages();
                }
            }
            else
                send(rid, bytes, true);
        }
        else
        {
            reqInternalError(rid, NRtError(RtErrorCode::TRANSPORT_ERROR, "Serialize message failed"));
        }
    }
    else
    {
        reqInternalError(rid, NRtError(RtErrorCode::CONNECT_ERROR, "Not connected"));
    }
}

bool NRtClient::send(RtReqId rid, const NBytes& data, bool triggerErrorOnFail)
{
    if (!_transport->send(data))
    {
        if (triggerErrorOnFail)
            reqInternalError(rid, NRtError(RtErrorCode::TRANSPORT_ERROR, "Send message failed"));
        disconnect();
        return false;
    }

    return true;
}

}
