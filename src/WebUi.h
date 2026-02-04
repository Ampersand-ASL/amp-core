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

    namespace amp {

/**
 * A web server for a simple UI.  
 */
class WebUi : public Runnable2, public MessageConsumer {
public:

    WebUi(Log& log, Clock& clock, MessageConsumer& cons, unsigned listenPort,
        unsigned networkDestLineId, unsigned radioDestLineId,
        const char* configFileName, const char* version, TraceLog& traceLog);

    void setConfig(const json& j) { _config.set(j); }
    void setBridgeStatus(const json& j) { _status.set(j); }
    void setBridgeLevels(const json& j) { _levels.set(j); }

    /**
     * Start this on a background thread.
     */
    static void uiThread(WebUi* ui, MessageConsumer* bus);

    // ----- MessageConsumer --------------------------------------------

    void consume(const Message& msg);

private:

    Log& _log;
    Clock& _clock;
    MessageConsumer& _consumer;
    const unsigned _listenPort;
    const unsigned _networkDestLineId;
    const unsigned _radioDestLineId;
    const std::string _configFileName;
    const std::string _version;
    TraceLog& _traceLog;

    std::atomic<bool> _ptt;

    copyableatomic<json> _config;
    copyableatomic<json> _status;
    copyableatomic<json> _levels;
};

    }
}