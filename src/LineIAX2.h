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

// ##### TODO CHANGE TO CallDestinationValidator
class CallValidator {
public:
    /**
     * @returns true if the destination number is reachable.
     */
    virtual bool isNumberAllowed(const char* destNumber) const = 0;
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
     * @returns true if the number is listed in the local registry.
     * If found, the addr is filled in with the address/port number
     * of the destination.
     */
    virtual bool lookup(const char* destNumber, sockaddr_storage& addr) = 0;
};

/**
 * An implementation of the Line interface that communicates with
 * IAX2 stations via UDP. 
 */
class LineIAX2 : public Line {
public:

    /**
     * @param consumer This is the sink interface that received messages
     * will be sent to. VERY IMPORTANT: Audio frames will not have been 
     * de-jittered before they are passed to this sink. 
     * @param destValidator An interface that is used to validate the 
     * DESTINATIONS of incoming calls. In other words, this can 
     * control what numbers this channel will ACCEPT calls for. Not
     * to be confused with validation of the caller (that is handled
     * internally).
     * @param supportDirectedPoke Please see documentation. This controls
     * whether POKE to another address can be requested.
     * @param privateKeyHex The server's private ED25519 seed in ASCII/Hex
     * representation. This should be exactly 64 characters long.
     */
    LineIAX2(Log& log, Clock& clock, int lineId, MessageConsumer& consumer,
        CallValidator* destValidator = 0, LocalRegistry* locReg = 0,
        bool supportDirectedPoke = false, const char* privateKeyHex = 0);

    /**
     * Opens the network connection for in/out traffic for this line.
     *  
     * NOTE: At the moment listening happens on all local interfaces.
     *
     * @param listenFamily - Either AF_INET or AF_INET6
     * @param listenPort
     * @param localNumber
     * @param localUser (Research in process to see if this is relevent here)
     *
     * @returns 0 if the open was successful.
     */
    int open(short addrFamily, int listenPort, const char* localUser);

    void close();

    /**
     * This will use DNS to convert the target number to an IP address.
     */
    int call(const char* localNumber, const char* targetNumber);

    void disconnectAllNonPermanent();

    void setTrace(bool a) { _trace = a; }
    unsigned getActiveCalls() const;

    void processManagementCommand(const char* msg);

    // ----- Line/MessageConsumer-----------------------------------------------------

    virtual void consume(const Message& m);

    // ----- Runnable -------------------------------------------------------

    virtual bool run2();

    /**
     * Audio rate tick is required here because of some background (timeout)
     * tasks that are still happening.
     * #### TODO: REMOVE THIS
     */
    virtual void audioRateTick(uint32_t tickMs);
    
    virtual void oneSecTick();
    virtual void tenSecTick();

    /**
     * This function is called by the EventLoop to collect the list of file 
     * descriptors that need to be monitored for asynchronous activity.
     */
    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);

private:

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
            STATE_INITIAL,
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
            STATE_TERMINATE_WAITING,
            STATE_TERMINATED,
            STATE_CALL_FAILED,
            STATE_POKE_0
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
        int localCallId = 0;
        int remoteCallId = 0;
        uint32_t localStartMs = 0;
        uint32_t lastElapsedTimeStampDispensed = 0;
        uint8_t outSeqNo = 0;
        uint8_t expectedInSeqNo = 0;
        // TODO: CLARIFY RX/TX
        uint32_t lastVoiceFrameElapsedMs = 0;
        uint32_t lastHangupMs = 0;
        fixedstring localNumber;
        fixedstring remoteNumber;
        fixedstring remoteUser;
        fixedstring calltoken;
        // This is the ED5519 public key in binary format
        unsigned char publicKeyBin[32];
        sockaddr_storage peerAddr;
        uint32_t supportedCodecs = 0;
        CODECType codec = CODECType::IAX2_CODEC_UNKNOWN;
        uint32_t lastFrameRxMs = 0;
        uint32_t terminationMs = 0;
        
        // A positive lag means the remote clock is behind, a negative lag means the 
        // remote clock is ahead.
        //
        // Or: frameTimeStamp + lag = localtime
        int32_t remoteClockLagEstimateMs = 0;

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
        // Used for VOX keying
        bool vox = false;
        uint32_t nextLMs = 0;

        uint32_t lastPingSentMs = 0;
        int32_t lastPingTimeMs = 0;
        unsigned pingCount = 0;

        // This is used for tracking LAGRQ/LAGRP latency
        int32_t lastLagMs = 0;
        uint32_t lastLagrqMs = 0;
        const uint32_t lagrqIntervalMs = 10 * 1000;

        uint32_t lastRxVoiceFrameMs = 0;

        void reset();

        void audioRateTick(Log& log, Clock& clock, MessageConsumer& cons, 
            unsigned dest, LineIAX2& line);
        void oneSecTick(Log& log, Clock& clock, LineIAX2& line);

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
        uint32_t dispenseElapsedMs(Clock& clock, bool voiceAlignment = false);
        uint32_t dispenseElapsedUsingMessageOrigin(const Message& msg,
            bool voiceAlignment = false);
        uint32_t dispenseElapsedFrom(uint32_t ts, bool voiceAlignment = false);

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
    };

    friend class Call;

    Log& _log;
    Clock& _clock;
    unsigned _busId;
    MessageConsumer& _bus;
    // An interface used to validate wether a call to a target number
    // should be allowed.
    CallValidator* _validator = 0;
    // Used to resolve targets using a local file
    LocalRegistry* _locReg = 0;
    bool _supportDirectedPoke;
    // This is an ED25519 private key in ASCII Hex format (exactly 64 characters)
    char _privateKeyHex[65];

    // The startup time of this line. Mostly used for generating unique
    // tokens.
    uint32_t _startTime;
    // The IP address family used for this connection. Either AF_INET
    // or AF_INET6.
    short _addrFamily = 0;
    // The UDP port number used to receive IAX packet
    int _iaxListenPort = 0;
    // NOTE: Still researching this
    fixedstring _localUser;
    // The UDP socket on which IAX messages are received/sent
    int _iaxSockFd = 0;

    // Used to assign unique IDs to the calls.  Starting at 100
    // because call ID 1 may have special significance on 
    // initial connection.
    int _localCallIdCounter = 20;

    static const unsigned MAX_CALLS = 8;
    Call _calls[MAX_CALLS];

    // Enables detailed network tracing
    bool _trace = false;
    // The UDP socket with which DNS calls are made
    int _dnsSockFd = 0;
    // Used for generating unique IDs for DNS requests
    unsigned int _dnsRequestIdCounter = 1;
    // Determines how much time we wait between call retries
    uint32_t _callRetryIntervalMs = 30 * 1000;
    // How long we wait before considering a talkspurt to be finished.
    uint32_t _voxUnkeyMs = 100;
    // Controls whether source IP validation is required
    bool _sourceIpValidationRequired = false;
    // Diagnostics    
    unsigned _invalidCallPacketCounter = 0;
    // Controls authentication methods, only relevant for inbound calls
    bool _authorizeWithCalltoken = true;
    bool _authorizeWithAuthreq = true;

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
    void _terminateCall(Call& call);

    /**
     * Sends a frame to the call peer. Also deals with call management:
     * 1. The frame is saved in case a retransmit is needed.
     * 2. If necessary, the outbound sequence number is incremented.
     */
    void _sendFrameToPeer(const IAX2FrameFull& frame, Call& call);
    void _sendFrameToPeer(const IAX2FrameFull& frame, const sockaddr& peerAddr);
    void _sendFrameToPeer(const uint8_t* frame, unsigned frameSize, const sockaddr& peerAddr);

    void _sendDNSRequestSRV(uint16_t requestId, const char* name);
    void _sendDNSRequestA(uint16_t requestId, const char* name);
    void _sendDNSRequestTXT(uint16_t requestId, const char* name);
    void _sendDNSRequest(const uint8_t* dnsPacket, unsigned dnsPacketLen);

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
