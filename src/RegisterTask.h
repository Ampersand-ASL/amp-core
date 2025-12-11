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

class RegisterTask : public Runnable2 {
public:

    RegisterTask(Log& log, Clock& clock);
    ~RegisterTask();

    void configure(const char* regServerUrl, const char* nodeNumber, const char* password, 
        unsigned iaxPort);

    void doRegister();

    // ----- Runnable -------

    int getPolls(pollfd* fds, unsigned fdsCapacity) { return 0; }
    bool run2() { return false; }
    void audioRateTick() { }
    void tenSecTick();

private:

    static size_t _writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    void _writeCallback2(const char* contents, size_t size);

    Log& _log;
    Clock& _clock;
    fixedstring _regServerUrl;
    fixedstring _nodeNumber;
    fixedstring _password;
    unsigned _iaxPort;

    // Here is were we accumulate the received data
    static const unsigned RESULT_AREA_SIZE = 128;
    unsigned _resultAreaLen = 0;
    char _resultArea[RESULT_AREA_SIZE];

    uint32_t _regIntervalMs;
    uint32_t _nextRegisterMs = 0;
    uint32_t _lastGoodRegistrationMs = 0;

    CURL* _curl = 0;
    curl_slist* _headers = 0;

    static const unsigned JSON_DATA_SIZE = 128;
    char _jsonData[JSON_DATA_SIZE];
};

}
