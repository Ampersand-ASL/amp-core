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

#include <functional>

#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;

/**
 * A task that fires a callback on every iteration
 */
class AlwaysTask : public Runnable2 {
public:

    AlwaysTask(Log& log, std::function<void()> cb) 
    :   _log(log), _cb(cb) { }

    // ----- Runnable -----------------------------------------------------------

    bool run2() { 
        _cb();
        return false; 
    }

private:

    Log& _log;
    std::function<void()> _cb;
};

}
