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
#include <thread>
#include <string>
#include <mutex>
#include <utility>

#include <curl/curl.h>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/threadsafequeue.h"

namespace kc1fsz {

/**
 * New-relic logger. The transfer to NR happens on a background thread so there 
 * shouldn't be any concern about overhead. 
 *
 * This logger is thread-safe.
 */
class NRLog : public Log {
public:

    NRLog(const char* serviceName, const char* env, const char* apiKey);
    void stop();

protected:

    virtual void _out(const char* sev, const char* dt, const char* msg);

private:

    static void _t(NRLog* o) { o->_t2(); }
    void _t2();
    
    void _sendMsg(CURL* curl, std::string& level, std::string& msg);

    static size_t _writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    void _writeCallback2(const char* contents, size_t size);

    std::string _serviceName;
    std::string _env;
    std::string _apiKey;

    std::mutex _lock;
    bool _run = true;
    threadsafequeue<std::pair<std::string, std::string> > _logQueue;
    std::thread _worker;
    // Here is were we accumulate the received data
    static const unsigned RESULT_AREA_SIZE = 128;
    unsigned _resultAreaLen = 0;
    char _resultArea[RESULT_AREA_SIZE];
};

}
