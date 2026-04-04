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

#include <functional>

#include "Line.h"
#include "IAX2Util.h"
#include "Message.h"
#include "MessageConsumer.h"

#include "voter/VoterPeer.h"

namespace kc1fsz {

class Log;
class Clock;

class LineVoter : public Line {
public:

    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned MAX_PEERS = 8;

    /**
     * @param consumer This is the sink interface that received messages
     * will be sent to. 
     */
    LineVoter(Log& log, Clock& clock, unsigned lineId, MessageConsumer& consumer, 
        unsigned audioDestLineId);

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
    
    void setServerPassword(const char* p);

    /**
     * @param ps A comma-separated list of client passwords. Will also include some 
     * flags (transmit). So for example: password0,password1,password2/transmit,password3.
     */
    void setClientPasswords(const char* ps);

    void setTrace(bool a) { _trace = a; }

    // ----- Line/MessageConsumer-----------------------------------------------------

    virtual void consume(const Message& m);

    // ----- Runnable -------------------------------------------------------

    virtual bool run2();  
    virtual void audioRateTick(uint32_t tickTimeMs);
    virtual void oneSecTick();
    virtual void tenSecTick();

    /**
     * This function is called by the EventLoop to collect the list of file 
     * descriptors that need to be monitored for asynchronous activity.
     */
    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);

private:

    bool _processInboundData();
    void _processReceivedPacket(const uint8_t* buf, unsigned bufLen, 
        const sockaddr& peerAddr, uint32_t stampMs);
    void _sendPacketToPeer(const uint8_t* b, unsigned len, 
        const sockaddr& peerAddr);

    Log& _log;
    Clock& _clock;
    const unsigned _lineId;
    MessageConsumer& _bus;
    // Where the inbound audio gets sent
    const unsigned _audioDestLineId;

    // The IP address family used for this connection. Either AF_INET
    // or AF_INET6.
    short _addrFamily = 0;
    // The UDP port number used to receive IAX packet
    int _listenPort = 0;
    // The UDP socket on which IAX messages are received/sent
    int _sockFd = 0;
    // Enables detailed network tracing
    bool _trace = false;

    std::string _serverChallenge;
    std::string _serverPassword;

    amp::VoterPeer _clients[MAX_PEERS];

    bool _lastTickAudio = false;
};

}
