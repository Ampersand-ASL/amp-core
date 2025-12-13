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
#include "kc1fsz-tools/linux/StdClock.h"

#include "Runnable2.h"
#include "EventLoop.h"

namespace kc1fsz {

void EventLoop::run(Log& log, Clock& clock, 
    Runnable** tasks1, unsigned task1Count,
    Runnable2** tasks, unsigned taskCount,
    std::function<bool(Log& log, Clock& clock)> cb, bool trace) {

    StdPollTimer timer20ms(clock, 20000);
    StdPollTimer timer1s(clock, 1000000);
    StdPollTimer timer10s(clock, 10000000);

    timer20ms.reset();
    timer1s.reset();
    timer10s.reset();

    unsigned long slowestLoopUs = 0;
    unsigned long totalPollUs = 0;
    unsigned long totalWorkUs = 0;
    unsigned long loopCount = 0;   
    unsigned long maxLateUs = 0;

    while (true) {        

        uint64_t pollStartUs = clock.timeUs();

        // Gather the FDs that we care about
        const unsigned fdsCapacity = 32;
        unsigned fdsSize = 0;
        pollfd fds[fdsCapacity];

        for (unsigned i = 0; i < taskCount; i++) {
            int used = tasks[i]->getPolls(fds + fdsSize, fdsCapacity - fdsSize);
            if (used < 0) {
                log.error("Not enough poll fds");
                break;
            }
            fdsSize += used;
        }

        // Figure out how long we can sleep without missing the 
        // end of the current 20ms interval. Good audio quality depends
        // on us servicing the 20ms promptly every time.
        uint32_t msLeft = timer20ms.usLeftInInterval();
        msLeft /= 1000;
    
        uint32_t sleepMs = msLeft;
        // But never less than 1ms
        sleepMs = std::max(sleepMs, (uint32_t)2);

        // Block waiting for activity or timeout
        int rc = 0;
#ifdef _WIN32
        if (fdsSize > 0)
            rc = WSAPoll(fds, fdsSize, sleepMs);
        else 
            // WARNING: It isn't really possible to sleep less than 15-20ms
            // on Windows, so don't expect a very short sleep here.
            Sleep(sleepMs);
#else        
        rc = poll(fds, fdsSize, sleepMs);
#endif
        if (rc < 0) {
            log.error("Poll error");
        } 

        uint64_t pollEndUs = clock.timeUs();
        unsigned long pollTimeUs = pollEndUs - pollStartUs;
        totalPollUs += pollTimeUs;

        unsigned long workStartUs = clock.timeUs();

        for (unsigned i = 0; i < task1Count; i++)
            tasks1[i]->run();

        // This timer has highest priority since the audio tick
        // rate is time-critical
        if (timer20ms.poll()) {
            if (timer20ms.getLateUs() > maxLateUs)
                maxLateUs = timer20ms.getLateUs();
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->audioRateTick();
            
        }  

        for (unsigned i = 0; i < taskCount; i++)
            tasks[i]->run2();             

        if (timer1s.poll()) {
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->oneSecTick();
        }

        bool showStats = false;

        if (timer10s.poll()) {
            for (unsigned i = 0; i < taskCount; i++)
                tasks[i]->tenSecTick();
            showStats = true;
        }

        if (cb) {
            if (cb(log, clock) == false)
                break;
        }

        unsigned long workEndUs = clock.timeUs();
        unsigned long workTimeUs = workEndUs - workStartUs;
        totalWorkUs += workTimeUs;
        
        if (workTimeUs > slowestLoopUs) {
            slowestLoopUs = workTimeUs;
            //log.info("Slowest loop %lu us", slowestLoopUs);
        }

        if (trace && showStats) {
            // Internal stats
            log.info("AvgPoll: %6lu, AvgWork: %6lu, MaxWork: %6lu, MaxLate: %6lu", 
                totalPollUs / loopCount,
                totalWorkUs / loopCount, 
                slowestLoopUs,
                maxLateUs);

            totalPollUs = 0;
            totalWorkUs = 0;
            maxLateUs = 0;
            slowestLoopUs = 0;
            loopCount = 0;            
        }


        loopCount++;
    }
}

}
