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
#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "StatsTask.h"

using namespace std;

namespace kc1fsz {

static const char* AST_CURL_USER_AGENT = "asterisk-libcurl-agent/1.0";

StatsTask::StatsTask(Log& log, Clock& clock, const char* version) 
:   _log(log),
    _clock(clock),
    _version(version),
    _startTime(time(0)),
    // Interval recommended by Jason N8EI on 20-Nov-2025
    _intervalMs(180 * 1000),
    _lastAttemptMs(0) { 
}

void StatsTask::configure(const char* url, const char* nodeNumber) {
    _url = url;
    _nodeNumber = nodeNumber;
}

void StatsTask::tenSecTick() {
    if (_clock.isPast(_lastAttemptMs + _intervalMs)) {
        _lastAttemptMs = _clock.time();
        _doStats();
    }
}

void StatsTask::_doStats() {  
    
    // Todo: Get uptime working
    char msg[256];
    snprintf(msg, 256, 
        "%s?node=%s&time=%lld&seqno=%u&nodes=&apprptvers=%s&apprptuptime=%lld&totalkerchunks=0&totalkeyups=0&totaltxtime=0&timeouts=0&totalexecdcommands=0&keyed=0&keytime=0",
        _url.c_str(),
        _nodeNumber.c_str(),
        time(0),
        _seqCounter++,
        _version.c_str(),
        time(0) - _startTime);

    _log.info("Stats URL: %s", msg);
    
    _curl = curl_easy_init();

    curl_easy_setopt(_curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(_curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, _writeCallback);
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(_curl, CURLOPT_URL, msg);    
    // cache the CA cert bundle in memory for a week 
    curl_easy_setopt(_curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);

    _resultAreaLen = 0;
  
    CURLcode res = curl_easy_perform(_curl);
    if (res != CURLE_OK) {
        _log.error("Registration failed (1) for %s", _nodeNumber.c_str());
    }
    else {
        long http_code = 0;
        curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &http_code);
        //printf("HTTP code %ld\n", http_code);
        //printf("GOT %s\n", _resultArea);
        char* r0 = strstr(_resultArea, "ok");
        if (http_code == 200 && r0 != 0) {
            _log.info("Stats success");
            _lastSuccessMs = _clock.time();
        }
        else {
            _log.info("Stats failed");
        }
    }

    curl_easy_cleanup(_curl);
    _curl = 0;
}

// Callback function to handle received data
size_t StatsTask::_writeCallback(void* contents, size_t size, 
    size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    ((StatsTask*)userp)->_writeCallback2((const char*)contents, realsize);
    return realsize;
}

void StatsTask::_writeCallback2(const char* contents, size_t size) {
    // Accumulate as much as we have room for
    for (unsigned i = 0; i < size && _resultAreaLen < RESULT_AREA_SIZE - 1; i++) {
        _resultArea[_resultAreaLen++] = contents[i];
    }
    _resultArea[_resultAreaLen++] = 0;
}

}
