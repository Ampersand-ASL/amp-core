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

#include "Runnable2.h"
#include "IAX2Util.h"
#include "Message.h"
#include "MessageConsumer.h"
#include "VoterPeer.h"

namespace kc1fsz {

class Log;
class Clock;

class VoterClient : public Runnable2, public MessageConsumer {
public:

    /**
     * @param consumer This is the sink interface that received messages
     * will be sent to. VERY IMPORTANT: Audio frames will not have been 
     * de-jittered before they are passed to this sink. 
     * @param audioDestLineId The line ID used to send any inbound audio 
     * received on the network.
     */
    VoterClient(Log& log, Clock& clock, int lineId, MessageConsumer& consumer,
        unsigned audioDestLineId);

    /**
     * Opens the network connection for in/out traffic for this line.
     *  
     * NOTE: At the moment listening happens on all local interfaces.
     *
     * @returns 0 if the open was successful.
     */
    int open(const char* serverAddrAndPort);

    void close();
    
    void setClientPassword(const char* p);

    void setServerPassword(const char* p);

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
    // The line ID used to send any inbound audio received on the network
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
    sockaddr_storage _serverAddr;

    amp::VoterPeer _client;
};

}
