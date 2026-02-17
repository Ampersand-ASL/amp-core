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
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
//#include <linux/sockios.h> // Required for SIOCOUTQ

#include <sys/types.h>

#include <cstring>
#include <iostream>
#include <algorithm>

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/NetUtils.h"
#include "kc1fsz-tools/Log.h"

#include "MessageConsumer.h"
#include "Message.h"
#include "LineVoter.h"

using namespace std;

static unsigned AUDIO_TICK_MS = 20;

namespace kc1fsz {

static uint32_t alignToTick(uint32_t ts, uint32_t tick) {
    return (ts / tick) * tick;
}

LineVoter::LineVoter(Log& log, Clock& clock, int lineId,
    MessageConsumer& bus)
:   _log(log),
    _clock(clock),
    _lineId(lineId),
    _bus(bus) {

    _server0.init(&clock, &log);
}

int LineVoter::open(short addrFamily, int listenPort) {

    // If the configuration is changing then ignore the request
    if (addrFamily == _addrFamily &&
        _listenPort == listenPort &&
        _sockFd != 0) {
        return 0;
    }

    close();

    _addrFamily = addrFamily;
    _listenPort = listenPort;

    _log.info("Listening on Voter port %d", _listenPort);

    // UDP open/bind
    int sockFd = socket(_addrFamily, SOCK_DGRAM, 0);
    if (sockFd < 0) {
        _log.error("Unable to open Voter port (%d)", errno);
        return -1;
    }    

    // #### TODO RAAI TO ADDRESS LEAKS BELOW
    
    int optval = 1; 
    // This allows the socket to bind to a port that is in TIME_WAIT state,
    // or allows multiple sockets to bind to the same port (useful for multicast).
    if (setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) < 0) {
        _log.error("Voter setsockopt SO_REUSEADDR failed (%d)", errno);
        ::close(sockFd);
        return -1;
    }

    struct sockaddr_storage servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.ss_family = _addrFamily;
    
    if (_addrFamily == AF_INET) {
        // Bind to all interfaces
        // Or, specify a particular IP address: servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        // TODO: Allow this to bec controlled
        ((sockaddr_in&)servaddr).sin_addr.s_addr = INADDR_ANY;
        ((sockaddr_in&)servaddr).sin_port = htons(_listenPort);
    } else if (_addrFamily == AF_INET6) {
        // To bind to all available IPv6 interfaces
        // TODO: Allow this to bec controlled
        ((sockaddr_in6&)servaddr).sin6_addr = in6addr_any; 
        ((sockaddr_in6&)servaddr).sin6_port = htons(_listenPort);
    } else {
        assert(false);
    }

    if (::bind(sockFd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        _log.error("Unable to bind to Voter port (%d)", errno);
        ::close(sockFd);
        return -1;
    }

    if (makeNonBlocking(sockFd) != 0) {
        _log.error("open fcntl failed (%d)", errno);
        ::close(sockFd);
        return -1;
    }

    _sockFd = sockFd;

    //server0.setLocalPassword(serverPwd.c_str());
    //server0.setLocalChallenge(serverChallenge.c_str());
    //server0.setRemotePassword(client0Pwd.c_str());


    return 0;
}

void LineVoter::close() {   
    if (_sockFd) 
        ::close(_sockFd);
    _sockFd = 0;
    _listenPort = 0;
    _addrFamily = 0;
} 

void LineVoter::setServerPassword(const char* p) {
    _server0.setLocalPassword(p);
}

void LineVoter::consume(const Message& m) {   
}

bool LineVoter::run2() {   
    return _processInboundData();
}

void LineVoter::oneSecTick() {    
}

void LineVoter::tenSecTick() {
}

int LineVoter::getPolls(pollfd* fds, unsigned fdsCapacity) {
    if (fdsCapacity < 1) 
        return -1;
    int used = 0;
    if (_sockFd) {
        // We're only watching for receive events
        fds[used].fd = _sockFd;
        fds[used].events = POLLIN;
        used++;
    }
    return used;
}

bool LineVoter::_processInboundData() {

    if (!_sockFd)
        return false;

    // Check for new data on the socket
    // ### TODO: MOVE TO CONFIG AREA
    const unsigned readBufferSize = 2048;
    uint8_t readBuffer[readBufferSize];
    struct sockaddr_storage peerAddr;
    socklen_t peerAddrLen = sizeof(peerAddr);
    int rc = recvfrom(_sockFd, readBuffer, readBufferSize, 0, (sockaddr*)&peerAddr, &peerAddrLen);
    if (rc == 0) {
        return false;
    } 
    else if (rc == -1 && errno == 11) {
        return false;
    } 
    else if (rc > 0) {
        _processReceivedPacket(readBuffer, rc, (const sockaddr&)peerAddr, _clock.time());
        // Return back to be nice, but indicate that there might be more
        return true;
    } else {
        // #### TODO: ERROR COUNTER
        _log.error("Voter read error %d/%d", rc, errno);
        return false;
    }
}

void LineVoter::_processReceivedPacket(
    const uint8_t* potentiallyDangerousBuf, unsigned bufLen,
    const sockaddr& peerAddr, uint32_t rxStampMs) {
}

}
