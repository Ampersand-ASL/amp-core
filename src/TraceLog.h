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

#include <iostream>
#include <mutex>
#include <string>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/circularqueue.h"

namespace kc1fsz {

class Clock;

class TraceLog : public Log {
public:

    TraceLog(Clock& clock, std::string* data, unsigned dataLen);
    
    void visitAll(std::function<void(const std::string& msg)> visitor);

protected:

    void _out(const char* sev, const char* dt, const char* msg);

private:

    Clock& _clock;
    std::mutex _lock;
    circularqueue<std::string> _queue;
};
}
