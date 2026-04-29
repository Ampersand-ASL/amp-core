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
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <errno.h>

#include <iostream>
#include <cassert>

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "MessageConsumer.h"
#include "SignalOut.h"

using namespace std;

namespace kc1fsz {
    namespace amp {

SignalOut::SignalOut(Log& log, Clock& clock, MessageConsumer& bus, 
    Message::SignalType sigTypeOn, Message::SignalType sigTypeOff) 
:   _log(log), _clock(clock), _bus(bus),  _sigTypeOn(sigTypeOn),
    _sigTypeOff(sigTypeOff) {
}

int SignalOut::openHid(const char* hidName) {

    close();

    if ((_hidFd = ::open(hidName, O_RDWR | O_NONBLOCK)) < 0) {
        _log.error("Cannot open HID device %s %d", hidName, errno);
        _hidFd = 0;
        _hidFailed = true;
        return -1;
    }
    else {
        _log.info("OPEN");
    }

    _hidPacketSize = 4;
    _hidOffset = 0;
    _hidMask = 0x04;
    _hidFailed = false;
    return 0;
}

void SignalOut::close() {
    if (_hidFd)
        ::close(_hidFd);
    _hidFd = 0;
}

// ----- MessageConsumer --------------------------------------------------

void SignalOut::consume(const Message& msg) {
    if (msg.isSignal(_sigTypeOn)) {
        // RELEVANT: https://github.com/twilly/cm108/blob/master/cm108.c
        // #### TODO: GENERALIZE
        char msg[5] = { 0 };
        // GPIO3
        msg[2] = _hidMask;
        msg[3] = _hidMask;
        int rc = write(_hidFd, msg, 5);
        if (rc != 5)
            _log.info("Signal out write error %d %d", rc, errno);
    }
    else if (msg.isSignal(_sigTypeOff)) {
        // RELEVANT: https://github.com/twilly/cm108/blob/master/cm108.c
        // #### TODO: GENERALIZE
        char msg[5] = { 0 };
        // GPIO3
        msg[2] = _hidMask;
        msg[3] = 0;
        int rc = write(_hidFd, msg, 5);
        if (rc != 5)
            _log.info("Signal out write error %d %d", rc, errno);
    }
}
    }
}
