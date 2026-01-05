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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "sound-map.h"

#include "ConfigPoller.h"

#define CMEDIA_VENDOR_ID ("0d8c")

using namespace std;
using json = nlohmann::json;

namespace kc1fsz {
    namespace amp {

const char* ConfigPoller::DEFAULT_CONFIG = "{ \"setupMode\":\"0\", \"aslHdwType\":\"0\", \"aslAudioDevice\":\"\", \"aslCosFrom\":\"usbinvert\", \"aslCtcssFrom\":\"none\", \"aslInvertPtt\":\"no\", \"aslTxMixASet\":\"500\", \"aslTxMixBSet\":\"500\", \"aslRxMixerSet\":\"500\",\"aslDnsRoot\":\"nodes.allstarlink.org\",\"aslRegUrl\":\"https://register.allstarlink.org\", \"aslStatUrl\":\"http://stats.allstarlink.org/uhandler\", \"call\":\"\", \"iaxPort\":\"4569\", \"lastUpdateMs\":0, \"node\":\"\", \"password\":\"\", \"privateKey\":\"\" }";

ConfigPoller::ConfigPoller(Log& log, const char* cfgFileName, std::function<void(const json& cfg)> cb) 
:   _log(log),
    _fn(cfgFileName),
    _cb(cb) { 
}

void ConfigPoller::oneSecTick() {   
    try {
        auto ftime = std::filesystem::last_write_time(_fn);
        if (_startup || ftime > _lastUpdate) {
            _lastUpdate = ftime;
            _startup = false;
            ifstream cfg(_fn);
            std::stringstream buffer;
            buffer << cfg.rdbuf();
            cfg.close();
            try {
                json j = json::parse(buffer.str());
                _populateDefaults(j);
                // Fire the callback
                _cb(j);

            } catch(json::exception& ex) {
                _log.error("Invalid config file format %s", ex.what());
            }
        }
    } catch (exception& ex) {
        _log.error("Unable to load config file %s %s", _fn.c_str(), ex.what());
    }
}

void ConfigPoller::_populateDefaults(json& j) {

    // If there is no USB sound device selected, default to the first C-Media 
    // device we can find.
    if (j["aslAudioDevice"].get<std::string>().empty()) {
        visitUSBDevices2([&j](
            const char*, const char*, 
            const char* vendorId, const char*,                 
            const char* busId, const char* portId) {
                if (strcasecmp(vendorId, CMEDIA_VENDOR_ID) == 0) {
                    string val("usb ");
                    val += "bus:";
                    val += busId;
                    val += ",";
                    val += "port:";
                    val += portId;
                    j["aslAudioDevice"] = val;
                }
            });
    }
}

    }
}
