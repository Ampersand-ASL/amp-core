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

/**
 * A generic class for managing the pointers in a circular queue.
 * (I'm tired of writing this over an over again.)
 */
class CircularQueuePointers {
public:
    
    CircularQueuePointers(unsigned capacity)
    :   _capacity(capacity) {        
        reset();
    }

    void reset() {
        _readPtr = 0; _writePtr = 0; _fault = false; _depth = 0;
    }

    bool isEmpty() const {
        return _readPtr == _writePtr;
    }

    bool isFull() const {
        return _next(_writePtr) == _readPtr;
    }

    /**
     * @return The current number of items on the queue
     */
    unsigned getDepth() const {
        return _depth;
    }

    bool isFault() const { return _fault; }

    unsigned writePtr() const { return _writePtr; }

    unsigned writePtrThenPush() { 
        unsigned t = _writePtr; 
        push(); 
        return t;
    }

    unsigned readPtr() const { return _readPtr; }

    unsigned readPtrThenPop() { 
        unsigned t = _readPtr; 
        pop(); 
        return t;
    }

    void push() { 
        if (isFull())
            _fault = true;
        else {
            _writePtr = _next(_writePtr);
            _depth++;
        }
    }

    void pop() {
        if (isEmpty())
            _fault = true;
        else {
            _readPtr = _next(_readPtr);
            _depth--;
        }
    }

private:

    /**
     * Increments the value, wrapping if needed
     */
    unsigned _next(unsigned i) const {
        i++;
        if (i == _capacity)
            i = 0;
        return i;
    }

    const unsigned _capacity;
    unsigned _readPtr = 0;
    unsigned _writePtr = 0;
    bool _fault = false;
    unsigned _depth = 0;
};

    namespace amp {

/**
 * An instance of this class represents either the server or client side of
 * a voter link. The protocol is reasonably symmetric so we are going to avoid
 * creating separate classes for each side.
 *
 * IMPORTANT NOTE: This is a class that should be re-usable on a microcontroller
 * so please avoid any constructs that aren't portable.
 */
class VoterPeer : public Runnable2 {
public:

    using sinkCb = std::function<void(const sockaddr& addr, 
        const uint8_t* packet, unsigned packetLen)>;

    /**
     * @param isClient Controls certain behaviors that are server-only or client-only.
     */
    VoterPeer(bool isClient = false);
    ~VoterPeer();

    void init(Clock* clock, Log* log);

    void reset();

    /**
     * Master Timing Source Mode. If not set, payload 1, 2 and 3 packets are delayed 
     * (approx 6mS) to guarantee that the packets from the device designated as "Master 
     * Timing Source" are received by the host previous to packets from any other device. 
     * If set, no delay is performed, and the packets are sent immediately. This must be 
     * set on only one device in the network. (sent by host only)
     */
    void setMasterTimingSource(bool t) { _masterTimingSource = t; }

    /** 
     * Operate in “general-purpose” mode (non-GPS-based). (sent by client, responded 
     * to by host)
     */
    void setGeneralPurposeMode(bool m) { _generalPurposeMode = m; }

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
    void setLocalChallenge(const char* p);

    /**
     * @param p The the local side's password. For a server-side 
     * peer this is the server's password.
     */
    void setLocalPassword(const char* p);

    /**
     * @param p The the remote side's challenge string. Probably
     * only needed for unit testing since this will normally
     * be exchanged during the handshake
     */
    void setRemoteChallenge(const char* p);

    /**
     * @param p The remote sides's password. For a server-side 
     * peer this is the client's password.
     */
    void setRemotePassword(const char* p);

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
     * Should be called after audioRateTick().
     * @returns True if this peer has audio to contribute in this tick.
     */
    bool isAudioAvailable() const;

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
     * Should be called after the RSSI/audio frame for the current tick 
     * has been used.
     */
    void popAudioFrame();

    /**
     * Called by the conference to transmit a frame of audio to the
     * Voter device.
     */
    void sendAudio(uint8_t rssi, const uint8_t* frame, unsigned frameLen);

    static bool isValidPacket(const uint8_t* packet, unsigned packetLen);

    static bool isInitialChallenge(const uint8_t* packet, unsigned packetLen);

    static int makeInitialChallengeResponse(Clock* clock, const uint8_t* inPacket, 
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

    /** 
     * Each frame of audio arriving from a VOTER client get stored in one 
     * of these frames. The frames are organized into a circular buffer.
     */
    struct AudioFrame {

        /**
        * @param gpMode True for "general-purpose" mode, that changes the semantics
        * of the nanosecond field.
        * @returns True if the frame is expired relative to the current time.
        */
        bool isExpired(bool gpMode, uint32_t currentS, uint32_t currentNs) const {
            if (gpMode)
                return packetNs < currentNs;
            else 
                return packetS < currentS || 
                    (packetS == currentS && packetNs < currentNs);
        }

        /**
        * @param gpMode True for "general-purpose" mode, that changes the semantics
        * of the nanosecond field.
        */
        bool isCurrent(bool gpMode, uint32_t currentS, uint32_t currentNs) const {
            if (gpMode)
                return packetNs == currentNs;
            else 
                return packetS == currentS && packetNs == currentNs;
        }

        uint64_t arrivalUs;
        uint32_t packetS;
        uint32_t packetNs;
        uint8_t content[160];
        uint8_t rssi;
    };

    const bool _isClient;
    Clock* _clock = 0;
    Log* _log = 0;

    bool _masterTimingSource = false;
    bool _generalPurposeMode = false;

    static const unsigned FRAME_COUNT = 8;
    AudioFrame _frames[FRAME_COUNT];
    // Pointers used to manage circular buffer.
    CircularQueuePointers _framePtrs;

    void _consumePacketTrusted(const uint8_t* packet, unsigned packetLen);
    void _populateHeader(uint16_t type, uint8_t* resp) const;

    sockaddr_storage _peerAddr;
    unsigned _badPackets = 0;
    sinkCb _sendCb;
    bool _peerTrusted = false;

    // Last time we heard from the peer
    uint64_t _lastRxMs = 0;
    // Used to track last audio arrival, used for detecting the 
    // end of a spurt
    uint64_t _lastAudioMs = 0;
    bool _inSpurt = false;
    // This is the actual time the spurt started
    uint64_t _spurtStartMs = 0;

    char _localPassword[10];
    char _remotePassword[10];
    char _localChallenge[16];
    char _remoteChallenge[16];

    uint32_t _audioSeq = 0;

    uint32_t _playCursorS = 0;
    uint32_t _playCursorNs = 0;
    bool _audioAvailableThisTick = false;

    // The controls how far behind the playout should lag the initial
    // network arrival in order to provide enough time for the 
    // inbound packets to "fill in" before being used.
    // In general purpose mode this is a packet sequence
    uint32_t _initialMarginGP = 3;
    // In GPS mode is this is in nanoseconds
    uint32_t _initialMarginGPS = 60000000;

    unsigned _oneTickCounter = 0;
};

}
    }
