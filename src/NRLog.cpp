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
#include <unistd.h>
#include <sys/types.h>

#include <iostream>
#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "ThreadUtil.h"
#include "NRLog.h"

// The amount of time we sleep when no messages are pending
#define SLEEP_INTERVAL_MS (100)
#define SERVICE_URL ("https://log-api.newrelic.com/log/v1")
#define API_KEY_HEADER ("Api-Key")

using namespace std;
using json = nlohmann::json;

namespace kc1fsz {

NRLog::NRLog(const char* serviceName, const char* env, const char* apiKey) 
:   _serviceName(serviceName),
    _env(env),
    _apiKey(apiKey),
    _worker(_t, this) { 
}

void NRLog::stop() {
    _run = false;
    _worker.join();
}

void NRLog::_out(const char* sev, const char* dt, const char* msg) {
    char tid[16];
    pthread_getname_np(pthread_self(), tid, sizeof(tid));
    _lock.lock();
    std::cout << tid << " " << sev << ": " << dt << " " << msg << std::endl;
    _lock.unlock();
    if (!_apiKey.empty())
        _logQueue.push(pair<string,string>(string(sev), string(msg)));
}

void NRLog::_t2() {     

    amp::setThreadName("NRLog");
    amp::lowerThreadPriority();

    CURL* _curl = curl_easy_init();

    curl_easy_setopt(_curl, CURLOPT_NOSIGNAL, 1L);
    //curl_easy_setopt(_curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, _writeCallback);
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, this);
    // Cache the CA cert bundle in memory for a week 
    curl_easy_setopt(_curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
    curl_slist* _headers = curl_slist_append(0, "Content-Type: application/json");
    string keyHeader = string(API_KEY_HEADER) + string(": ") + _apiKey;
    _headers = curl_slist_append(_headers, keyHeader.c_str());
    curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, _headers);
    curl_easy_setopt(_curl, CURLOPT_URL, SERVICE_URL);
    curl_easy_setopt(_curl, CURLOPT_FORBID_REUSE, 1L); 

    curl_easy_setopt(_curl, CURLOPT_POST, 1L);
 	curl_easy_setopt(_curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(_curl, CURLOPT_TIMEOUT, 15L);

    // Main event loop

    while (_run || !_logQueue.empty()) {

        // Prevent 100% CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_INTERVAL_MS));

        // We clear everything from the queue whenever we have a chance.
        while (!_logQueue.empty()) {
            pair<string,string> item;
            if (_logQueue.try_pop(item)) {
                _sendMsg(_curl, item.first, item.second);
            }
        }
    }

    curl_slist_free_all(_headers);
    curl_easy_cleanup(_curl);
}

void NRLog::_sendMsg(CURL* curl, string& level, string& msg) {

    _resultArea[0] = 0;
    _resultAreaLen = 0;

    // New Relic specific message format here
    json o;
    o["message"] = msg;
    o["level"] = level;
    o["service"] = _serviceName;
    o["env"] = _env;
    string jsonData = o.dump();

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)jsonData.length());
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cout << "Logging failed " << res << endl;
    }
    else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        //printf("HTTP code %ld\n", http_code);
        //printf("GOT %s\n", _resultArea);
        if (http_code == 200 || http_code == 202) {
        }
        else {
            cout << "Log failed 2 " << http_code << endl;
        }
    }
}

// Callback function to handle received data
size_t NRLog::_writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    ((NRLog*)userp)->_writeCallback2((const char*)contents, realsize);
    return realsize;
}

void NRLog::_writeCallback2(const char* contents, size_t size) {
    // Accumulate as much as we have room for
    for (unsigned i = 0; i < size && _resultAreaLen < RESULT_AREA_SIZE - 1; i++) {
        _resultArea[_resultAreaLen++] = contents[i];
    }
    _resultArea[_resultAreaLen++] = 0;
}

}
