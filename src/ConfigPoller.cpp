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

#ifndef _WIN32
#include "sound-map.h"
#endif

#include "ConfigPoller.h"

#define CMEDIA_VENDOR_ID ("0d8c")

using namespace std;
using json = nlohmann::json;

namespace kc1fsz {
    namespace amp {

ConfigPoller::ConfigPoller(Log& log, const char* cfgFileName, 
    std::function<void(const json& cfg)> cb,
    std::function<void(const json& cfg)> startupCb) 
:   _log(log),
    _fn(cfgFileName),
    _cb(cb),
    _startupCb(startupCb) { 
}

void ConfigPoller::oneSecTick() {   

    try {
        // Get the last update time. This also has the side-effect of 
        // validating the location of the config file.
        auto ftime = std::filesystem::last_write_time(_fn);
        _configLoadErrorDisplayed = false;

        if (_startup || ftime > _lastUpdate) {
            _lastUpdate = ftime;
            try {
                json j = json::parse(ifstream(_fn));
                _populateDefaults(j);
                // Fire the callbacks
                _cb(j);
                if (_startup && _startupCb)
                    _startupCb(j);
            } catch(json::exception& ex) {
                _log.error("Config file format error %s", ex.what());
            } catch (exception& ex) {
                _log.error("Unable to load config: %s", ex.what());
            }
            _startup = false;
        }

    } catch (exception& ex) {
        if (!_configLoadErrorDisplayed) {
            _log.error("Unable to load config %s", ex.what());
            _configLoadErrorDisplayed = true;
        }
    }
}

static void setDefault(json& j, const char* name, const char* value) {
    if (!j.contains(name))
        j[name] = value;
}

void ConfigPoller::_populateDefaults(json& j) {

    setDefault(j, "node", "");
    setDefault(j, "password", "");
    setDefault(j, "privateKey", "");
    setDefault(j, "callsign", "");
    setDefault(j, "iaxPort", "4569");
    setDefault(j, "setupMode", "0");
    setDefault(j, "aslTxMixASet", "");
    setDefault(j, "aslTxMixBSet", "");
    setDefault(j, "aslRxMixerSet", "");
    setDefault(j, "aslStatUrl", "http://stats.allstarlink.org/uhandler.php");
    setDefault(j, "aslRegUrl", "https://register.allstarlink.org");
    setDefault(j, "aslDnsRoot", "nodes.allstarlink.org");

    // If there is no USB sound device selected, default to the first C-Media 
    // device we can find.
    if (!j.contains("aslAudioDevice") || j["aslAudioDevice"].get<std::string>().empty()) {
#ifndef _WIN32        
        visitUSBDevices2([&j](
            const char*, const char*, 
            const char* vendorId, const char*, const char* portPath, int, int) {
                if (strcasecmp(vendorId, CMEDIA_VENDOR_ID) == 0) {
                    string val("usbaud ");
                    val += portPath;
                    j["aslAudioDevice"] = val;
                }
            });
#endif            
    }

    if (!j.contains("aslCosDevice") || j["aslCosDevice"].get<std::string>().empty()) {
#ifndef _WIN32        
        visitUSBDevices2([&j](
            const char*, const char*, 
            const char* vendorId, const char*, const char* portPath, int, int) {
                if (strcasecmp(vendorId, CMEDIA_VENDOR_ID) == 0) {
                    string val("usbaud ");
                    val += portPath;
                    j["aslCosDevice"] = val;
                }
            });
#endif            
    }
    setDefault(j, "aslCosSignal", "default");
    setDefault(j, "aslCosInvert", "0");

    setDefault(j, "aslCtcssDevice", "");
    setDefault(j, "aslCtcssSignal", "");
    setDefault(j, "aslCtcssInvert", "0");

    if (!j.contains("aslPttDevice") || j["aslPttDevice"].get<std::string>().empty()) {
#ifndef _WIN32        
        visitUSBDevices2([&j](
            const char*, const char*, 
            const char* vendorId, const char*, const char* portPath, int, int) {
                if (strcasecmp(vendorId, CMEDIA_VENDOR_ID) == 0) {
                    string val("usbaud ");
                    val += portPath;
                    j["aslPttDevice"] = val;
                }
            });
#endif            
    }
    setDefault(j, "aslPttSignal", "default");
    setDefault(j, "aslPttInvert", "0");

    if (!j.contains("favorites"))
        j["favorites"] = "2002:ASL Parrot";
    setDefault(j, "kfnodes", "");
    if (!j.contains("kfdelay"))
        j["kfdelay"] = "2000";
    if (!j.contains("callsign"))
        j["callsign"] = "";
    if (j.contains("aslStatUrl")) {
        // Correct a mistake
        if (j["aslStatUrl"] == "http://stats.allstarlink.org/uhandler")
            j["aslStatUrl"] = "http://stats.allstarlink.org/uhandler.php";
    }

    // SA818 defaults

    if (!j.contains("sa818port")) {
        j["sa818port"] = "";
    }
    if (!j.contains("sa818bw")) {
        j["sa818bw"] = "1";
    }
    if (!j.contains("sa818txf")) {
        j["sa818txf"] = "";
    }
    if (!j.contains("sa818txpl")) {
        j["sa818txpl"] = "0000";
    }
    if (!j.contains("sa818rxf")) {
        j["sa818rxf"] = "";
    }
    if (!j.contains("sa818rxpl")) {
        j["sa818rxpl"] = "0000";
    }
    if (!j.contains("sa818sq")) {
        j["sa818sq"] = "4";
    }
    if (!j.contains("sa818vol")) {
        j["sa818vol"] = "8";
    }
    if (!j.contains("sa818emp")) {
        j["sa818emp"] = "0";
    }
    if (!j.contains("sa818lpf")) {
        j["sa818lpf"] = "0";
    }
    if (!j.contains("sa818hpf")) {
        j["sa818hpf"] = "0";
    }

    if (!j.contains("sa818port_1")) {
        j["sa818port_1"] = "";
    }
    if (!j.contains("sa818bw_1")) {
        j["sa818bw_1"] = "1";
    }
    if (!j.contains("sa818txf_1")) {
        j["sa818txf_1"] = "";
    }
    if (!j.contains("sa818txpl_1")) {
        j["sa818txpl_1"] = "0000";
    }
    if (!j.contains("sa818rxf_1")) {
        j["sa818rxf_1"] = "";
    }
    if (!j.contains("sa818rxpl_1")) {
        j["sa818rxpl_1"] = "0000";
    }
    if (!j.contains("sa818sq_1")) {
        j["sa818sq_1"] = "4";
    }
    if (!j.contains("sa818vol_1")) {
        j["sa818vol_1"] = "8";
    }
    if (!j.contains("sa818emp_1")) {
        j["sa818emp_1"] = "0";
    }
    if (!j.contains("sa818lpf_1")) {
        j["sa818lpf_1"] = "0";
    }
    if (!j.contains("sa818hpf_1")) {
        j["sa818hpf_1"] = "0";
    }

    if (!j.contains("duplexmode")) {
        j["duplexmode"] = "0";
    }
    if (!j.contains("echogain")) {
        j["echogain"] = "0";
    }
}

    }
}

