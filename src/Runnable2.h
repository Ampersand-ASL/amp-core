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

#include "kc1fsz-tools/Runnable.h"

struct pollfd;

namespace kc1fsz {

class Runnable2 : public Runnable {
public:

    /**
     * Fulls up the poll list with any file desriptors that need to be
     * monitored for aysnchronous activity. 
     * 
     * @returns The number of pollfds consumed, or -1 if there is not 
     * enough capacity.
     */
    virtual int getPolls(pollfd* fds, unsigned fdsCapacity) { return 0; }

    virtual void run() { run2(); }

    /**
     * This is called whenever a poll event is detected, or when a 
     * previous call to run2() indicates that more work might be 
     * pending. An event loop will keep calling this function until
     * it returns false.
     * 
     * @returns If any work might still be pending
     */
    virtual bool run2() { return false; }

    /**
     * Called at the audio interval (usually every 20ms), as precisely
     * as possible. Although the tick may happen slightly ahead or behind 
     * in "real time," this function will be called for every 20m tick of 
     * the system clock (i.e. none will be skipped entirely).
     * 
     * @param tickTimeMs Returns the official beginning time for which this audio
     * tick applies. This time will likely be before the "clock time" since it 
     * is locked in at the start of the tick.
     */
    virtual void audioRateTick(uint32_t tickTimeMs) { }

    virtual void oneSecTick() { }
    
    virtual void tenSecTick() { }
};

}
