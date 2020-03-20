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

#pragma once

#include "nakama-cpp/realtime/NRtClientInterface.h"
#include "github.com/heroiclabs/nakama-common/rtapi/realtime.pb.h"
#include "realtime/NRtClientProtocolInterface.h"
#include <list>
#include <map>

namespace Nakama {

    using RtReqId = int32_t;

    struct RtRequestContext
    {
        RtRequestContext(RtReqId id) : id(id) {}
        RtReqId id;
        std::function<void(::nakama::realtime::Envelope&)> successCallback;
        RtErrorCallback errorCallback;
    };

    /**
     * A real-time client to interact with Nakama server.
     * Don't use it directly, use `createDefaultRtClient` instead.
     */
    class NRtClient : public NRtClientInterface
    {
    public:
        NRtClient(NRtTransportPtr transport, const std::string& host, int32_t port, bool ssl);
        ~NRtClient();

        void tick() override;

        void setListener(NRtClientListenerInterface* listener) override;

        void setUserData(void* userData) override { _userData = userData; }
        void* getUserData() const override { return _userData; }

        void connect(NSessionPtr session, bool createStatus, NRtClientProtocol protocol) override;

        bool isConnected() const override;

        void disconnect() override;

        // "Buffered Sends" API
        bool enableBufferedSends(const RtClientBufferedSendsParameters& params) override;
        void disableBufferedSends() override;
        bool isEnabledBufferedSends() const override { return _bufferedSends != nullptr; }
        bool sendBufferedMessages() override;
        void clearBufferedMessages() override;

        NRtTransportPtr getTransport() const override { return _transport; }

        void joinChat(
            const std::string& target,
            NChannelType type,
            const opt::optional<bool>& persistence = opt::nullopt,
            const opt::optional<bool>& hidden = opt::nullopt,
            std::function<void (NChannelPtr)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void leaveChat(
            const std::string& channelId,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void writeChatMessage(
            const std::string& channelId,
            const std::string& content,
            std::function<void(const NChannelMessageAck&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void updateChatMessage(
            const std::string& channelId,
            const std::string& messageId,
            const std::string& content,
            std::function<void(const NChannelMessageAck&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void removeChatMessage(
            const std::string& channelId,
            const std::string& messageId,
            std::function<void(const NChannelMessageAck&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void createMatch(
            std::function<void(const NMatch&)> successCallback,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void joinMatch(
            const std::string& matchId,
            const NStringMap& metadata,
            std::function<void(const NMatch&)> successCallback,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void joinMatchByToken(
            const std::string& token,
            std::function<void(const NMatch&)> successCallback,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void leaveMatch(
            const std::string& matchId,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void addMatchmaker(
            const opt::optional<int32_t>& minCount = opt::nullopt,
            const opt::optional<int32_t>& maxCount = opt::nullopt,
            const opt::optional<std::string>& query = opt::nullopt,
            const NStringMap& stringProperties = {},
            const NStringDoubleMap& numericProperties = {},
            std::function<void(const NMatchmakerTicket&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void removeMatchmaker(
            const std::string& ticket,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void sendMatchData(
            const std::string& matchId,
            int64_t opCode,
            const NBytes& data,
            const std::vector<NUserPresence>& presences = {}
        ) override;

        void followUsers(
            const std::vector<std::string>& userIds,
            std::function<void(const NStatus&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void unfollowUsers(
            const std::vector<std::string>& userIds,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void updateStatus(
            const std::string& status,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        void rpc(
            const std::string& id,
            const opt::optional<std::string>& payload = opt::nullopt,
            std::function<void(const NRpc&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) override;

        protected:
            void onTransportDisconnected(const NRtClientDisconnectInfo& info);
            void onTransportError(const std::string& description);
            void onTransportMessage(const NBytes& data);

            void reqInternalError(RtReqId rid, const NRtError& error);

            RtRequestContext* createReqContext(::nakama::realtime::Envelope& msg);
            void send(RtReqId rid, const ::nakama::realtime::Envelope& msg);
            bool send(RtReqId rid, const NBytes& data, bool triggerErrorOnFail);

        protected:
            struct BufferedMessage
            {
                RtReqId rid = 0;
                NBytes data;
            };

            std::string _host;
            int32_t _port = 0;
            bool _ssl = false;
            NRtClientListenerInterface* _listener = nullptr;
            NRtTransportPtr _transport;
            NRtClientProtocolPtr _protocol;
            std::map<RtReqId, std::unique_ptr<RtRequestContext>> _reqContexts;
            RtReqId _nextReqId = 0;
            void* _userData = nullptr;
            std::unique_ptr<RtClientBufferedSendsParameters> _bufferedSends;
            std::list<BufferedMessage> _bufferedMessages;
            size_t _bufferedMessagesSize = 0;
            NTimestamp _flushedTimestamp = 0;
    };

}
