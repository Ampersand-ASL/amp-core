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

    StatsTask(Log& log, Clock& clock);
    ~StatsTask();

    void configure(const char* statsServerUrl, const char* nodeNumber);

    // ----- Runnable -----------------------------------------------------------

    int getPolls(pollfd* fds, unsigned fdsCapacity);
    bool run2();
    void audioRateTick() { }
    void tenSecTick();

private:

    static size_t _writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    void _writeCallback2(const char* contents, size_t size);

    void _statsStart();

    enum State {
        STATE_IDLE,
        STATE_RUNNING,
    };

    Log& _log;
    Clock& _clock;
    fixedstring _url;
    fixedstring _nodeNumber;

    // Here is were we accumulate the received data
    static const unsigned RESULT_AREA_SIZE = 128;
    unsigned _resultAreaLen = 0;
    char _resultArea[RESULT_AREA_SIZE];

    State _state = State::STATE_IDLE;
    uint32_t _intervalMs;
    uint32_t _lastAttemptMs = 0;
    uint32_t _lastSuccessMs = 0;

    CURLM* _multiHandle = 0;
    CURL* _curl = 0;
    curl_slist* _headers = 0;
    unsigned _seqCounter = 1;
};

}
