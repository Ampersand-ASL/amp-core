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
#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"
#include "kc1fsz-tools/raiiholder.h"

#include "RegisterTask.h"

#ifdef _WIN32
// The xxd-generated resource which was downloaded from the Curl website
// at build time.
#include "cacerts.h"
#endif

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

void RegisterTask::configure(const char* regServerUrl, 
    const char* nodeNumber, const char* password, unsigned iaxPort) {
    _regServerUrl = regServerUrl;
    _nodeNumber = nodeNumber;
    _password = password;    
    _iaxPort = iaxPort;

    _log.info("RegisterTask %s, %s, %d", _regServerUrl.c_str(), _nodeNumber.c_str(), _iaxPort);
}

void RegisterTask::_doRegister() {     

    CURL* curl = curl_easy_init();
    // Responsible for freeing resource on exit
    raiiholder<CURL> curlHolder(curl, [](CURL* c) { curl_easy_cleanup(c); });

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);

#ifdef _WIN32    
    // On Windows we statically link the Certificate Authority root
    // keys to avoid deployment hassles.
    // See: https://curl.se/libcurl/c/CURLOPT_CAINFO_BLOB.html
    struct curl_blob blob;
    blob.data = curl_cacerts;
    blob.len = strlen((const char*)curl_cacerts);
    blob.flags = CURL_BLOB_COPY;
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
#endif

    // Cache the CA cert bundle in memory for a week 
    curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);

    curl_slist* headers = 0;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    // Responsible for freeing resource on exit
    raiiholder<curl_slist> headerHolder(headers, 
        [](curl_slist* h) { curl_slist_free_all(h); });

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Populate the JSON template with the necessary information.
    snprintf(_jsonData, JSON_DATA_SIZE, JSON_TEMPLATE, _iaxPort, 
        _nodeNumber.c_str(), _nodeNumber.c_str(), _password.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, _regServerUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L); 

    // NOTE: Library doesn't make a copy of this data!! So we need to make 
    // sure it lasts beyond the scope of this function call.
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, _jsonData);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(_jsonData));

    curl_easy_setopt(curl, CURLOPT_POST, 1L);
 	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    
    _resultArea[0] = 0;
    _resultAreaLen = 0;

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        _log.error("ASL node registration failed (1) for %s %d", 
            _nodeNumber.c_str(), res);
    }
    else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        char* r0 = strstr(_resultArea, "successfully registered");
        if (http_code == 200 && r0 != 0) {
            _lastGoodRegistrationMs = _clock.time();
        } else {
            _log.error("ASL node registration failed for %s", _nodeNumber.c_str());
            _log.error("  HTTP code: %d", http_code);
            _log.error("  Response body: %s", _resultArea);
        }
    }
}

void RegisterTask::tenSecTick() {
    if (_clock.time() >= _nextRegisterMs) {
        _nextRegisterMs = _clock.time() + _regIntervalMs;
        if (!_regServerUrl.empty() && !_nodeNumber.empty() && !_password.empty())
            _doRegister();
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
