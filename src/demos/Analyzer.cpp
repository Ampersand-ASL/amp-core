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
#include <sys/time.h>
#include "kc1fsz-tools/Log.h"
#include "Analyzer.h"

namespace kc1fsz {

Analyzer::Analyzer(Log& log, Clock& clock, MessageConsumer& consumer, unsigned target)
:   _log(log),
    _clock(clock),
    _bus(consumer),
    _target(target) {

}

void Analyzer::consume(const Message& frame) {
    timeval tv0;
    gettimeofday(&tv0, NULL); 
    long loopEndUs = (long)tv0.tv_sec * 1000000 + tv0.tv_usec;
    _log.info("Received audio block %ld", loopEndUs);
}

void Analyzer::fastTick() {
}

void Analyzer::audioRateTick() {
}

void Analyzer::oneSecTick() {
}

}
