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
#pragma once

#include <cstdint>

#include "Message.h"
#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;
class MessageConsumer;

    namespace amp {

class SignalIn : public Runnable2, public MessageConsumer {
public:

    SignalIn(Log& log, Clock& clock, MessageConsumer& bus, unsigned radioLineId,
         Message::SignalType sigTypeOn, Message::SignalType sigTypeOff);

    int openHid(const char* hidDeviceName, const char* signal);
    int openSerial(const char* serialDeviceName, const char* signal);

    void close();

    // ----- MessageConsumer --------------------------------------------------

    virtual void consume(const Message& frame);

    // ----- Runnable ---------------------------------------------------------

    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);
    virtual void audioRateTick(uint32_t tickMs);

private:

    enum Mode {
        MODE_NONE,
        MODE_HID,
        MODE_SERIAL
    };

    Log& _log;
    Clock& _clock;
    MessageConsumer& _bus;
    unsigned _radioLineId;
    Message::SignalType _sigTypeOn;
    Message::SignalType _sigTypeOff;
    Mode _mode = Mode::MODE_NONE;
    int _fd = 0;

    void _generateEvent(bool status);

    // ----- HID Related -----------------------------------------------------

    void _pollHidStatus();
    void _processHidPacket(const uint8_t* packet, unsigned packetLen);
    
    bool _hidFailed = false;
    static const unsigned MAX_HID_MESSAGE_SIZE = 8;
    uint8_t _hidAcc[MAX_HID_MESSAGE_SIZE] = {};
    uint8_t _previousHidAcc[MAX_HID_MESSAGE_SIZE] = {};
    unsigned _hidAccPtr = 0;

    // These can be configured according to the behavior of the HID device.
    unsigned _hidPacketSize = 4;
    // CM108 Volume Down button is register 0, bit 0x02.
    unsigned _hidOffset = 0;
    uint8_t _hidMask = 0x02;

    // ----- Serial Related ---------------------------------------------------

    int _ticomStatus;
    int _ticomMask;

    void _pollSerialStatus();
};

    }
}

   