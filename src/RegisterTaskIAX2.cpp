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
#include <stdint.h>

#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"
#include "kc1fsz-tools/raiiholder.h"
#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/NetUtils.h"

#include "RegisterTaskIAX2.h"
#include "IAX2FrameFull.h"
#include "IAX2Util.h"

using namespace std;

namespace kc1fsz {

RegisterTaskIAX2::RegisterTaskIAX2(Log& log, Clock& clock) 
:   _log(log),
    _clock(clock),
    // Interval recommended by Jason N8EI on 20-Nov-2025
    _regIntervalMs(180 * 1000),
    _lastGoodRegistrationMs(0),
    _state(&clock, State::STATE_IDLE) { 
}

void RegisterTaskIAX2::configure(const char* regServerAddr, 
    const char* nodeNumber, const char* password, unsigned iaxPort) {

    _regServerAddr = regServerAddr;
    _nodeNumber = nodeNumber;
    _password = password;    
    _iaxPort = iaxPort;

    _log.info("RegisterTask %s, %s, %d", _regServerAddr.c_str(), _nodeNumber.c_str(), _iaxPort);

    _doRegister();
}

void RegisterTaskIAX2::_doRegister() {     

    if (_regServerAddr.empty() || _nodeNumber.empty() || _password.empty())
        return;

    _state = State::STATE_REG_PENDING;
}

void RegisterTaskIAX2::tenSecTick() {
    if (_state == State::STATE_IDLE && 
        _clock.isPastWindow(_lastGoodRegistrationMs, _regIntervalMs))
        _doRegister();
}

bool RegisterTaskIAX2::run2() {

    if (_state == State::STATE_IDLE) {
    }
    else if (_state == State::STATE_REG_PENDING) {
        if (_openIAX()) {
            _sendREGREQ();
            _state = State::STATE_0;
        }
        else 
            _state = State::STATE_IDLE;
    }
    else if (_state == State::STATE_0 ||
        _state == State::STATE_1) {
        _processInboundIAXData();
    }
    else if (_state == State::STATE_FAILED) {
        _closeIAX();
        _state = State::STATE_IDLE;
    }
    else assert(false);

    return true;
}

bool RegisterTaskIAX2::_openIAX() {

    int addrFamily = AF_INET;

    // UDP open/bind
    int iaxSockFd = socket(addrFamily, SOCK_DGRAM, 0);
    if (iaxSockFd < 0) {
        _log.error("Unable to open IAX port (%d)", errno);
        return false;
    }    
    
    int optval = 1; 
    // This allows the socket to bind to a port that is in TIME_WAIT state,
    // or allows multiple sockets to bind to the same port (useful for multicast).
    if (setsockopt(iaxSockFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) < 0) {
        _log.error("IAX setsockopt SO_REUSEADDR failed (%d)", errno);
        ::close(iaxSockFd);
        return false;
    }

    _iaxFd = iaxSockFd;
    return true;
}

void RegisterTaskIAX2::_closeIAX() {
    if (_iaxFd != -1)
        ::close(_iaxFd);
    _iaxFd = -1;
}

bool RegisterTaskIAX2::_sendREGREQ() {

    // 52.44.147.201:4568
    struct sockaddr_storage targetAddr;
    memset(&targetAddr, 0, sizeof(sockaddr_storage));
    if (parseIPAddrAndPort("52.44.147.201:4569", targetAddr) != 0) {
        return false;
    }
    
    // Make a REGREQ packet
    IAX2FrameFull frame;
    frame.setHeader(0, 0,
        // Timestamp
        0, 
        // OSeqNo, ISeqNo
        0, 0,
        FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_REGREQ);
    frame.addIE_str(0x06, _nodeNumber.c_str());

    _sendFrameToPeer(frame.buf(), frame.size(), (sockaddr&)targetAddr);
    
    return true;
}

void RegisterTaskIAX2::_sendFrameToPeer(const uint8_t* b, unsigned len, 
    const sockaddr& peerAddr) {

    if (_iaxFd == -1)
        return;

    _log.infoDump("Sending", b, len);

    int rc = ::sendto(_iaxFd, 
        b,
        len, 0, &peerAddr, getIPAddrSize(peerAddr));

    if (rc < 0) {
        if (errno == 101) {
            char temp[64];
            formatIPAddrAndPort(peerAddr, temp, 64);
            _log.error("Network is unreachable to %s", temp);
        } else 
            _log.error("Send error %d", errno);
    }
}

bool RegisterTaskIAX2::_processInboundIAXData() {

    if (_iaxFd == -1)
        return false;

    // Check for new data on the socket
    // ### TODO: MOVE TO CONFIG AREA
    const unsigned readBufferSize = 2048;
    uint8_t readBuffer[readBufferSize];
    struct sockaddr_storage peerAddr;
    socklen_t peerAddrLen = sizeof(peerAddr);
    int rc = recvfrom(_iaxFd, readBuffer, readBufferSize, 0, (sockaddr*)&peerAddr, &peerAddrLen);
    if (rc == 0) {
        return false;
    } 
    else if (rc == -1 && errno == 11) {
        return false;
    } 
    else if (rc > 0) {
        // The actual processing of the received packet
        _processReceivedIAXPacket(readBuffer, rc, (const sockaddr&)peerAddr, _clock.time());
        // Return back to be nice, but indicate that there might be more
        return true;
    } else {
        _log.error("IAX2 read error %d/%d", rc, errno);
        return false;
    }
}

void RegisterTaskIAX2::_processReceivedIAXPacket(
    const uint8_t* buf, unsigned bufLen,
    const sockaddr& peerAddr, uint32_t rxStampMs) {

    _log.infoDump("Received", buf, bufLen);
}

}
