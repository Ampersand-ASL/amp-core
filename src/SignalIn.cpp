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
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <errno.h>

#include <iostream>

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "MessageConsumer.h"
#include "SignalIn.h"

#define DEST_CALL_ID (1)

using namespace std;

namespace kc1fsz {
    namespace amp {

SignalIn::SignalIn(Log& log, Clock& clock, MessageConsumer& bus, unsigned radioLineId,
    Message::SignalType sigTypeOn, Message::SignalType sigTypeOff) 
:   _log(log), _clock(clock), _bus(bus), _radioLineId(radioLineId), _sigTypeOn(sigTypeOn),
    _sigTypeOff(sigTypeOff) {
}

int SignalIn::openHid(const char* hidName) {

    if ((_hidFd = ::open(hidName, O_RDWR | O_NONBLOCK)) < 0) {
        _log.error("Cannot open HID device %d", errno);
        return -1;
    }

    _hidAccPtr = 0;
    _hidPacketSize = 4;
    _hidOffset = 0;
    _hidMask = 0x02;
    _hidFailed = false;

    return 0;
}

void SignalIn::close() {
    if (_hidFd)
        ::close(_hidFd);
    _hidFd = 0;
}

void SignalIn::_pollHidStatus() {

    if (_hidFd == 0 || _hidFailed)
        return;

    // We're not assuming that the HID packet will arrive in once piece. This
    // code accumulates until a complete packet is received.
    char buffer[16];
    int rc = read(_hidFd, buffer, sizeof(buffer));
    if (rc > 0) {
        for (unsigned i = 0; i < (unsigned)rc; i++) {
            _hidAcc[_hidAccPtr++] = buffer[i];
            // Full packet accumulated yet?  If so, process and reset the 
            // accumulation.
            if (_hidAccPtr == _hidPacketSize) {
                _processHidPacket(_hidAcc, _hidPacketSize);
                _hidAccPtr = 0;
            }
        }
    }
    else if (rc == -1 && errno == EWOULDBLOCK) {
        // Ignore this case, normal
    }
    else {
        _hidFailed = true;
        _log.info("Unexpected HID error %d/%d", rc, errno);
    }
}

void SignalIn::_processHidPacket(const uint8_t* packet, unsigned packetLen) {
    bool ptt = (packet[_hidOffset] & _hidMask) != 0;
    Message msg;
    if (ptt) 
        msg = Message::signal(_sigTypeOn);
    else 
        msg = Message::signal(_sigTypeOff);
    msg.setDest(_radioLineId, DEST_CALL_ID);
    _bus.consume(msg);
}

// ----- MessageConsumer --------------------------------------------------

void SignalIn::consume(const Message& frame) {
}

// ----- Runnable ---------------------------------------------------------

int SignalIn::getPolls(pollfd* fds, unsigned fdsCapacity) {
    return 0;
}

bool SignalIn::run2() {
    return false;
}

void SignalIn::audioRateTick(uint32_t tickMs) {
    _pollHidStatus();
}

    }
}
