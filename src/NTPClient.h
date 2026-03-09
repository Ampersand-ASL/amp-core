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

#include "Runnable2.h"
#include "TimerTask.h"

#define NTP_SAMPLE_COUNT (8)
#define NTP_SAMPLE_INTERVAL_S (6)

namespace kc1fsz {

class Log;
class Clock;

class NTPClient : public Runnable2 {
public:

    NTPClient(Log& log, Clock& clock);

    uint64_t getNowUs() const;

    void open();
    void close();

    // ------ Runnable2 --------------------------------------------------------

    void oneSecTick();
    bool run2();

private:

    Log& _log;
    Clock& _clock;

    struct NTPSample {
        double offset = 0;
        double delay = 0;
        // Infinity is 16.0s
        uint32_t dispersion = 16 << 16;
    };

    const unsigned _sampleCount = NTP_SAMPLE_COUNT;
    NTPSample _samples[NTP_SAMPLE_COUNT];

    TimerTask _timer2;
    TimerTask _timer3;

    int _sockFd = 0;
    bool _startup = true;
    bool _networkState = false;
    uint64_t _correctionOffsetUs = 0;
    const uint64_t _fixedOffsetUs = 2500;
    unsigned _newSampleCount = 0;
    uint64_t _lastResponseUs = 0;

    void _shift();
    void _ageDispersions(uint32_t ageShort);
};


}
