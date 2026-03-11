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
#include "httplib.h"

extern "C" {
    #include "base64.h"
}

#ifdef _WIN32
#include <process.h>
#endif

#include <iostream>
#include <thread>
#include <fstream>
#include <sstream>

// 3rd Party
#include <nlohmann/json.hpp>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"
#include "kc1fsz-tools/threadsafequeue.h"

#ifndef _WIN32
#include "sound-map.h"
#include "serial-map.h"
#include "LineUsb.h"
#endif

#include "Message.h"
#include "MessageConsumer.h"
#include "WebUi.h"
#include "ThreadUtil.h"
#include "TraceLog.h"
#include "SerialUtil.h"

#define CMEDIA_VENDOR_ID ("0d8c")

using namespace std;
using namespace kc1fsz;

using json = nlohmann::json;

// https://github.com/yhirose/cpp-httplib
// https://github.com/nlohmann/json

static const unsigned DEST_CALL_ID = 1;

/* 
External Resources
------------------
In order to simplify deployment, the static asset files (.html, .css, etc.) 
are converted into static resources and linked directly into the binary. This 
is done using the xxd command. Here's an example of what this looks like in a 
typical CMake file:

# Converting an html file into a .c file 
set(RESOURCE0 "${CMAKE_CURRENT_SOURCE_DIR}/amp-core/www/index.html")
set(OBJECT0 "${CMAKE_CURRENT_BINARY_DIR}/resource-amp-core-www-index.c")
set(NAME0 "_amp_core_www_index_html")
add_custom_command(
    OUTPUT ${OBJECT0}
    COMMAND xxd
    ARGS 
        -n ${NAME0}
        -i 
        ${RESOURCE0}
        ${OBJECT0}
    DEPENDS ${RESOURCE0}
)
*/
extern unsigned char _amp_core_www_index_html[];
extern unsigned int _amp_core_www_index_html_len;
extern unsigned char _amp_core_www_config_html[];
extern unsigned int _amp_core_www_config_html_len;
extern unsigned char _amp_core_www_main_css[];
extern unsigned int _amp_core_www_main_css_len;

namespace kc1fsz {

    namespace amp {

static void checkJSON(json j, const char* name) {
    if (!j[name].is_string()) {
        string msg = name;
        msg += " is missing/invalid";
        throw invalid_argument(msg);
    }
}

WebUi::WebUi(Log& log, Clock& clock, unsigned listenPort,
    unsigned networkDestLineId, unsigned radioDestLineId, const char* configFileName,
    const char* version, TraceLog& traceLog) 
:   _log(log), 
    _clock(clock),
    _listenPort(listenPort),
    _networkDestLineId(networkDestLineId),
    _radioDestLineId(radioDestLineId),
    _configFileName(configFileName),
    _version(version),
    _traceLog(traceLog) {
}

void WebUi::consume(const Message& msg) {   
    if (msg.isSignal(Message::SignalType::CALL_LEVELS)) {
        assert(msg.size() == sizeof(PayloadCallLevels));
        const PayloadCallLevels* payload = (const PayloadCallLevels*)msg.body();
        json o;
        o["usb-rx-meter"] = payload->rx0Db;
        o["usb-tx-meter"] = payload->tx0Db;
        o["net-rx-meter"] = payload->rx1Db;
        o["net-tx-meter"] = payload->tx1Db;
        _levels.set(o);
    }
}

// https://github.com/joedf/base64.c/blob/master/base64.h
bool WebUi::_checkAuthorization(const string& auth) const {
    string up = string("user:") + _uiPwd;
    // Prevent an overrun on the output of the encoding process
    if (b64e_size(up.length() > 64))
        return false;
    unsigned char encoded[65];
    b64_encode((const unsigned char*)up.c_str(), up.length(), encoded);
    string target = string("Basic ") + string((const char*)encoded);
    return auth == target;
}

void WebUi::uiThread(WebUi* ui, MessageConsumer* bus) {

    amp::setThreadName("amp-ui");
    
    ui->_log.info("ui_thread start (HTTP port is %d)", ui->_listenPort);

    // HTTP
    httplib::Server svr;

    svr.set_pre_request_handler([ui](const auto& req, auto& res) {
        if (!ui->_uiPwd.empty()) {
            if (req.has_header("Authorization")) {
                if (ui->_checkAuthorization(req.get_header_value("Authorization"))) 
                    return httplib::Server::HandlerResponse::Unhandled;
            }
            ui->_log.info("Requesting authentication");
            // Generate challenge
            res.set_header("WWW-Authenticate", "Basic realm=\"Ampersand\"");
            res.status = httplib::StatusCode::Unauthorized_401;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // ------ Common -----------------------------------------------------------

    svr.Get("/main.css", [](const httplib::Request &, httplib::Response &res) {
        res.set_content((const char*)_amp_core_www_main_css, _amp_core_www_main_css_len,
            "text/css");
        //res.set_file_content("../amp-core/www/main.css");
    });

    // ------ Main Page --------------------------------------------------------

    svr.Get("/", [](const httplib::Request &, httplib::Response &res) {
        res.set_content((const char*)_amp_core_www_index_html, _amp_core_www_index_html_len,
            "text/html");
        //res.set_file_content("../amp-core/www/index.html");
    });

    svr.Get("/favorites", [ui](const httplib::Request &, httplib::Response &res) {
        auto l = json::array();
        // Parse
        json cfg = ui->_config.getCopy();
        if (cfg.contains("favorites")) {
            string favorites = cfg["favorites"].get<std::string>();
            // This is a comma-delimited list
            std::istringstream tokenStream(favorites);
            string token;
            while (std::getline(tokenStream, token, ',')) {
                trim(token);
                if (token.empty())
                    continue;
                l.push_back(token);
            }
        }

        // The levels document gets posted in periodically
        res.set_content(l.dump(), "application/json");
    });

    svr.Get("/levels", [ui](const httplib::Request &, httplib::Response &res) {
        // The levels document gets posted in periodically
        res.set_content(ui->_levels.getCopy().dump(), "application/json");
    });

    svr.Get("/controls", [ui](const httplib::Request &, httplib::Response &res) {
        json o;
        o["ptt"] = ui->_ptt.load();
        res.set_content(o.dump(), "application/json");
    });

    svr.Get("/status", [ui](const httplib::Request &, httplib::Response &res) {
        // The status document gets posted in periodically
        res.set_content(ui->_status.getCopy().dump(), "application/json");
    });

    svr.Post("/status-pressed", [ui, bus](const httplib::Request &, httplib::Response &res, 
        const httplib::ContentReader &content_reader) {

        // Pull out the JSON content from the post body
        std::string body;
        content_reader([&body](const char *data, size_t data_length) {
            body.append(data, data_length);
            return true;
        });
        json data = json::parse(body);

        if (data.contains("action")) {
            if (data["action"] == "tone") {
                PayloadTone payload;
                string freq = data["freq"];
                payload.freq = std::stof(freq);
                payload.level = 0.5;
                ui->_log.info("Tone %f %f", payload.freq, payload.level);
                MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::TONE, 
                    sizeof(payload), (const uint8_t*)&payload, 0, 0);
                msg.setDest(ui->_radioDestLineId, DEST_CALL_ID);
                bus->consume(msg);
            } else if (data["action"] == "ptt") {
                ui->_ptt.store(!ui->_ptt.load());
                MessageEmpty msg;
                if (ui->_ptt.load()) 
                    msg = MessageEmpty::signal(Message::SignalType::COS_ON);
                else 
                    msg = MessageEmpty::signal(Message::SignalType::COS_OFF);
                msg.setDest(ui->_radioDestLineId, DEST_CALL_ID);
                bus->consume(msg);
            } 
            else if (data["action"] == "call") {
                json cfgDoc = ui->_config.getCopy();
                string localNode;
                if (cfgDoc.contains("node"))
                    localNode = cfgDoc["node"];
                string targetNode = data["node"];
                // ### TODO: CLEAN UP
                trim(targetNode);
                if (!localNode.empty() && !targetNode.empty()) {
                    PayloadCall payload;
                    strcpyLimited(payload.localNumber, localNode.c_str(), sizeof(payload.localNumber));
                    strcpyLimited(payload.targetNumber, targetNode.c_str(), sizeof(payload.targetNumber));
                    MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_NODE, 
                        sizeof(payload), (const uint8_t*)&payload, 0, 0);
                    msg.setDest(ui->_networkDestLineId, DEST_CALL_ID);
                    bus->consume(msg);
                } else {
                    // ### TODO: ERROR MESSAGE
                }
            } else if (data["action"] == "dtmf") {
                string symbol = data["symbol"].get<std::string>();
                if (symbol.size() >= 1) {
                    PayloadDtmfGen payload;
                    payload.symbol = symbol[0];
                    MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::DTMF_GEN, 
                        sizeof(payload), (const uint8_t*)&payload, 0, 0);
                    msg.setDest(ui->_networkDestLineId, DEST_CALL_ID);
                    bus->consume(msg);
                }
            }
            else if (data["action"] == "dropcall") {
                MessageEmpty msg = MessageEmpty::signal(Message::SignalType::DROP_CALL);
                int lineId = data["lineId"].get<int>();
                int callId = data["callId"].get<int>();
                msg.setDest(lineId, callId);
                bus->consume(msg);
            }
            else if (data["action"] == "dropall") {
                MessageEmpty msg = MessageEmpty::signal(Message::SignalType::DROP_ALL_CALLS);
                msg.setDest(ui->_networkDestLineId, DEST_CALL_ID);
                bus->consume(msg);
            }
            else if (data["action"] == "capture") {
                ofstream trace("./capture.txt");
                ui->_traceLog.visitAll([&trace](const string& str) {
                    trace << str << endl;
                });
            }
        }
    });

    // ------ Config Page-------------------------------------------------------

    svr.Get("/config", [](const httplib::Request &, httplib::Response &res) {
        res.set_content((const char*)_amp_core_www_config_html, _amp_core_www_config_html_len,
            "text/html");
        //res.set_file_content("../amp-core/www/config.html");
    });
    svr.Get("/config-load", [ui](const httplib::Request &, httplib::Response &res) {
        // The config document gets posted in whenever it changes
        res.set_content(ui->_config.getCopy().dump(), "application/json");
    });
    svr.Post("/config-save", [ui](const httplib::Request &, httplib::Response &res, 
        const httplib::ContentReader &content_reader) {
        std::string body;
        content_reader([&](const char *data, size_t data_length) {
            body.append(data, data_length);
            return true;
        });
        ui->_log.info("Saving configuration");
        json jBody = json::parse(body);
        jBody["lastUpdateMs"] = ui->_clock.timeUs() / 1000;
        jBody["version"] = ui->_version;
        ofstream cf(ui->_configFileName);
        cf << jBody.dump(4) << endl;
    });

    // This is the end-point that supplies choices for the dynamic select/option
    // menus.
    svr.Get("/config-select-options", 
        [](const httplib::Request& req, httplib::Response &res) {

        auto a = json::array();
        const string menuName = req.get_param_value("name");      

        // The menus that show a list of allowable dB values for USB mixers.
        if (menuName == "aslTxMixASet" || menuName == "aslTxMixBSet" || menuName == "aslRxMixerSet") {
            string arg = req.get_param_value("arg");      
#ifndef _WIN32            
            if (arg.starts_with("usb ")) {
                // Try to locate that sound device and get its volume range
                int alsaDev;
                string ossDev;
                int rc = querySoundMap(arg.substr(4).c_str(), alsaDev, ossDev);
                if (rc == 0) {
                    // Get range, first in units and then convert to dB
                    char name[32];
                    snprintf(name, 32, "hw:%d", alsaDev);
                    int rc2, minV, maxV;
                    // The parameter name depends on whether we are talking about play or capture
                    if (menuName == "aslTxMixASet" || menuName == "aslTxMixBSet") {
                        rc2 = getMixerRange(name, "Speaker Playback Volume", &minV, &maxV);
                    }
                    else if (menuName == "aslRxMixerSet") {
                        rc2 = getMixerRange(name, "Mic Capture Volume", &minV, &maxV);
                    }
                    else {
                        rc2 = -1;
                    }
                    if (rc2 == 0) {
                        int minDb = 0, maxDb = 0;
                        if (menuName == "aslTxMixASet" || menuName == "aslTxMixBSet") {
                            convertMixerValueToDb(name, "Speaker Playback Volume", minV, &minDb);
                            convertMixerValueToDb(name, "Speaker Playback Volume", maxV, &maxDb);
                        }
                        else if (menuName == "aslRxMixerSet") {
                            convertMixerValueToDb(name, "Mic Capture Volume", minV, &minDb);
                            convertMixerValueToDb(name, "Mic Capture Volume", maxV, &maxDb);
                        }
                        for (int g = maxDb; g >= minDb; g--) {
                            json o;
                            char t[32];
                            snprintf(t, 32, "%d", g);
                            o["value"] = t;
                            snprintf(t, 32, "%ddB", g);
                            o["desc"] = t;
                            a.push_back(o);
                        }
                    }
                }
            }
#endif            
        }

        // The menu that shows the list of USB sound devices
        else if (menuName == "aslAudioDevice") {

#ifndef _WIN32            
            json o;
            o["value"] = "";
            o["desc"] = "None";
            a.push_back(o);

            visitUSBDevices2([&a](
                const char* vendorName, const char* productName, 
                const char* vendorId, const char* productId,                 
                const char* busId, const char* portId) {
                    // Only doing C-Media
                    if (strcasecmp(vendorId, CMEDIA_VENDOR_ID) == 0) {
                        // Make the value
                        string val("usb ");
                        val += "bus:";
                        val += busId;
                        val += ",";
                        val += "port:";
                        val += portId;
                        // Make the description
                        string desc("USB bus");
                        desc += busId;
                        desc += "/port";
                        desc += portId;
                        desc += " ";
                        desc += vendorName;
                        desc += " ";
                        desc += productName;
                        json o;
                        o["value"] = val;
                        o["desc"] = desc;
                        a.push_back(o);                
                    }
                }
            );
#endif  
        }          
        // The menu that shows the list of USB serial devices
        else if (menuName == "sa818port" || menuName == "sa818port_1") {
#ifndef _WIN32            
            json o;
            o["value"] = "";
            o["desc"] = "None";
            a.push_back(o);
            // Traverse the USB serial devices
            visitUSBSerialDevices(
                [&a](const char* dev, unsigned busId, unsigned portId) {
                    // Make the value
                    char value[32];
                    snprintf(value, sizeof(value), "usb bus:%u,port:%u", busId, portId);
                    // Make the description
                    char desc[64];
                    snprintf(desc, sizeof(desc), "%s (bus %u, port %u)", dev, busId, portId);
                    json o;
                    o["value"] = value;
                    o["desc"] = desc;
                    a.push_back(o);
                }
            );
#endif
        }

        res.set_content(a.dump(), "application/json");
    });

    svr.Get("/hiddevice-list", [](const httplib::Request &, httplib::Response &res) {
        
        auto a = json::array();

#ifndef _WIN32        

        json o;
        o["value"] = "";
        o["desc"] = "None";
        a.push_back(o);

        visitUSBDevices2([&a](const char* vendorName, const char* productName, 
            const char* vendorId, const char* productId,             
            const char* busId, const char* portId) {
                // Skip some things that aren't relevant
                if (strstr(vendorName, "Linux Foundation") != 0)
                    return;
                // Make the value
                string val("usb ");
                val += "bus:";
                val += busId;
                val += ",";
                val += "port:";
                val += portId;
                // Make the description
                string desc("USB ");
                desc += busId;
                desc += "/";
                desc += portId;
                desc += " ";
                desc += vendorName;
                desc += " ";
                desc += productName;
                json o;
                o["value"] = val;
                o["desc"] = desc;
                a.push_back(o);
            }
        );
#endif        
        res.set_content(a.dump(), "application/json");
    });

    // An end-point for programming SA818 radios
    svr.Post("/program-sa818", [ui, bus](const httplib::Request &, httplib::Response &res, 
        const httplib::ContentReader &content_reader) {

        // Pull out the JSON content from the post body
        std::string body;
        content_reader([&body](const char *data, size_t data_length) {
            body.append(data, data_length);
            return true;
        });

        json cfg = json::parse(body);
        json result;

        int radio = cfg["radio"].get<int>();

        if ((radio == 0 || radio == 1) && cfg.contains("sa818port")) {

            string sa818portQuery = cfg["sa818port"].get<std::string>().c_str();
            if (!sa818portQuery.empty()) {

                string sa818Device;
                ui->_log.info("SA818 port config [%s]", sa818portQuery.c_str());
                if (querySerialDevices(sa818portQuery.c_str(), sa818Device) == 0) {

                    ui->_log.info("Resolved to SA818 device %s", sa818Device.c_str());

                    {
                        // Get the configuration parameters and program the device
                        checkJSON(cfg, "sa818bw");
                        unsigned bw = std::stoi(cfg["sa818bw"].get<std::string>());
                        checkJSON(cfg, "sa818txf");
                        float f = std::stof(cfg["sa818txf"].get<std::string>());
                        unsigned txKhz = f * 10000;
                        checkJSON(cfg, "sa818rxf");
                        f = std::stof(cfg["sa818rxf"].get<std::string>());
                        unsigned rxKhz = f * 10000;
                        checkJSON(cfg, "sa818txpl");
                        unsigned txPl = std::stoi(cfg["sa818txpl"].get<std::string>());
                        checkJSON(cfg, "sa818rxpl");
                        unsigned rxPl = std::stoi(cfg["sa818rxpl"].get<std::string>());
                        checkJSON(cfg, "sa818sq");
                        unsigned sq = std::stoi(cfg["sa818sq"].get<std::string>());
                        checkJSON(cfg, "sa818vol");
                        unsigned vol = std::stoi(cfg["sa818vol"].get<std::string>());
                        checkJSON(cfg, "sa818emp");
                        bool emp = cfg["sa818emp"].get<std::string>() == "1";
                        checkJSON(cfg, "sa818lpf");
                        bool lpf = cfg["sa818lpf"].get<std::string>() == "1";
                        checkJSON(cfg, "sa818hpf");
                        bool hpf = cfg["sa818hpf"].get<std::string>() == "1";

                        int rc = SerialUtil::configureSA818(ui->_log, sa818Device.c_str(), bw, txKhz, rxKhz,
                            txPl, rxPl, sq, vol, emp, lpf, hpf);
                        if (rc != 0) {
                            ui->_log.error("SA818 config error %d", rc);
                            result["status"] = "fail";
                            result["msg"] = "Programming error: " +  std::to_string(rc);
                        } else {
                            result["status"] = "ok";
                        }
                    }
                } else {
                    result["status"] = "fail";
                    result["msg"] = "SA818 not found";
                }
            }
            else {
                result["status"] = "fail";
                result["msg"] = "SA818 not selected";
            }
        } else {
            result["status"] = "fail";
            result["msg"] = "SA818 not selected";
        }

        res.set_content(result.dump(), "application/json");
    });

    svr.Get("/log", [](const httplib::Request &, httplib::Response &res) {
        res.set_content((const char*)_amp_core_www_index_html, _amp_core_www_index_html_len,
            "text/html");
        //res.set_file_content("../amp-core/www/log.html");
    });

    svr.Get("/log-data", [ui](const httplib::Request &, httplib::Response &res) {
        auto a = json::array();
        // GO FAST! This is blocking the multi-threaded logger
        res.set_content(a.dump(), "application/json");
    });

    // OK to use this const
    if (!svr.listen("0.0.0.0", ui->_listenPort)) {
        ui->_log.error("Unable to bind to HTTP port %d", ui->_listenPort);
    }

    ui->_log.info("ui_thread end");
}

    }
}
