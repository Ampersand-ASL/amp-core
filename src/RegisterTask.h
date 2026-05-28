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

#include <string>
#include <curl/curl.h>

#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;

class RegisterTask : public Runnable2 {
public:

    RegisterTask(Log& log, Clock& clock);

    /**
     * NOTE: This function can be called at any time. The updates will take effect
     * in the next polling cycle.
     * 
     * @param regServerUrl The URL of the ASL registration server.
     */
    void configure(const char* regServerUrl, const char* nodeNumber, const char* password, 
        unsigned iaxPort);

    // ----- Runnable -------

    void tenSecTick();

private:

    void _doRegister();

    static size_t _writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    void _writeCallback2(const char* contents, size_t size);

    Log& _log;
    Clock& _clock;

    std::string _regServerUrl;
    std::string _nodeNumber;
    std::string _password;
    unsigned _iaxPort;

    // Here is were we accumulate the received data
    static const unsigned RESULT_AREA_SIZE = 128;
    unsigned _resultAreaLen = 0;
    char _resultArea[RESULT_AREA_SIZE];

    uint32_t _regIntervalMs;
    uint64_t _lastGoodRegistrationMs = 0;

    static const unsigned JSON_DATA_SIZE = 512;
    char _jsonData[JSON_DATA_SIZE];
};

}
