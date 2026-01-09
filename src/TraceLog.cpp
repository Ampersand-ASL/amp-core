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
#include <stdio.h>
#include <errno.h>

#include "kc1fsz-tools/Clock.h"
#include "TraceLog.h"

using namespace std;

namespace kc1fsz {

TraceLog::TraceLog(Clock& clock, std::string* data, unsigned dataLen) 
:   _clock(clock), _queue(data, dataLen) { }

void TraceLog::visitAll(std::function<void(const std::string& msg)> visitor) {
    _queue.visitAll(visitor);
}

void TraceLog::_out(const char* sev, const char* dt, const char* msg) {
    std::lock_guard<std::mutex> lock(_lock);
    char line[80];
    snprintf(line, 80, "%ld: %s", _clock.timeUs(), msg);
    _queue.push(string(line));
}

}
