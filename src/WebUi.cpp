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
#include "LineUsb.h"
#endif

#include "Message.h"
#include "MessageConsumer.h"
#include "WebUi.h"
#include "ThreadUtil.h"
#include "TraceLog.h"

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

WebUi::WebUi(Log& log, Clock& clock, MessageConsumer& cons, unsigned listenPort,
    unsigned networkDestLineId, unsigned radioDestLineId, const char* configFileName,
    const char* version, TraceLog& traceLog) 
:   _log(log), 
    _clock(clock),
    _consumer(cons),
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

void WebUi::uiThread(WebUi* ui, MessageConsumer* bus) {

    amp::setThreadName("amp-ui");
    
    ui->_log.info("ui_thread start (HTTP port is %d)", ui->_listenPort);

    // HTTP
    httplib::Server svr;

    // ------ Common -----------------------------------------------------------

    svr.Get("/main.css", [](const httplib::Request &, httplib::Response &res) {
        //res.set_content((const char*)_amp_core_www_main_css, _amp_core_www_main_css_len,
        //    "text/css");
        res.set_file_content("../amp-core/www/main.css");
    });

    // ------ Main Page --------------------------------------------------------

    svr.Get("/", [](const httplib::Request &, httplib::Response &res) {
        //res.set_content((const char*)_amp_core_www_index_html, _amp_core_www_index_html_len,
        //    "text/html");
        res.set_file_content("../amp-core/www/index.html");
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
        o["ptt"] = ui->_ptt;
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
            if (data["action"] == "ptt") {
                ui->_ptt = !ui->_ptt;
                Message msg;
                if (ui->_ptt) 
                    msg = Message::signal(Message::SignalType::COS_ON);
                else 
                    msg = Message::signal(Message::SignalType::COS_OFF);
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
                    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_NODE, 
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
                    Message msg(Message::Type::SIGNAL, Message::SignalType::DTMF_GEN, 
                        sizeof(payload), (const uint8_t*)&payload, 0, 0);
                    msg.setDest(ui->_networkDestLineId, DEST_CALL_ID);
                    bus->consume(msg);
                }
            }
            else if (data["action"] == "dropcall") {
                Message msg = Message::signal(Message::SignalType::DROP_CALL);
                int lineId = data["lineId"].get<int>();
                int callId = data["callId"].get<int>();
                msg.setDest(lineId, callId);
                bus->consume(msg);
            }
            else if (data["action"] == "dropall") {
                Message msg = Message::signal(Message::SignalType::DROP_ALL_CALLS);
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
        ofstream cf(ui->_configFileName);
        cf << jBody.dump(4) << endl;
    });
    svr.Get("/config-select-options", [](const httplib::Request& req, httplib::Response &res) {

        auto a = json::array();

        string menuName = req.get_param_value("name");      
        
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
        else if (menuName == "aslAudioDevice") {

            json o;
            o["value"] = "";
            o["desc"] = "None";
            a.push_back(o);

#ifndef _WIN32            
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

        res.set_content(a.dump(), "application/json");
    });

    svr.Get("/hiddevice-list", [](const httplib::Request &, httplib::Response &res) {
        
        auto a = json::array();

        json o;
        o["value"] = "";
        o["desc"] = "None";
        a.push_back(o);

#ifndef _WIN32        
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

    // OK to use this const
    svr.listen("0.0.0.0", ui->_listenPort);

    ui->_log.info("ui_thread end");
}

    }
}
