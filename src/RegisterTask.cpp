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

#include "RegisterTask.h"

using namespace std;

namespace kc1fsz {

// The port: attribute is set to the IAX port. (20-Nov-2025) It was discovered that 
// setting this to the wrong value was causing a problem with the registration, even 
// though the registration server was still sending back a successful message.
// Thanks to Jason N8EI for help resolving this.
static const char *JSON_TEMPLATE = 
"{\"port\": %u,\"data\": {\"nodes\": {\"%s\": {\"node\": \"%s\",\"passwd\": \"%s\",\"remote\": 0}}}}";

static const char* AST_CURL_USER_AGENT = "asterisk-libcurl-agent/1.0";

RegisterTask::RegisterTask(Log& log, Clock& clock) 
:   _log(log),
    _clock(clock),
    // Interval recommended by Jason N8EI on 20-Nov-2025
    _regIntervalMs(180 * 1000),
    _nextRegisterMs(_clock.time() + 5 * 1000) { 
}

RegisterTask::~RegisterTask() {
}

void RegisterTask::configure(const char* regServerUrl, 
    const char* nodeNumber, const char* password, unsigned iaxPort) {
    _regServerUrl = regServerUrl;
    _nodeNumber = nodeNumber;
    _password = password;    
    _iaxPort = iaxPort;
}

void RegisterTask::doRegister() {     

    _curl = curl_easy_init();

    curl_easy_setopt(_curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(_curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, _writeCallback);
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, this);
    // Cache the CA cert bundle in memory for a week 
    curl_easy_setopt(_curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
    _headers = curl_slist_append(_headers, "Content-Type: application/json");
    curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, _headers);

    // Populate the JSON template with the necessary information.
    snprintf(_jsonData, JSON_DATA_SIZE, JSON_TEMPLATE, _iaxPort, 
        _nodeNumber.c_str(), _nodeNumber.c_str(), _password.c_str());
    //_log.info(_jsonData);

    curl_easy_setopt(_curl, CURLOPT_URL, _regServerUrl.c_str());
    curl_easy_setopt(_curl, CURLOPT_FORBID_REUSE, 1L); 

    // NOTE: Library doesn't make a copy of this data!! So we need to make 
    // sure it lasts beyond the scope of this function call.
    curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, _jsonData);
    curl_easy_setopt(_curl, CURLOPT_POSTFIELDSIZE, (long)strlen(_jsonData));

    curl_easy_setopt(_curl, CURLOPT_POST, 1L);
 	curl_easy_setopt(_curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(_curl, CURLOPT_TIMEOUT, 15L);
    
    _resultArea[0] = 0;
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
        char* r0 = strstr(_resultArea, "successfully registered");
        if (http_code == 200 && r0 != 0) {
            _log.info("Successfully registered %s", _nodeNumber.c_str());
            _lastGoodRegistrationMs = _clock.time();
        }
        else {
            _log.error("Registration failed for %s", _nodeNumber.c_str());
        }
    }

    curl_slist_free_all(_headers);
    curl_easy_cleanup(_curl);
    _headers = 0;
    _curl = 0;
}

void RegisterTask::tenSecTick() {
    if (_clock.time() >= _nextRegisterMs) {
        _nextRegisterMs = _clock.time() + _regIntervalMs;
        if (!_nodeNumber.empty())
            doRegister();
    }
}

// Callback function to handle received data
size_t RegisterTask::_writeCallback(void* contents, size_t size, 
    size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    ((RegisterTask*)userp)->_writeCallback2((const char*)contents, realsize);
    return realsize;
}

void RegisterTask::_writeCallback2(const char* contents, size_t size) {
    // Accumulate as much as we have room for
    for (unsigned i = 0; i < size && _resultAreaLen < RESULT_AREA_SIZE - 1; i++) {
        _resultArea[_resultAreaLen++] = contents[i];
    }
    _resultArea[_resultAreaLen++] = 0;
}

}
