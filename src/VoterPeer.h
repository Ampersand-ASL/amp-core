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

    void init(Clock* clock, Log* log);

    void reset();

    unsigned getBadPackets() const { return _badPackets; }

    /**
     * Make the connection to support transmission to the 
     * remote Voter device.
     */
    void setSink(sinkCb cb) { _sendCb = cb; };

    /**
     * @param p The the local side's challenge string. It should 
     * be a random string (<=9 chars + null) that stays constant
     * through the life of the session.
     */
    void setLocalChallenge(const char* p) { _localChallenge = p; }

    /**
     * @param p The the local side's password. For a server-side 
     * peer this is the server's password.
     */
    void setLocalPassword(const char* p) { _localPassword = p; }

    /**
     * @param p The the remote side's challenge string. Probably
     * only needed for unit testing since this will normally
     * be exchanged during the handshake
     */
    void setRemoteChallenge(const char* p) { _remoteChallenge = p; }

    /**
     * @param p The remote sides's password. For a server-side 
     * peer this is the client's password.
     */
    void setRemotePassword(const char* p) { _remotePassword = p; }

    /**
     * Used when the address of the peer is known in advance, like
     * for a client that knows the location of its server.
     */
    void setPeerAddr(const sockaddr_storage& addr);

    bool isPeerTrusted() const { return _peerTrusted; }

    void setPeerTrusted(bool v) { _peerTrusted = true; }

    /**
     * @returns true if the packets is signed by someone that knows
     * this peer's password. This is used for the server case to 
     * determine which peer a message belongs to.
     */
    bool belongsTo(const uint8_t* packet, unsigned packetLen) const;

    // ----- For the Voter-facing side of the system ------------------

    /**
     * Called for all inbound packets from the voter unit.
     */
    void consumePacket(const sockaddr& peerAddr, const uint8_t* packet, unsigned packetLen);

    // ----- For the conference-facing side of the system ---------------

    // This set of methods are used every audio tick to 
    // obtain the audio contribution for this client.

    /**
     * @returns Zero if this client has no audio to contribute in
     * the specified time interval.
     */
    uint8_t getRSSI(uint64_t ms);

    /**
     * Extracts the audio frame from this client for the specified
     * time interval.
     */
    void getAudioFrame(uint64_t ms, uint8_t* frame, unsigned frameLen);

    /**
     * Called by the conference to transmit a frame of audio to the
     * Voter device.
     */
    void sendAudio(uint8_t rssi, const uint8_t* frame, unsigned frameLen);

    static bool isValidPacket(const uint8_t* packet, unsigned packetLen);

    static bool isInitialChallenge(const uint8_t* packet, unsigned packetLen);

    static int makeInitialChallengeResponse(const uint8_t* inPacket, 
        const char* localChallenge, const char* localPassword, uint8_t* resp);

    /**
     * @returns A random challenge used for the session.
     */
    static std::string makeChallenge();

    // ----- From Runnable2 --------------------------------------------------

    virtual void audioRateTick(uint32_t tickTimeMs);
    virtual void oneSecTick();
    virtual void tenSecTick();

private:

    struct AudioFrame {
        uint64_t arrivalUs;
        uint32_t packetS;
        uint32_t packetNs;
        uint8_t content[160];
        uint8_t rssi;
    };

    friend class AudioFrame;

    Clock* _clock = 0;
    Log* _log = 0;

    static const unsigned FRAME_COUNT = 4;
    AudioFrame _frames[FRAME_COUNT];
    // Pointers used to manage circular buffer
    unsigned _frameWrPtr = 0;
    unsigned _frameRdPtr = 0;

    void _consumePacketTrusted(const uint8_t* packet, unsigned packetLen);
    void _populateAuth(uint8_t* resp) const;
    void _flushExpiredFrames();

    sockaddr_storage _peerAddr;
    unsigned _badPackets = 0;
    sinkCb _sendCb;
    bool _peerTrusted = false;
    std::string _localPassword;
    std::string _remotePassword;
    std::string _localChallenge;
    std::string _remoteChallenge;

    uint32_t _audioSeq = 0;

    bool _inSpurt = false;
    // This is the actual time the spurt started
    uint64_t _spurtStartUs = 0;

    uint32_t _playCursorS = 0;
    uint32_t _playCursorNs = 0;
    // The controls how far behind the playout should lag the initial
    // network arrival in order to provide enough time for the 
    // inbound packets to "fill in" before being used.
    uint32_t _initialMargin = 2;
};

}
    }
