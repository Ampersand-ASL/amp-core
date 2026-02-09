/**
 * Copyright (C) 2025, Bruce MacKinnon KC1FSZ
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

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif 

#include <functional>

#include "kc1fsz-tools/fixedstring.h"
#include "kc1fsz-tools/fixedqueue.h"

#include "amp/SequencingBufferStd.h"
#include "amp/RetransmissionBufferStd.h"

#include "Line.h"
#include "IAX2Util.h"
#include "IAX2FrameFull.h"
#include "Message.h"
#include "MessageConsumer.h"

namespace kc1fsz {

class Log;
class Clock;

/**
 * And interface used for authorizing numbers in a few contexts
 */
class NumberAuthorizer {
public:

    /**
     * @returns true if the number is authorized
     */
    virtual bool isAuthorized(const char* nodeNumber) const = 0;
};

/**
 * A support interface for LineIAX2. Abstracts the process of resolving
 * a node number's address in a local registry (i.e. before going out
 * to DNS to use the public registry). Presumably this would be used
 * private nodes.
 */
class LocalRegistry {
public:
    /**
     * @param addr Will be populated with the node's address
     * @param user Will be populated with the user to use for the call
     * @param password Will be populated with the password to use for the call

     * @returns true if the number is listed in the local registry.
     * If found, the addr is filled in with the address/port number
     * of the destination.
     */
    virtual bool lookup(const char* destNumber, sockaddr_storage& addr,
        fixedstring& user, fixedstring& password) = 0;
};

/**
 * An implementation of the Line interface that communicates with
 * IAX2 stations via UDP. 
 */
class LineIAX2 : public Line {
public:

    class Call;

    /**
     * @param consumer This is the sink interface that received messages
     * will be sent to. VERY IMPORTANT: Audio frames will not have been 
     * de-jittered before they are passed to this sink. 
     * @param destAuthorizer An interface that is used to validate the 
     * DESTINATIONS of incoming calls. In other words, this can 
     * control what numbers this channel will ACCEPT calls to. Not
     * to be confused with validation of the caller (that is handled
     * internally).
     * @param sourceAuthorizer An interface that is used to validate the 
     * SOURCES of incoming calls. In other words, this can 
     * control what numbers this channel will ACCEPT calls from. 
     * @param publicUser The IAX2 user sent for calls to public nodes.
     */
    LineIAX2(Log& log, Log& traceLog, Clock& clock, int lineId, MessageConsumer& consumer,
        NumberAuthorizer* destAuthorizer, NumberAuthorizer* sourceAuthorizer, 
        LocalRegistry* locReg, unsigned destLineId, const char* publicUser,
        LineIAX2::Call* callSpace, unsigned callSpaceLen);

    /**
     * Controls the root of the DNS queries that are used to resolve IP addresses
     * and lookup public keys. Defaults to "allstarlink.org"
     */
    void setDNSRoot(const char* dnsRoot);

    /** 
     * Controls whether a regular poke is issued. This would generally be used 
     * to keep a UDP hole punch open.
     */
    void setPokeEnabled(bool b) { _pokeEnabled = b; }

    /**
     * This flag enables to extended capabilities:
     * 1. If a POKE message has a TARGET_ADDR then the POKE is forwarded on
     *    to the designated address.
     * 2. If a POKE message has a TARGET_ADDR2 then the PONG that is generated
     *    will use that value as the TARGET_ADDR.
     * 3. If a PONG message has a TARGET_ADDR then the PONG is forwarded on
     *    to the designated address.
     */
    void setDirectedPokeEnabled(bool b) { _supportDirectedPoke = b; }

    /**
     * The address and port in xx.xx.xx.xx:yyyy (IPv4) or [xx:xx:xx:xx]:yyyy (IPv6)
     * format.
     */
    void setPokeAddr(const char* addrAndPort);
    
    // #### TODO: MAKE THIS A LIST
    void setPokeNodeNumber(const char* nodeNumber);

    /**
     * Sets the private ED25519 seed. This is a 64-byte ASCII hex string.
     */
    void setPrivateKey(const char* privateKeyHex);

    enum AuthMode {
        OPEN,
        SOURCE_IP,
        CHALLENGE_ED25519
    };

    void setAuthMode(AuthMode mode);

    /**
     * Opens the network connection for in/out traffic for this line.
     *  
     * NOTE: At the moment listening happens on all local interfaces.
     *
     * @param listenFamily - Either AF_INET or AF_INET6
     * @param listenPort
     * @returns 0 if the open was successful.
     */
    int open(short addrFamily, int listenPort);

    /**
     * Resets all calls as a side-effect.
     */
    void close();

    /**
     * Calls the target node. This will use either (a) the explicit target
     * information (b) the local registry (c) DNS to convert the target 
     * public node number to an IP address/port.
     * 
     * @param targetNode Can be a node number or an explicit target address 
     *   in the format of: user@address:port/number,password
     * @returns 0 on success, -4 target syntax error, -1 call already active,
     *   -2 max call limit exceeded, -5 address format error.
     * 
     */
    int call(const char* localNumber, const char* targetNode);

    /**
     * Drops the target node number.
     * @returns 0 On success, -1 on failure (not found)
     */
    int drop(const char* localNumber, const char* targetNumber);

    int dropCall(unsigned callId);

    /**
     * Drops all connections that aren't marked as permanent.
     */
    void dropAllNonPermanent();

    /**
     * Drops all outbound connections.
     */
    void dropAllOutbound();

    void setTrace(bool a) { _trace = a; }

    unsigned getActiveCalls() const;

    void processManagementCommand(const char* msg);

    /**
     * Used to identify the active talker for outbound voice 
     * traffic for the designed local call.
     */
    //void setLocalTalkerName(unsigned localCallId, const char* name);
    
    /**
     * @returns The name of who is talking on the other side of the 
     * designated call.
     */
    fixedstring getRemoveTalkerName(unsigned localCallId);

    // ----- Line/MessageConsumer-----------------------------------------------------

    virtual void consume(const Message& m);

    // ----- Runnable -------------------------------------------------------

    virtual bool run2();
  
    virtual void oneSecTick();
    virtual void tenSecTick();

    /**
     * This function is called by the EventLoop to collect the list of file 
     * descriptors that need to be monitored for asynchronous activity.
     */
    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);

    // There is one instance of this call for each active call on the Channel.
    // This is where all of the call-specific state should live.

    class Call {
    public:

        Call();

        enum State {
            STATE_NONE,
            STATE_LOOKUP_0,
            STATE_LOOKUP_0A,
            STATE_LOOKUP_1A,
            // Go into this state when ready to start initiating a call
            STATE_INITIATION_WAIT,
            STATE_WAITING,
            // Sent out the DNS request to get the caller's public key
            STATE_AUTHREP_WAIT_0,
            // Sent out the AUTHREQ challenge and waiting for the response
            STATE_AUTHREP_WAIT_1,
            // Waiting for DNS to respond to the validation request
            STATE_IP_VALIDATION_0,
            // All validations complete, clear to send accept
            STATE_CALLER_VALIDATED,
            STATE_LINKED,
            STATE_UP,
            // This is the state that requests a termination. 
            STATE_TERMINATE_WAITING,
            // This is a state that we enter to shutdown a connection.
            // We stay here for a short time to allow the retransmission of any 
            // unACKd messages. Once the retransmit buffer empties the connection
            // is closed.
            STATE_TERMINATED
        };

        enum Side {
            SIDE_NONE,
            SIDE_CALLER,
            SIDE_CALLED
        };

        bool active = false;
        Side side = Side::SIDE_NONE;
        State state = State::STATE_NONE;
        bool trusted = false;
        bool sourceAddrValidated = false;
        unsigned localCallId = 0;
        unsigned remoteCallId = 0;
        uint32_t localStartMs = 0;
        // Used by dispenseElapsed() function. 
        uint32_t lastElapsedMsDispensed = 0;
        uint8_t outSeqNo = 0;
        uint8_t expectedInSeqNo = 0;
        // TODO: CLARIFY RX/TX
        uint32_t lastVoiceFrameElapsedMs = 0;
        fixedstring localNumber;
        fixedstring remoteNumber;
        fixedstring callUser;
        fixedstring callPassword;
        fixedstring calltoken;
        // This is the ED5519 public key in binary format
        unsigned char publicKeyBin[32];
        sockaddr_storage peerAddr;
        uint32_t supportedCodecs = 0;
        CODECType codec = CODECType::IAX2_CODEC_UNKNOWN;
        uint32_t lastFrameRxMs = 0;
        uint32_t terminationMs = 0;
        
        // Used to smooth/estimate network delay
        int32_t networkDelayEstimateMs = 0;
        float _ndi = 0;
        float _ndi_1 = 0;
        float _nvi = 0;
        float _nvi_1 = 0;
        float _nAlpha = 0.75;

        // Here is where outbound frames are stored in case
        // they are needed for retransmission later.
        amp::RetransmissionBufferStd reTx;

        // Used to track which DNS response ID we are waiting for
        uint16_t dnsRequestId;
        uint32_t lastPingSentMs = 0;
        int32_t lastPingTimeMs = 0;
        unsigned pingCount = 0;

        // This is used for tracking LAGRQ/LAGRP latency
        int32_t lastLagMs = 0;
        uint32_t lastLagrqMs = 0;
        const uint32_t LAGRQ_INTERVAL_MS = 10 * 1000;

        uint64_t lastRxVoiceFrameMs = 0;
        uint64_t lastTxVoiceFrameMs = 0;
        // When a NEW request is sent out during call initiation
        uint32_t _callInitiatedMs = 0;
        
        void reset();

        void oneSecTick(Log& log, Clock& clock, LineIAX2& line);

        void tenSecTick(Log& log, Clock& clock, LineIAX2& line);

        /**
         * @returns The milliseconds since the start of the call, based on
         * the local clock. The reference point is established at the 
         * start of the call.
         */
        uint32_t localElapsedMs(Clock& clock) const;

        /**
         * These functions give out the call elapsed time-stamp that should be 
         * put into the message header.
         *
         * The alignment is significant for voice frames since we want them 
         * to go out with time-stamps of 20, 40, 60, 80, ....
         *
         * However, if a non-voice timestamp is dispensed in the 20ms window 
         * we will not give out an earlier time just to achieve alignment.
         *
         * @return The milliseconds elapsed since the start of the call, with the 
         * ability to request to have the time aligned on the current 20ms boundary.
         */
        uint32_t dispenseElapsedMs(Clock& clock);
        uint32_t dispenseElapsedMsForVoice(const Message& msg);

        void incrementExpectedInSeqNo() {
            // This is a one-byte number that is expected to wrap.
            expectedInSeqNo++;
        }

        /**
         * @return true if the address specified matches the peer of 
         * this call.
         */
        bool isPeerAddr(const sockaddr& addr) const;

        void logStats(Log& log);
        void resetStats();

        void setNetworkDelayEstimate(unsigned ms, bool first = false);

        void dtmfGen(Log& log, Clock& clock, LineIAX2& line, char symbol);
    };

    friend class Call;

private:

    Log& _log;
    Log& _traceLog;
    Clock& _clock;
    const unsigned _busId;
    MessageConsumer& _bus;
    // An interface used to validate wether a call to a target number
    // should be allowed.
    NumberAuthorizer* _destAuthorizer = 0;
    // Used to determine whether the source number is allowed
    NumberAuthorizer* _sourceAuthorizer = 0;
    // Used to resolve targets using a local file
    LocalRegistry* _locReg = 0;
    // The line that all messages are directed to
    const unsigned _destLineId;
    const fixedstring _publicUser;
    // The startup time of this line. Mostly used for generating unique
    // tokens.
    const uint32_t _startTime;

    // This is an ED25519 private key in ASCII Hex format (exactly 64 characters)
    char _privateKeyHex[65];
    char _dnsRoot[32];

    // The IP address family used for this connection. Either AF_INET
    // or AF_INET6.
    short _addrFamily = 0;
    // The UDP port number used to receive IAX packet
    int _iaxListenPort = 0;
    // The UDP socket on which IAX messages are received/sent
    int _iaxSockFd = 0;

    // Used to assign unique IDs to the calls.  Starting at 100
    // because call ID 1 may have special significance on 
    // initial connection.
    int _localCallIdCounter = 20;

    Call* const _calls;
    const unsigned _maxCalls;

    // Enables detailed network tracing
    bool _trace = false;
    // The UDP socket with which DNS calls are made
    int _dnsSockFd = 0;
    // Used for generating unique IDs for DNS requests
    unsigned int _dnsRequestIdCounter = 1;
    // Diagnostics    
    unsigned _invalidCallPacketCounter = 0;
    // Controls whether source IP validation is required
    bool _sourceIpValidationRequired = false;
    // Controls authentication methods, only relevant for inbound calls
    bool _authorizeWithCalltoken = true;
    bool _authorizeWithAuthreq = false;

    bool _pokeEnabled = false;
    char _pokeAddr[65];
    bool _supportDirectedPoke = false;
    // #### TODO: MAKE THIS A LIST
    char _pokeNodeNumber[17];

    /**
     * Drops all calls that match the predicate.
     * @returns The number of calls dropped
     */
    unsigned _dropIf(std::function<bool(const Call& call)> pred);

    /**
     * @return true if there might be more work to be done
     */
    bool _progressCalls();

    /**
     * @return true if there might be more work to be done
     */
    bool _progressCall(Call& call);

    /**
     * @return true if there might be more work to be done
     */
    bool _processInboundIAXData();
    void _processReceivedIAXPacket(const uint8_t* buf, unsigned bufLen, 
        const sockaddr& peerAddr, uint32_t stampMs);
    void _processMiniFrame(const uint8_t* buf, unsigned bufLen, const sockaddr& peerAddr, 
        uint32_t stampMs);
    void _processFullFrame(const uint8_t* buf, unsigned bufLen, const sockaddr& peerAddr, 
        uint32_t stampMs);
    void _processFullFrameInCall(const IAX2FrameFull& frame, Call& call, 
        uint32_t stampMs);

    void _sendACK(uint32_t timeStamp, Call& call); 
    void _sendREJECT(uint16_t destCall, const sockaddr& peerAddr, const char* cause);
    void _hangupCall(Call& call);
    /**
     * Puts the call in terminate mode. The call will 
     * continue to be active long enough for the peer
     * to request any needed re-transmissions.
     */
    void _terminateCall(Call& call);
    // ### TODO: NEED TO DESIGNATE CALL
    void _dtmfGen(char symbol);
    // Generates a signal message for a call failure and publishes it 
    // onto the bus. 
    void _publishCallFailed(const char* localNumber, const char* remoteNumber, 
        const char* msg);

    /**
     * Sends a frame to the call peer. Also deals with call management:
     * 1. The frame is saved in case a retransmit is needed.
     * 2. If necessary, the outbound sequence number is incremented.
     */
    void _sendFrameToPeer(const IAX2FrameFull& frame, Call& call);
    void _sendFrameToPeer(const IAX2FrameFull& frame, const sockaddr& peerAddr);
    void _sendFrameToPeer(const uint8_t* frame, unsigned frameSize, const sockaddr& peerAddr);

    int _sendDNSRequestSRV(uint16_t requestId, const char* name);
    int _sendDNSRequestA(uint16_t requestId, const char* name);
    int _sendDNSRequestTXT(uint16_t requestId, const char* name);
    int _sendDNSRequest(const uint8_t* dnsPacket, unsigned dnsPacketLen);

    /**
     * @return true if there might be more work to be done
     */
    bool _processInboundDNSData();
    void _processReceivedDNSPacket(const uint8_t* buf, unsigned bufLen, const sockaddr& peerAddr);
    void _processDNSResponse0(Call& call, const uint8_t* buf, unsigned bufLen);
    void _processDNSResponse1(Call& call, const uint8_t* buf, unsigned bufLen);
    void _processDNSResponseIPValidation(Call& call, const uint8_t* buf, unsigned bufLen);
    void _processDNSResponsePublicKey(Call& call, const uint8_t* buf, unsigned bufLen);

    int _allocateCallIx();   

    void _visitActiveCallsIf(std::function<void(LineIAX2::Call& call)> visitor,
        std::function<bool(const LineIAX2::Call& call)> predicate);
    void _visitActiveCallsIf(std::function<void(const LineIAX2::Call& call)> visitor, 
        std::function<bool(const LineIAX2::Call& call)> predicate) const;
};

}
