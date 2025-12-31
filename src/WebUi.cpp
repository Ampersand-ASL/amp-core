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

#include "Message.h"
#include "MessageConsumer.h"
#include "WebUi.h"

using namespace std;
using json = nlohmann::json;

// https://github.com/yhirose/cpp-httplib
// https://github.com/nlohmann/json

static const unsigned DEST_CALL_ID = 1;

namespace kc1fsz {

    namespace amp {

WebUi::WebUi(Log& log, Clock& clock, MessageConsumer& cons, unsigned listenPort,
    unsigned networkDestLineId, unsigned radioDestLineId, const char* configFileName) 
:   _log(log), 
    _clock(clock),
    _consumer(cons),
    _listenPort(listenPort),
    _networkDestLineId(networkDestLineId),
    _radioDestLineId(radioDestLineId),
    _configFileName(configFileName) {

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
        string remoteNumber = payload->remoteNumber;
        if (!remoteNumber.empty()) {
            _status.manipulateUnderLock([&remoteNumber, payload](std::vector<Peer>& v) {
                Peer peer;
                peer.remoteNumber = remoteNumber;
                // #### TODO: NEED TO MAKE THIS 64-bit!
                peer.startMs = payload->startMs;
                v.push_back(peer);
            });
        }
    } else if (msg.isSignal(Message::SignalType::CALL_END)) {
        assert(msg.size() == sizeof(PayloadCallEnd));
        const PayloadCallEnd* payload = (const PayloadCallEnd*)msg.body();
        string remoteNumber = payload->remoteNumber;
        if (!remoteNumber.empty()) {
            _status.manipulateUnderLock([remoteNumber, payload](std::vector<Peer>& v) {
                v.erase(
                    std::remove_if(v.begin(), v.end(),
                        [remoteNumber](Peer& p) { 
                            return p.remoteNumber == remoteNumber; 
                        }
                    ), 
                    v.end());                 
            });
        }
    }
        //msg.isSignal(Message::SignalType::CALL_STATUS)) {
        //cout << "Got signal" << endl;
    //}
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

    pthread_setname_np(pthread_self(), "amp-server-ui");

    _log.info("ui_thread start");

    // HTTP
    httplib::Server svr;

    // ------ Main Page --------------------------------------------------------

    svr.Get("/", [](const httplib::Request &, httplib::Response &res) {
        res.set_file_content("../amp-core/www/index.html");
    });
    svr.Get("/status", [this](const httplib::Request &, httplib::Response &res) {
        json o;
        o["cos"] = _cos.load();
        o["ptt"] = _ptt;
        // Build the list of nodes that we are connected to 
        auto a = json::array();
        vector<Peer> list = _status.getCopy();
        for (Peer l : list) {
            json o2;
            o2["node"] = l.remoteNumber;
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
            } 
            else if (data["button"] == "drop") {
                // #### TODO: CHANGE
                string localNode = "672730";
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
            else if (data["button"] == "exit") {
                exit(0);
            }
        }
    });

    // ------ Config Page-------------------------------------------------------

    svr.Get("/config", [](const httplib::Request &, httplib::Response &res) {
        res.set_file_content("../amp-core/www/config.html");
    });
    svr.Get("/config-load", [this](const httplib::Request &, httplib::Response &res) {
        cout << "/config-load" << endl;
        cout << _config.getCopy().dump() << endl;
         res.set_content(_config.getCopy().dump(), "application/json");
    });
    svr.Post("/config-save", [this](const httplib::Request &, httplib::Response &res, 
        const httplib::ContentReader &content_reader) {
        std::string body;
        content_reader([&](const char *data, size_t data_length) {
            body.append(data, data_length);
            return true;
        });
        cout << "/config-save" << endl;
        cout << body << endl;
        json jBody = json::parse(body);
        jBody["lastUpdateMs"] = _clock.timeUs() / 1000;
        // #### TODO: Reformat audio device selection
        ofstream cf(_configFileName);
        cf << jBody.dump() << endl;
        cf.close();
    });
    svr.Get("/audiodevice-list", [](const httplib::Request &, httplib::Response &res) {
        auto a = json::array();
        json o;
        o["bus"] = "1";
        o["port"] = "2";
        o["query"] = "bus:1,port:2";
        o["desc"] = "C-Media Electronics, Inc. USB Audio Device";
        a.push_back(o);
        o["bus"] = "1";
        o["port"] = "3";
        o["query"] = "bus:1,port:3";
        o["desc"] = "Other";
        a.push_back(o);
         res.set_content(a.dump(), "application/json");
    });

    // OK to use this const
    svr.listen("0.0.0.0", _listenPort);

    _log.info("ui_thread end");
}

    }
}
