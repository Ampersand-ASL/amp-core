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

#include <atomic>
#include <string>
#include <vector>
#include <utility>

#include <nlohmann/json.hpp>

#include "kc1fsz-tools/threadsafequeue.h"
#include "kc1fsz-tools/copyableatomic.h"

#include "Runnable2.h"
#include "MessageConsumer.h"

using json = nlohmann::json;

namespace kc1fsz {

class Log;
class Clock;
class MessageConsumer; 
class TraceLog;
class CircularBuffer2Locked;

    namespace amp {

/**
 * A web server for a simple UI.  
 */
class WebUi : public Runnable2, public MessageConsumer {
public:

    WebUi(Log& log, Clock& clock, unsigned listenPort,
        unsigned networkDestLineId, unsigned radioDestLineId,
        const char* configFileName, const char* version, TraceLog& traceLog);
        
    void setConfig(const json& j) { _config.set(j); }
    void setBridgeStatus(const json& j) { _status.set(j); }
    void setBridgeLevels(const json& j) { _levels.set(j); }
    void setUiPWd(const std::string& uipwd) { _uiPwd = uipwd; }

    /**
     * A utility that takes a comma-separated list and makes a vector,
     * including support for quoted strings. Also removes leading/trailing
     * whitespace on tokens. So, for example:
     * 
     * a:1, b:"2,3"
     *
     * Result in two pairs:
     * 
     *  first=a second=1
     *  first=b second=2,3
     */
    static std::vector<std::pair<std::string, std::string>> parseFavorites(const std::string& fav);

    /**
     * Start this on a background thread since the HTTP server listen call 
     * is blocking.
     * 
     * @param logBuffer The source of console log data for display on the Log tab.
     */
    static void uiThread(WebUi* ui, MessageConsumer* bus, CircularBuffer2Locked* logBuffer);

    // ----- MessageConsumer --------------------------------------------

    void consume(const Message& msg);

private:

    bool _checkAuthorization(const std::string& auth) const;

    Log& _log;
    Clock& _clock;
    const unsigned _listenPort;
    const unsigned _networkDestLineId;
    const unsigned _radioDestLineId;
    const std::string _configFileName;
    const std::string _version;
    TraceLog& _traceLog;

    std::string _uiPwd;
    
    std::atomic<bool> _ptt;

    copyableatomic<json> _config;
    copyableatomic<json> _status;
    copyableatomic<json> _levels;
};

/**
 * An instance of this structure is put into the log buffer for each line logged.
 */
struct LogEntry {
    unsigned seq;
    char text[80];
};

    }
}
