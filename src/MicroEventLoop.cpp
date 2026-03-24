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
#include <algorithm>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/StdPollTimer.h"

#include "Runnable2.h"

#include "MicroEventLoop.h"

namespace kc1fsz {

void MicroEventLoop::run(Log& log, Clock& clock, 
    Runnable** tasks1, unsigned task1Count,
    Runnable2** tasks, unsigned taskCount,
    std::function<bool(Log& log, Clock& clock)> cb, 
    void (*cb_top)(),
    void (*cb_bottom)(),
    bool trace) {

    StdPollTimer timer20ms(clock, 20000);
    StdPollTimer timer250ms(clock, 250000);
    StdPollTimer timer1s(clock, 1000000);
    StdPollTimer timer10s(clock, 10000000);

    timer20ms.reset();
    timer250ms.reset();
    timer1s.reset();
    timer10s.reset();

    uint64_t slowestLoopUs = 0;
    uint64_t totalWorkUs = 0;
    unsigned long loopCount = 0;   
    unsigned long maxLateUs = 0;

    while (true) {        

        uint64_t workStartUs = clock.timeUs();

        if (cb_top)
            cb_top();

        for (unsigned i = 0; i < task1Count; i++)
            tasks1[i]->run();

        // This timer has highest priority since the audio tick rate is time-critical
        uint64_t intervalUs = 0;
        if (timer20ms.poll(&intervalUs)) {
            if (timer20ms.getLateUs() > maxLateUs)
                maxLateUs = timer20ms.getLateUs();
            uint64_t intervalMs = intervalUs / 1000;
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->audioRateTick(intervalMs);
        }  

        for (unsigned i = 0; i < taskCount; i++)
            tasks[i]->run2();             

        if (timer250ms.poll()) {
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->quarterSecTick();
        }

        if (timer1s.poll()) {
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->oneSecTick();
        }

        bool showStats = false;

        if (timer10s.poll()) {
            showStats = true;
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->tenSecTick();
        }

        if (cb) {
            if (cb(log, clock) == false)
                break;
        }

        if (cb_bottom)
            cb_bottom();

        uint64_t workEndUs = clock.timeUs();
        uint64_t workTimeUs = workEndUs - workStartUs;
        totalWorkUs += workTimeUs;
        
        if (workTimeUs > slowestLoopUs) {
            slowestLoopUs = workTimeUs;
            //log.info("Slowest loop %lu us", (unsigned long)slowestLoopUs);
        }

        loopCount++;

        if (trace && showStats) {
            // Internal stats
            log.info("AvgWork: %6lu, MaxWork: %6lu, MaxLate: %6lu", 
                totalWorkUs / loopCount, 
                (unsigned)slowestLoopUs,
                maxLateUs);

            totalWorkUs = 0;
            maxLateUs = 0;
            slowestLoopUs = 0;
            loopCount = 0;            
        }
    }
}

}
