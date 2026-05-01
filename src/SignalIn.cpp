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
/*
CM108 - HID offset 0, HID mask 0x02 used for COS.
*/
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>

#include <iostream>
#include <cstring>

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

int SignalIn::openHid(const char* hidName, const char* serial) {

    close();

    if (strcmp(serial, "default") == 0) {
        _hidOffset = 0;
        _hidMask = 0x02;
    }
    else
        return -1;

    if ((_fd = ::open(hidName, O_RDWR | O_NONBLOCK)) < 0) {
        _log.error("Cannot open HID device %d", errno);
        _fd = -1;
        _hidFailed = true;
        return -1;
    }

    _hidAccPtr = 0;
    _hidPacketSize = 4;
    _hidFailed = false;
    assert(_hidPacketSize <= MAX_HID_MESSAGE_SIZE);
    _mode = Mode::MODE_HID;

    return 0;
}

int SignalIn::openSerial(const char* deviceName, const char* signal) {

    close();

    if (strcmp(signal, "cts") == 0) {
        _ticomMask = TIOCM_CTS;
    }
    else if (strcmp(signal, "dcd") == 0) {
        _ticomMask = TIOCM_CD;
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

    _log.info("Opened serial");

    return 0;
}

void SignalIn::close() {
    if (_fd)
        ::close(_fd);
    _fd = -1;
    _mode = Mode::MODE_NONE;
}

void SignalIn::_pollHidStatus() {

    if (_fd == -1 || _hidFailed)
        return;

    // We're not assuming that the HID packet will arrive in once piece. This
    // code accumulates until a complete packet is received.
    char buffer[16];
    int rc = read(_fd, buffer, sizeof(buffer));
    if (rc > 0) {
        for (unsigned i = 0; i < (unsigned)rc; i++) {
            // NOTE ON BUFFER SAFETY: There is an assert above that checks to make
            // sure that _hidPacketSize < MAX_HID_MESSAGE_SIZE to prevent overrun.
            _hidAcc[_hidAccPtr++] = buffer[i];
            // Full packet accumulated yet?  If so, process and reset the 
            // accumulation.
            if (_hidAccPtr == _hidPacketSize) {
                _processHidPacket(_hidAcc, _hidPacketSize);
                // Keep a copy of the HID message so we can track changes
                memcpy(_previousHidAcc, _hidAcc, MAX_HID_MESSAGE_SIZE);
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

    //_log.infoDump("HID packet", packet, packetLen);

    // Note that HID messages may be generated for a number of reasons. We check
    // to see if the current HID message differs in the way that is relevant
    // to our signal by comparing to the last HID message.
    bool state = (packet[_hidOffset] & _hidMask) != 0;
    bool previousSate = (_previousHidAcc[_hidOffset] & _hidMask) != 0;
    if (state != previousSate)
        _generateEvent(state);
}

void SignalIn::_pollSerialStatus() {

    if (_fd == -1)
        return;

    int status = 0;
    ioctl(_fd, TIOCMGET, &status);
    // Mask off the bit we care about
    status &= _ticomMask;
    // If something changed then generate an event
    if (status != _ticomStatus) {
        _log.info("Serial status changed");
        _generateEvent(status != 0);
        _ticomStatus = status;
    }
}

void SignalIn::_generateEvent(bool status) {
    MessageEmpty msg = MessageEmpty::signal((status) ? _sigTypeOn : _sigTypeOff);
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

void SignalIn::audioRateTick(uint32_t) {
    if (_mode == Mode::MODE_HID)
        _pollHidStatus();
    else if (_mode == Mode::MODE_SERIAL)
        _pollSerialStatus();
}

    }
}
