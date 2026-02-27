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

#include <cstdint>

#include "Message.h"
#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;
class MessageConsumer;

    namespace amp {

class SignalOut : public Runnable2, public MessageConsumer {
public:

    SignalOut(Log& log, Clock& clock, MessageConsumer& bus, 
         Message::SignalType sigTypeOn, Message::SignalType sigTypeOff);

    int openHid(const char* hidName);

    void close();

    // ----- MessageConsumer --------------------------------------------------

    virtual void consume(const Message& frame);

private:

    Log& _log;
    Clock& _clock;
    MessageConsumer& _bus;
    Message::SignalType _sigTypeOn;
    Message::SignalType _sigTypeOff;
    
    int _hidFd = 0;
    bool _hidFailed = false;
    static const unsigned MAX_HID_SIZE = 16;
    uint8_t _hidAcc[MAX_HID_SIZE];
    unsigned _hidPacketSize = 4;

    unsigned _hidOffset = 1;
    uint8_t _hidMask = 0x04;
};

    }
}

   