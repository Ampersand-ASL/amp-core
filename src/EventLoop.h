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

#include <functional>

namespace kc1fsz {

class Log;
class Clock;
class Runnable;
class Runnable2;

class EventLoop {
public:

    /**
     * @param cb (Optional) Called on every cycle. If false is
     * returned then the loop exits.
     */
    static void run(Log& log, Clock& lock, 
        Runnable** tasks, unsigned tasksLen,
        Runnable2** tasks2, unsigned tasks2Len,
        std::function<bool(Log& log, Clock& clock)> cb = nullptr,
        bool trace = false);    
};

}
