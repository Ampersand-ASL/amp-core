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

/**
 * A module that can generate test audio signals and inject them into 
 * the Ampersand data bus just like what would happen in the real system.
 */
class TestAudioGenerator : public Runnable2 {
public:

    TestAudioGenerator(Log& log, Clock& clock, MessageConsumer& bus, unsigned destLineId);

    // ----- Runnable2 ----------------------------------------------------------

    void audioRateTick(uint32_t tickMs);

private:

    Log& _log;
    Clock& _clock;
    MessageConsumer& _bus;
    const unsigned _destLineId;

    const unsigned _audioRate = 48000;
    float _testToneHz;
    float _omega;
    float _phi;
    float _amp;
    unsigned int _stateCounter = 0;
};

}