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
#include <sys/select.h>
#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "StatsTask.h"

using namespace std;

namespace kc1fsz {

StatsTask::StatsTask(Log& log, Clock& clock) 
:   _log(log),
    _clock(clock),
    // Interval recommended by Jason N8EI on 20-Nov-2025
    _intervalMs(180 * 1000),
    _lastAttemptMs(0) { 

    _multiHandle = curl_multi_init();
    _curl = curl_easy_init();

    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, _writeCallback);
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, this);
    curl_multi_add_handle(_multiHandle, _curl);

    // cache the CA cert bundle in memory for a week 
    curl_easy_setopt(_curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);

    //_headers = curl_slist_append(_headers, "Content-Type: application/json");
    //curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, _headers);
}

StatsTask::~StatsTask() {
    curl_multi_remove_handle(_multiHandle, _curl);
    curl_slist_free_all(_headers);
    curl_easy_cleanup(_curl);
    curl_multi_cleanup(_multiHandle);
}

int StatsTask::getPolls(pollfd* fds, unsigned fdsCapacity) {

    // Pull out the set of FDs used by curl
    fd_set rdSet, wrSet, exSet;
    int maxFd = 0;
    curl_multi_fdset(_multiHandle, &rdSet, &wrSet, &exSet, &maxFd);

    // Now scan the old fd_sets and allocate some pollfds.
    unsigned pollfdUsed = 0;
    for (int fd = 0; fd < maxFd; fd++) {
        if (FD_ISSET(fd, &rdSet) || FD_ISSET(fd, &wrSet)) {
            if (fdsCapacity == 0)
                return -1;
            fds[pollfdUsed].fd = fd;
            fds[pollfdUsed].events = 0;
            if (FD_ISSET(fd, &rdSet))
                fds[pollfdUsed].events |= POLLIN;
            if (FD_ISSET(fd, &wrSet))
                fds[pollfdUsed].events |= POLLOUT;
            pollfdUsed++;
            fdsCapacity--;
        }
    }
    return pollfdUsed;
}

void StatsTask::configure(const char* url, const char* nodeNumber) {
    _url = url;
    _nodeNumber = nodeNumber;
}

void StatsTask::tenSecTick() {
    if (_clock.isPast(_lastAttemptMs + _intervalMs)) {
        _lastAttemptMs = _clock.time();
        _statsStart();
    }
}

bool StatsTask::run2() {  
    
    const State originalState = _state;

    if (_state == State::STATE_RUNNING) {
        int still_running = 1;
        CURLMcode mc = curl_multi_perform(_multiHandle, &still_running);
        if (mc != CURLM_OK) {
            _log.error("curl_multi_perform or curl_multi_wait failed: %s", 
                curl_multi_strerror(mc));
            _state = State::STATE_IDLE;
        }
        else if (still_running == 0) {
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
            _state = State::STATE_IDLE;
        }
    }

    return _state != originalState;
}

void StatsTask::_statsStart() {     

    // Todo: Get uptime working
    char msg[256];
    snprintf(msg, 256, 
        "%s?node=%s&time=%ld&seqno=%u&nodes=&apprptvers=0.0.0&apprptuptime=109&totalkerchunks=0&totalkeyups=0&totaltxtime=0&timeouts=0&totalexecdcommands=0&keyed=0&keytime=0",
        _url.c_str(),
        _nodeNumber.c_str(),
        time(0),
        _seqCounter++);

    _log.info("Stats URL: %s", msg);

    curl_easy_setopt(_curl, CURLOPT_URL, msg);    
    _resultAreaLen = 0;
    _state = State::STATE_RUNNING;
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
