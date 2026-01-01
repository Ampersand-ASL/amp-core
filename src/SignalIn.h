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

#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;
class MessageConsumer;

    namespace amp {

class SignalIn : public Runnable2, public MessageConsumer {
public:

    SignalIn(Log& log, Clock& clock, MessageConsumer& bus);

    int openHid(const char* hidName);

    void close();

    // ----- MessageConsumer --------------------------------------------------

    virtual void consume(const Message& frame);

    // ----- Runnable ---------------------------------------------------------

    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);
    virtual bool run2();
    virtual void audioRateTick(uint32_t tickMs);

private:

    void _pollHidStatus();

    Log& _log;
    Clock& _clock;
    MessageConsumer& _bus;

    int _hidFd = 0;
};

    }
}

   