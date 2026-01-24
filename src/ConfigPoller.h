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

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "Runnable2.h"

using json = nlohmann::json;

namespace kc1fsz {   

class Log;

    namespace amp {

/**
 * Polls the configuration file and fires a callback any time the system 
 * configuration is changed. This should be the only thing that reads
 * the configuration file.
 */
class ConfigPoller : public Runnable2 {
public:

    static const char* DEFAULT_CONFIG;

    /**
     * @param configChangeCb Called every time the configuration document changes.
     * @param startupCb Called on first startup only, but after configChangeCb.
     */
    ConfigPoller(Log& log, const char* cfgFileName, 
        std::function<void(const json& cfg)> configChangeCb,
        std::function<void(const json& cfg)> startupCb = nullptr);

    // ----- Runnable2 --------------------------------------------------------

    bool run2() { return false; }
    void oneSecTick();

private: 

    void _populateDefaults(json& j);

    Log& _log;
    std::string _fn;
    std::function<void(const json& cfg)> _cb;
    std::function<void(const json& cfg)> _startupCb;
    // Initialize at the epoch
    std::filesystem::file_time_type _lastUpdate;
    bool _startup = true;
    bool _configLoadErrorDisplayed = false;
};

    }
}
