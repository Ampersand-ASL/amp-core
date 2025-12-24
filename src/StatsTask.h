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

#include <curl/curl.h>

#include "kc1fsz-tools/fixedstring.h"

#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;

class StatsTask : public Runnable2 {
public:

    StatsTask(Log& log, Clock& clock, const char* version);

    /**
     * NOTE: This function can be called at any time. The updates will take effect
     * in the next polling cycle.
     * 
     * @param statsServerUrl The URL of the ASL stats server.
     */
    void configure(const char* statsServerUrl, const char* nodeNumber);

    // ----- Runnable -----------------------------------------------------------

    bool run2() { return false; }

    void tenSecTick();

private:

    void _doStats();

    static size_t _writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    void _writeCallback2(const char* contents, size_t size);

    Log& _log;
    Clock& _clock;
    fixedstring _version;
    const time_t _startTime;
    fixedstring _url;
    fixedstring _nodeNumber;

    // Here is were we accumulate the received data
    static const unsigned RESULT_AREA_SIZE = 128;
    unsigned _resultAreaLen = 0;
    char _resultArea[RESULT_AREA_SIZE];

    uint32_t _intervalMs;
    uint32_t _lastAttemptMs = 0;
    uint32_t _lastSuccessMs = 0;

    CURL* _curl = 0;
    unsigned _seqCounter = 1;
};

}
