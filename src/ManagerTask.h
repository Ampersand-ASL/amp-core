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
#include <utility>

#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;

class ManagerTask : public Runnable2 {
public:

    class CommandSink {
    public:
        virtual void execute(const char* cmd) = 0;
    };

    ManagerTask(Log&, Clock&, int listenPort);

    int setCommandSink(CommandSink* sink) {
        _sink = sink;
        return 0;
    }

    // ----- Runnable -------------------------------------------------

    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);
    virtual bool run2();
    void audioRateTick() { }

public:

    class Session {
    public: 

        Session() {
            reset();
        }

        void reset() {
            active = false;
            fd = 0;
            startMs = 0;
            inBufLen = 0;
        }

        /**
         * @returns True if a command was available.
         */
        bool popCommandIfPossible(char* cmdBuf, unsigned cmdBufCapacity);

        bool active = false;
        int fd = 0;
        uint32_t startMs = 0;
        static const unsigned BUF_CAPACITY = 128;
        char inBuf[BUF_CAPACITY];
        unsigned inBufLen = 0;
    };

private:

    void _send(const Session& session, const char* d, unsigned dLen);

    Log& _log;
    Clock& _clock;
    int _listenPort;
    int _fd;
    CommandSink* _sink;

    static const unsigned MAX_SESSIONS = 4;
    Session _sessions[MAX_SESSIONS];
};

using VisitFn = std::function<void(const char* name, const char* value)>;

int visitValues(const char* buf, unsigned bufLen, VisitFn fn);

}
