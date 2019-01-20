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

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include "nakama-cpp/NSessionInterface.h"
#include "nakama-cpp/data/NMatch.h"
#include "nakama-cpp/data/NRpc.h"
#include "nakama-cpp/realtime/NRtClientListenerInterface.h"
#include "nakama-cpp/realtime/NRtTransportInterface.h"
#include "nakama-cpp/realtime/rtdata/NChannel.h"
#include "nakama-cpp/realtime/rtdata/NChannelMessageAck.h"
#include "nakama-cpp/realtime/rtdata/NMatchmakerTicket.h"
#include "nakama-cpp/realtime/rtdata/NUserPresence.h"
#include "nakama-cpp/realtime/rtdata/NStatus.h"

namespace Nakama {

    using RtErrorCallback = std::function<void(const NRtError&)>;

    /**
     * A real-time client interface to interact with Nakama server.
     */
    class NRtClientInterface
    {
    public:
        virtual ~NRtClientInterface() {}

        /**
         * Close the connection with the server.
         */
        virtual void disconnect() = 0;

        /**
        * Set events listener
        *
        * @param listener The listener of client events.
        */
        virtual void setListener(NRtClientListenerInterface* listener) = 0;

        /**
        * Connect to the server.
        *
        * @param session The session of the user.
        * @param createStatus True if the socket should show the user as online to others.
        */
        virtual void connect(NSessionPtr session, bool createStatus) = 0;

        /**
        * Join a chat channel on the server.
        *
        * @param target The target channel to join.
        * @param type The type of channel to join.
        * @param persistence True if chat messages should be stored.
        * @param hidden True if the user should be hidden on the channel.
        */
        virtual void joinChat(
            const std::string& target,
            NChannelType type,
            const opt::optional<bool>& persistence = opt::nullopt,
            const opt::optional<bool>& hidden = opt::nullopt,
            std::function<void (NChannelPtr)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Leave a chat channel on the server.
        *
        * @param channelId The channel to leave.
        */
        virtual void leaveChat(
            const std::string& channelId,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Send a chat message to a channel on the server.
        *
        * @param channelId The channel to send on.
        * @param content The content of the chat message.
        */
        virtual void writeChatMessage(
            const std::string& channelId,
            const std::string& content,
            std::function<void(const NChannelMessageAck&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Update a chat message to a channel on the server.
        *
        * @param channelId The ID of the chat channel with the message.
        * @param messageId The ID of the message to update.
        * @param content The content update for the message.
        */
        virtual void updateChatMessage(
            const std::string& channelId,
            const std::string& messageId,
            const std::string& content,
            std::function<void(const NChannelMessageAck&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Remove a chat message from a channel on the server.
        *
        * @param channelId The chat channel with the message.
        * @param messageId The ID of a chat message to remove.
        */
        virtual void removeChatMessage(
            const std::string& channelId,
            const std::string& messageId,
            std::function<void(const NChannelMessageAck&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Create a multiplayer match on the server.
        */
        virtual void createMatch(
            std::function<void(const NMatch&)> successCallback,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Join a multiplayer match by ID.
        *
        * @param matchId A match ID.
        */
        virtual void joinMatch(
            const std::string& matchId,
            std::function<void(const NMatch&)> successCallback,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Join a multiplayer match with a matchmaker.
        *
        * @param token A matchmaker ticket result object.
        */
        virtual void joinMatchByToken(
            const std::string& token,
            std::function<void(const NMatch&)> successCallback,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Leave a match on the server.
        *
        * @param matchId The match to leave.
        */
        virtual void leaveMatch(
            const std::string& matchId,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Join the matchmaker pool and search for opponents on the server.
        *
        * @param minCount The minimum number of players to compete against.
        * @param maxCount The maximum number of players to compete against.
        * @param query A matchmaker query to search for opponents.
        * @param stringProperties A set of k/v properties to provide in searches.
        * @param numericProperties A set of k/v numeric properties to provide in searches.
        */
        virtual void addMatchmaker(
            const opt::optional<int32_t>& minCount = opt::nullopt,
            const opt::optional<int32_t>& maxCount = opt::nullopt,
            const opt::optional<std::string>& query = opt::nullopt,
            const NStringMap& stringProperties = {},
            const NStringDoubleMap& numericProperties = {},
            std::function<void(const NMatchmakerTicket&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Leave the matchmaker pool by ticket.
        *
        * @param ticket The ticket returned by the matchmaker on join. See <c>NMatchmakerTicket.ticket</c>.
        */
        virtual void removeMatchmaker(
            const std::string& ticket,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Send a state change to a match on the server.
        *
        * When no presences are supplied the new match state will be sent to all presences.
        *
        * @param matchId The Id of the match.
        * @param opCode An operation code for the match state.
        * @param data The new state to send to the match.
        * @param presences The presences in the match to send the state.
        */
        virtual void sendMatchData(
            const std::string& matchId,
            int64_t opCode,
            const NBytes& data,
            const std::vector<NUserPresence>& presences = {}
        ) = 0;

        /**
        * Follow one or more users for status updates.
        *
        * @param userIds The user Ids to follow.
        */
        virtual void followUsers(
            const std::vector<std::string>& userIds,
            std::function<void(const NStatus&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Unfollow status updates for one or more users.
        *
        * @param userIds The ids of users to unfollow.
        */
        virtual void unfollowUsers(
            const std::vector<std::string>& userIds,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Update the user's status online.
        *
        * @param status The new status of the user.
        */
        virtual void updateStatus(
            const std::string& status,
            std::function<void()> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;

        /**
        * Send an RPC message to the server.
        *
        * @param id The ID of the function to execute.
        * @param payload The string content to send to the server.
        */
        virtual void rpc(
            const std::string& id,
            const opt::optional<std::string>& payload = opt::nullopt,
            std::function<void(const NRpc&)> successCallback = nullptr,
            RtErrorCallback errorCallback = nullptr
        ) = 0;
    };

    using NRtClientPtr = std::shared_ptr<NRtClientInterface>;
}
