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
:   _log(log), 
    _clock(clock), 
    _bus(bus),  
    _sigTypeOn(sigTypeOn),
    _sigTypeOff(sigTypeOff),
    // The debouncer is looking at the last raw value received from the message bus
    _debouncedState(clock, [this]() {
        return _rawValue;
    }) {
    // Attack is immediate
    _debouncedState.setActiveTime(0);
    // Release has some delay
    // ### TODO: MAKE THIS CONFIGURABLE
    _debouncedState.setInactiveTime(200);
}

int SignalOut::openHid(const char* hidName, const char* signalName) {

    // Ignore inconsequential opens
    if (_mode == Mode::MODE_HID && _deviceName == hidName && _signalName == signalName && 
        !_hidFailed)
        return 0;

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
    _deviceName = hidName;
    _signalName = signalName;

    // Set initial state
    _rawValue = false;
    _setOfficialValue(false);

    return 0;
}

int SignalOut::openSerial(const char* deviceName, const char* signalName) {

    // Ignore inconsequential opens
    if (_mode == Mode::MODE_SERIAL && _deviceName == deviceName && _signalName == signalName)
        return 0;

    close();

    if (strcmp(signalName, "rts") == 0)
        _ticomMask = TIOCM_RTS;
    else if (strcmp(signalName, "dtr") == 0)
        _ticomMask = TIOCM_DTR;
    else {
        _log.info("Unrecognized serial signal");
        return -1;
    }

    // Serial modem control signals are manipulated using ioctl() calls. So 
    // the port needs to be opened for this to work.
    if ((_fd = ::open(deviceName, O_RDWR | O_NONBLOCK | O_NOCTTY)) < 0) {
        _log.error("Cannot open serial device %d", errno);
        _fd = -1;
        return -1;
    }

    _mode = Mode::MODE_SERIAL;
    _deviceName = deviceName;
    _signalName = signalName;

    // Set initial state
    _rawValue = false;
    _setOfficialValue(false);

    return 0;
}

void SignalOut::close() {
    if (_fd != -1)
        ::close(_fd);
    _fd = -1;
    _mode = Mode::MODE_NONE;
    _deviceName.clear();
    _signalName.clear();
}

// ----- MessageConsumer --------------------------------------------------

void SignalOut::consume(const Message& msg) {
    if (msg.isSignal(_sigTypeOn))
        _rawValue = true;
    else if (msg.isSignal(_sigTypeOff))
        _rawValue = false;
}

void SignalOut::audioRateTick(uint32_t tickMs) {
    // Look for the transition of the debounced state
    if (_debouncedState.get() != _officialValue) {        
        _officialValue = _debouncedState.get();
        _setOfficialValue(_officialValue);
    }
}

void SignalOut::_setOfficialValue(bool s) {
    if (_fd != -1) {
        if (_mode == Mode::MODE_HID) {
            if ((!_invert && s) || (_invert && !s)) {
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
            else {
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
            int status = 0;
            if (ioctl(_fd, TIOCMGET, &status) != 0)
                return;
            if ((!_invert && s) || (_invert && !s))
                status |= _ticomMask;
            else
                status &= ~_ticomMask;
            ioctl(_fd, TIOCMSET, &status);
        }
    }
}

    }
}
