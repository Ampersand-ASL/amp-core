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

#ifdef _WIN32
    _beginthread(_uiThread, 0, (void*)this);
#else
    // Get the service thread running
    pthread_t new_thread_id;
    if (pthread_create(&new_thread_id, NULL, _uiThreadPosix, (void*)this)!= 0) {
        perror("Error creating thread");
    }
#endif
}

void WebUi::consume(const Message& msg) {   
    // Some selected message types will be copied and sent over to the UI 
    // thread for processing
    if (msg.isSignal(Message::COS_ON)) {
        _cos.store(true);
    } else if (msg.isSignal(Message::COS_OFF)) {
        _cos.store(false);
    } else if (msg.isSignal(Message::SignalType::CALL_START)) {
        assert(msg.size() == sizeof(PayloadCallStart));
        const PayloadCallStart* payload = (const PayloadCallStart*)msg.body();
        _status.manipulateUnderLock([msg, payload](std::vector<Peer>& v) {
            Peer peer;
            peer.lineId = msg.getSourceBusId();
            peer.callId = msg.getSourceCallId();
            peer.localNumber = payload->localNumber;
            peer.remoteNumber = payload->remoteNumber;
            // #### TODO: NEED TO MAKE THIS 64-bit!
            peer.startMs = payload->startMs;
            v.push_back(peer);
        });
    } else if (msg.isSignal(Message::SignalType::CALL_END)) {
        _status.manipulateUnderLock([msg](std::vector<Peer>& v) {
            v.erase(
                std::remove_if(v.begin(), v.end(),
                    [msg](Peer& p) { 
                        return p.lineId == msg.getSourceBusId() &&
                            p.callId == msg.getSourceCallId(); 
                    }
                ), 
                v.end());                 
        });
    }
    else if (msg.isSignal(Message::SignalType::CALL_LEVELS)) {
        assert(msg.size() == sizeof(PayloadCallLevels));
        _status.manipulateUnderLock([msg](std::vector<Peer>& v) {
            const PayloadCallLevels* payload = (const PayloadCallLevels*)msg.body();
            for (Peer& p : v) 
                if (p.lineId == msg.getSourceBusId() &&
                    p.callId == msg.getSourceCallId()) {
                    p.rx0Db = payload->rx0Db;
                    p.tx0Db = payload->tx0Db;
                    p.rx1Db = payload->rx1Db;
                    p.tx1Db = payload->tx1Db;
                }
        });
    }
}

bool WebUi::run2() {
    Message msg;
    if (_outQueue.try_pop(msg)) {
        _consumer.consume(msg);
        return true;
    }
    else return false;
}

void WebUi::_uiThread(void* o) {
    ((WebUi*)o)->_thread();
}

void WebUi::_thread() {

    amp::setThreadName("amp-ui");
    
    _log.info("ui_thread start (HTTP port is %d)", _listenPort);

    // HTTP
    httplib::Server svr;

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
    svr.Get("/status", [this](const httplib::Request &, httplib::Response &res) {

        json o;
        o["usb-rx-meter"] = -99;
        o["usb-tx-meter"] = -99;
        o["net-rx-meter"] = -99;
        o["net-tx-meter"] = -99;

        vector<Peer> peerList = _status.getCopy();
        // Find the radio and pull out some stats
        for (auto peer : peerList) {
            if (peer.remoteNumber == "Radio") {
                o["usb-rx-meter"] = peer.rx0Db;
                o["usb-tx-meter"] = peer.tx0Db;
                o["net-rx-meter"] = peer.rx1Db;
                o["net-tx-meter"] = peer.tx1Db;
            }
        }

        o["cos"] = _cos.load();
        o["ptt"] = _ptt;

        // Build the list of nodes that we are connected to 
        auto a = json::array();
        for (Peer peer : peerList) {
            if (peer.remoteNumber == "Radio")
                continue;
            json o2;
            o2["localNode"] = peer.localNumber;
            o2["remoteNode"] = peer.remoteNumber;
            auto b = json::array();
            // #### TODO: NEED TO ADD CONNECTIONS
            o2["connections"] = b;
            a.push_back(o2);
        }
        o["connections"] = a;

        res.set_content(o.dump(), "application/json");
    });
    svr.Post("/status-save", [this](const httplib::Request &, httplib::Response &res, 
        const httplib::ContentReader &content_reader) {
        // Pull out the JSON content from the post body
        std::string body;
        content_reader([&body](const char *data, size_t data_length) {
            body.append(data, data_length);
            return true;
        });

        json data = json::parse(body);

        if (data.contains("button")) {
            if (data["button"] == "ptt") {
                _ptt = !_ptt;
                Message msg;
                if (_ptt) 
                    msg = Message::signal(Message::SignalType::COS_ON);
                else 
                    msg = Message::signal(Message::SignalType::COS_OFF);
                msg.setDest(_radioDestLineId, DEST_CALL_ID);
                _outQueue.push(msg);
            } 
            else if (data["button"] == "call") {
                json cfgDoc = _config.getCopy();
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
                    msg.setDest(_networkDestLineId, DEST_CALL_ID);
                    _outQueue.push(msg);
                } else {
                    // ### TODO: ERROR MESSAGE
                }
            } else if (data["button"].get<std::string>().starts_with("dtmf")) {
                string symbol = data["button"].get<std::string>().substr(4);
                if (symbol.size() >= 1) {
                    PayloadDtmfGen payload;
                    payload.symbol = symbol[0];
                    _log.info("DTMF %c", payload.symbol); 
                    Message msg(Message::Type::SIGNAL, Message::SignalType::DTMF_GEN, 
                        sizeof(payload), (const uint8_t*)&payload, 0, 0);
                    msg.setDest(_networkDestLineId, DEST_CALL_ID);
                    _outQueue.push(msg);
                }
            }
            else if (data["button"] == "drop") {
                string localNode = "*";
                string targetNode = data["node"];
                if (!targetNode.empty()) {
                    // NOTE: Drop uses the same payload as call
                    PayloadCall payload;
                    strcpyLimited(payload.localNumber, localNode.c_str(), sizeof(payload.localNumber));
                    strcpyLimited(payload.targetNumber, targetNode.c_str(), sizeof(payload.targetNumber));
                    Message msg(Message::Type::SIGNAL, Message::SignalType::DROP_NODE, 
                        sizeof(payload), (const uint8_t*)&payload, 0, 0);
                    msg.setDest(_networkDestLineId, DEST_CALL_ID);
                    _outQueue.push(msg);
                }
            }
            else if (data["button"] == "dropall") {
                Message msg = Message::signal(Message::SignalType::DROP_ALL_NODES);
                msg.setDest(_networkDestLineId, DEST_CALL_ID);
                _outQueue.push(msg);
            }
            else if (data["button"] == "capture") {
                ofstream trace("./capture.txt");
                _traceLog.visitAll([&trace](const string& str) {
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
    svr.Get("/config-load", [this](const httplib::Request &, httplib::Response &res) {
        json j = _config.getCopy();
         res.set_content(j.dump(), "application/json");
    });
    svr.Post("/config-save", [this](const httplib::Request &, httplib::Response &res, 
        const httplib::ContentReader &content_reader) {
        std::string body;
        content_reader([&](const char *data, size_t data_length) {
            body.append(data, data_length);
            return true;
        });
        _log.info("Saving configuration");
        cout << body << endl;
        json jBody = json::parse(body);
        jBody["lastUpdateMs"] = _clock.timeUs() / 1000;
        ofstream cf(_configFileName);
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
    svr.listen("0.0.0.0", _listenPort);

    _log.info("ui_thread end");
}

    }
}
