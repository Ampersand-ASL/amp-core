/**
 * Copyright (C) 2026, Bruce MacKinnon KC1FSZ
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <functional>

#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

/**
 * An instance of this class represents either the server or client side of
 * a voter link. The protocol is reasonably symmetric so we are going to avoid
 * creating separate classes for each side.
 */
class VoterPeer : public Runnable2 {
public:

    using sinkCb = std::function<void(const sockaddr& addr, 
        const uint8_t* packet, unsigned packetLen)>;

    VoterPeer();
    ~VoterPeer();

    void init(Log* log);

    void reset();

    /**
     * Make the connection to support transmission to the 
     * remote Voter device.
     */
    void setSink(sinkCb cb) { _sendCb = cb; };

    /**
     * @param p The the local side's password. For a server-side 
     * peer this is the server's password.
     */
    void setLocalPassword(const char* p) { _localPassword = p; }

    /**
     * @param p The remote sides's password. For a server-side 
     * peer this is the client's password.
     */
    void setRemotePassword(const char* p) { _remotePassword = p; }

    // ----- For the Voter-facing side of the system ------------------

    /**
     * Called for all inbound packets from the voter unit.
     */
    void consumePacket(const uint8_t* packet, unsigned packetLen);

    // ----- For the conference-facing side of the system ---------------

    // This set of methods are used every audio tick to 
    // obtain the audio contribution for this client.

    /**
     * @returns Zero if this client has no audio to contribute in
     * the specified time interval.
     */
    uint8_t getRSSI(uint64_t ms) const;

    /**
     * Extracts the audio frame from this client for the specified
     * time interval.
     */
    void getAudioFrame(uint64_t ms, uint8_t* frame, unsigned frameLen);

    /**
     * Called by the conference to transmit a frame of audio to the
     * Voter device.
     */
    void sendAudio(uint64_t ms, const uint8_t* frame, unsigned frameLen);

    // ----- From Runnable2 --------------------------------------------------

    virtual void oneSecTick();

private:

    Log* _log = 0;

    void _consumePacketTrusted(const uint8_t* packet, unsigned packetLen);

    /**
     * @returns A random challenge used for the session.
     */
    static std::string _makeChallenge();

    sockaddr_storage _peerAddr;
    unsigned _badPackets = 0;
    sinkCb _sendCb;
    bool _peerTrusted = false;
    std::string _localPassword;
    std::string _remotePassword;
    std::string _localChallenge;
    std::string _remoteChallenge;
};

}
    }
