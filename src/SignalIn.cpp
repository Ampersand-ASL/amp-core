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
#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "MessageConsumer.h"
#include "SignalIn.h"

using namespace std;

namespace kc1fsz {
    namespace amp {

SignalIn::SignalIn(Log& log, Clock& clock, MessageConsumer& bus) 
:   _log(log), _clock(clock), _bus(bus) {
}

int SignalIn::openHid(const char* hidName) {
    return 0;
}

void SignalIn::close() {
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
}

    }
}
