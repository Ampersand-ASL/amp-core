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
#include <termios.h>
#include <sys/ioctl.h>

#include <iostream>
#include <cassert>
#include <cstring> 

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

int SignalOut::openHid(const char* hidName, const char* signalName) {

    close();

    if ((_fd = ::open(hidName, O_RDWR | O_NONBLOCK)) < 0) {
        _log.error("Cannot open HID device %s %d", hidName, errno);
        _fd = -1;
        _hidFailed = true;
        return -1;
    }

    _hidPacketSize = 4;
    _hidOffset = 0;
    _hidMask = 0x04;
    _hidFailed = false;
    _mode = Mode::MODE_HID;

    return 0;
}

int SignalOut::openSerial(const char* deviceName, const char* signal) {

    close();

    if (strcmp(signal, "rts") == 0) {
        _ticomMask = TIOCM_RTS;
    }
    else if (strcmp(signal, "dtr") == 0) {
        _ticomMask = TIOCM_DTR;
    }
    else {
        _log.info("Unrecognized serial signal");
        return -1;
    }

    if ((_fd = ::open(deviceName, O_RDWR | O_NONBLOCK | O_NOCTTY)) < 0) {
        _log.error("Cannot open serial device %d", errno);
        _fd = -1;
        return -1;
    }

    _mode = Mode::MODE_SERIAL;

    return 0;
}

void SignalOut::close() {
    if (_fd != -1)
        ::close(_fd);
    _fd = -1;
    _mode = Mode::MODE_NONE;
}

// ----- MessageConsumer --------------------------------------------------

void SignalOut::consume(const Message& msg) {
    if (_mode == Mode::MODE_HID) {
        if (msg.isSignal(_sigTypeOn)) {
            // RELEVANT: https://github.com/twilly/cm108/blob/master/cm108.c
            // #### TODO: GENERALIZE
            char msg[5] = { 0 };
            // GPIO3
            msg[2] = _hidMask;
            msg[3] = _hidMask;
            int rc = write(_fd, msg, 5);
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
            int rc = write(_fd, msg, 5);
            if (rc != 5)
                _log.info("Signal out write error %d %d", rc, errno);
        }
    }
    else if (_mode == Mode::MODE_SERIAL) {
        // Get existing status
        int status;
        ioctl(_fd, TIOCMGET, &status);
        if (msg.isSignal(_sigTypeOn)) {
            status |= _ticomMask;
        } else if (msg.isSignal(_sigTypeOff)) {
            status &= ~_ticomMask;
        }
        ioctl(_fd, TIOCMSET, &status);
    }
}

    }
}
