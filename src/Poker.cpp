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
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <arpa/inet.h>

#include <cassert>
#include <cstring>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"
#include "kc1fsz-tools/MicroDNS.h"
#include "kc1fsz-tools/NetUtils.h"
#include "kc1fsz-tools/raiiholder.h"

#include "IAX2FrameFull.h"
#include "IAX2Util.h"
#include "Message.h"
#include "Poker.h"
#include "ThreadUtil.h"

using namespace std;

namespace kc1fsz {

Poker::Result Poker::poke(Log& log, Clock& clock, Poker::Request req) {
    return poke(log, clock, req.bindAddr, req.nodeNumber, req.timeoutMs);
}

Poker::Result Poker::poke(Log& log, Clock& clock, const char* bindAddr,
    const char* nodeNumber, unsigned timeoutMs) {

    unsigned char answer[128];
    int answerLen;

    log.info("Test requested for node %s from %s", 
        nodeNumber, bindAddr);

    // Query the SRV record
    char dname[64];
    snprintf(dname, 64, "_iax._udp.%s.nodes.allstarlink.org", nodeNumber);
    answerLen = res_query(dname, 1, 33, answer, sizeof(answer));
    if (answerLen < 0) 
        return { .code = -1 };

    uint16_t pri;
    uint16_t weight;
    uint16_t iaxPort;
    char hostname[64];
    int rc2 = microdns::parseDNSAnswer_SRV(answer, answerLen, &pri, &weight, &iaxPort,
        hostname, sizeof(hostname));
    if (rc2 < 0) 
        return { .code = -2 };

    // Resolve the hostname to an IP address
    answerLen = res_query(hostname, 1, 1, answer, sizeof(answer));
    if (answerLen < 0) 
        return { .code = -1 };
    uint32_t addr;
    rc2 = microdns::parseDNSAnswer_A(answer, answerLen, &addr);
    if (rc2 < 0)
        return { .code = -3 };

    char dottedAddr[32];
    formatIP4Address(addr, dottedAddr, sizeof(dottedAddr));

    log.info("Testing %s:%d", dottedAddr, iaxPort);

    // Create a UDP socket 
    // The IP address family used for this connection. Either AF_INET
    // or AF_INET6.
    short addrFamily = AF_INET;

    int iaxSockFd = socket(addrFamily, SOCK_DGRAM, 0);
    if (iaxSockFd < 0) {
        log.error("Unable to open IAX port (%d)", errno);
        return { .code = -4 };
    }    
    // Setup a holder so we are sure to close the socket 
    raiiholder<int> fdHolder(&iaxSockFd, [](int* fdp) { ::close(*fdp); });
    
    // Configure reuse
    int optval = 1; 
    // This allows the socket to bind to a port that is in TIME_WAIT state,
    // or allows multiple sockets to bind to the same port (useful for multicast).
    if (setsockopt(iaxSockFd, SOL_SOCKET, SO_REUSEADDR, 
        (const char*)&optval, sizeof(optval)) < 0) {
        log.error("IAX setsockopt SO_REUSEADDR failed (%d)", errno);
        return { .code = -5 };
    }

    // Set a timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeoutMs * 1000;
    if (setsockopt(iaxSockFd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) 
        return { .code = -5 };

    // Bind locally
    struct sockaddr_storage servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.ss_family = addrFamily;
    if (addrFamily == AF_INET) {
        // Bind to a specific interface
        ((sockaddr_in&)servaddr).sin_addr.s_addr = inet_addr(bindAddr);
        ((sockaddr_in&)servaddr).sin_port = htons(0);
    }
    else if (addrFamily == AF_INET6) {
        // To bind to all available IPv6 interfaces
        // TODO: Allow this to bec controlled
        ((sockaddr_in6&)servaddr).sin6_addr = in6addr_any; 
        ((sockaddr_in6&)servaddr).sin6_port = htons(0);
    } else {
        assert(false);
    }

    if (::bind(iaxSockFd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
        return { .code = -6 };

    sockaddr_storage peerAddr;
    peerAddr.ss_family = addrFamily;
    setIPAddr(peerAddr, dottedAddr);
    setIPPort(peerAddr, iaxPort);
    const sockaddr& peerAddr2 = (const sockaddr&)peerAddr;

    const uint32_t originTime = clock.time();

    // Make a poke message
    IAX2FrameFull poke2;
    poke2.setHeader(
        // Call IDs unused
        0, 0,
        // Timestamp
        originTime,
        // SEQ UNUSED
        0, 0, 
        FrameType::IAX2_TYPE_IAX, 
        IAXSubclass::IAX2_SUBCLASS_IAX_POKE);

    uint64_t startTimeUs = clock.timeUs();
    
    int rc = ::sendto(iaxSockFd, 
        poke2.buf(), poke2.size(), 
        0, &peerAddr2, getIPAddrSize(peerAddr2));
    if (rc != (int)poke2.size())
        return { .code = -8 };
       
    // Pull back a response
    struct sockaddr_storage respAddr;
    socklen_t respAddrLen = sizeof(respAddr);
    const unsigned readBufferSize = 2048;
    uint8_t readBuffer[readBufferSize];

    rc = recvfrom(iaxSockFd, readBuffer, readBufferSize, 0, 
        (sockaddr*)&respAddr, &respAddrLen);
    if (rc == -1)  {
        if (errno == EWOULDBLOCK) 
            return { .code = -9 };
        else 
            return { .code = -10 };
    }

    uint64_t endTimeUs = clock.timeUs();

    IAX2FrameFull respFrame(readBuffer, rc);

    // Make sure we got a PONG back
    if (!respFrame.isTypeClass(FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_PONG))         
        return { .code = -11 };

    // Make sure the timestamp matches
    if (respFrame.getTimeStamp() != originTime)
        return { .code = -12 };

    Result r;
    r.code = 0;
    strcpyLimited(r.addr4, dottedAddr, sizeof(r.addr4));
    r.port = iaxPort;
    r.pokeTimeMs = (endTimeUs - startTimeUs) / 1000;
    
    return r;
}

void Poker::loop(Log* log, Clock* clock, 
    threadsafequeue2<Message>* reqQueue,
    threadsafequeue2<Message>* respQueue, std::atomic<bool>* runFlag) {

    amp::setThreadName("NetDiag");
    amp::lowerThreadPriority();

    log->info("Start network diagnostic thread");

    while (runFlag->load()) {

        // Attempt to take a request off the request queue. This has a 
        // long timeout to avoid high CPU, but not too long because 
        // this block is on the critical path of exiting the thread.
        Message msg;
        if (reqQueue->try_pop(msg, 500)) {
            if (msg.getType() == Message::Type::NET_DIAG_1_REQ) {
                Poker::Request req;
                memcpy(&req, msg.body(), sizeof(req));
                Poker::Result r = Poker::poke(*log, *clock, req);
                Message res(Message::Type::NET_DIAG_1_RES, 0, 
                    sizeof(Poker::Result),  (const uint8_t*)&r, 0, 0);
                res.setSource(msg.getDestBusId(), msg.getDestCallId());
                res.setDest(msg.getSourceBusId(), msg.getSourceCallId());
                respQueue->push(res);
            }
        }
    }

    log->info("End network diagnostic thread");
}

}
