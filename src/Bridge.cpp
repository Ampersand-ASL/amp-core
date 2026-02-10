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
#include <iostream>
#include <cstring> 
#include <thread>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "Bridge.h"

using namespace std;

namespace kc1fsz {
    namespace amp {

Bridge::Bridge(Log& log, Log& traceLog, Clock& clock, MessageConsumer& bus, 
    BridgeCall::Mode defaultMode, 
    unsigned lineId, unsigned ttsLineId, unsigned netTestLineId,
    const char* netTestBindAddr,
    unsigned networkDestLineId,
    BridgeCall* callSpace, unsigned callSpaceLen)
:   _log(log),
    _traceLog(traceLog),
    _clock(clock),
    _bus(bus),
    _defaultMode(defaultMode),
    _lineId(lineId),
    _ttsLineId(ttsLineId),
    _netTestLineId(netTestLineId),
    _networkDestLineId(networkDestLineId),
    _calls(callSpace, callSpaceLen) { 

    // One-time (static) setup of all calls
    for (unsigned i = 0; i < callSpaceLen; i++)
        callSpace[i].init(this, &log, &traceLog, &clock, &_bus, 
            _lineId, i, _ttsLineId, _netTestLineId, netTestBindAddr);
}

void Bridge::reset() {
    _calls.visitAll(RESET_VISITOR);
    _lastCallListChangeMs = 0;
    _statusMessageText.clear();
    _statusMessageUpdateMs = 0;
    _statusMessageLevel = 0;
    _maxTickUs = 0;
}

unsigned Bridge::getCallCount() const {
    return _calls.countIf(ACTIVE_PRED);
}

void Bridge::setLocalNodeNumber(const char* nodeNumber) { 
    _log.info("Bridge node number %s", nodeNumber);
    _nodeNumber = nodeNumber; 
}

void Bridge::setKerchunkFilterNodes(std::vector<std::string> nodes) {
    _kerchunkFilterNodes = nodes;
}

void Bridge::setKerchunkFilterDelayMs(unsigned ms) {
    _kerchunkFilterDelayMs = ms;
}

void Bridge::setGreeting(const char* greeting) { 
    if (greeting)
        _greetingText = greeting; 
    else 
        _greetingText.clear();
}

void Bridge::setParrotLevelThresholds(std::vector<int>& thresholds) {
    _parrotLevelThresholds = thresholds;
}

vector<string> Bridge::getConnectedNodes() const {
    vector<string> result;
    _visitActiveCalls(
        [&result](const BridgeCall& call) { 
            result.push_back(call.getRemoteNodeNumber());
            return true;
        }
    );
    return result;
}

string Bridge::addSpaces(const char* text) {
    string result;
    for (unsigned i = 0; i < strlen(text); i++) {
        if (i > 0)
            result += " ";
        result += text[i];
    }
    return result;
}

uint64_t Bridge::getStatusDocStampMs() const {

    // The idea here is to find the maximum change stamp of any of
    // the possibly dynamic elements of the Bridge.
    uint64_t maxStampMs = _lastCallListChangeMs;
    maxStampMs = max(maxStampMs, _statusMessageUpdateMs);

    // Check each call for more recent activity
    _visitActiveCalls(
        [&maxStampMs](const BridgeCall& call) { 
            maxStampMs = max(call.getStatusDocStampMs(), maxStampMs);
            return true;
        }
    );

    return maxStampMs;
}

json Bridge::getStatusDoc() const {

    json root;

    root["stamp"] = getStatusDocStampMs();

    // Bridge-level status message
    json o2;
    o2["text"] = _statusMessageText;
    o2["level"] = _statusMessageLevel;
    o2["stamp"] = _statusMessageUpdateMs;

    root["message"] = o2;

    // Call-level status
    auto calls = json::array();

    _visitActiveCalls(
        [&calls](const BridgeCall& call) { 
            // Enforce a limit to prevent a giant document
            if (calls.size() < 16) {
                calls.push_back(call.getStatusDoc());
                return true;
            } 
            else {
                return false;
            }
        }
    );

    root["calls"] = calls;

    return root;
}

json Bridge::getLevelsDoc() const {
    json levels;
    // Find the first call that has audio levels to provide.
    // TODO: The logic here needs work.
    _calls.visitIf(
        // Visitor
        [&levels](const BridgeCall& call) { 
            levels = call.getLevelsDoc();
            // Stop on the first hit
            return false;
        },
        // Predicate
        [](const BridgeCall& s) { return s.hasAudioLevels(); }
    );
    return levels;
}

void Bridge::consume(const Message& msg) {

    // This signal is generated when a call that was originated from this 
    // node fails to establish.
    if (msg.isSignal(Message::SignalType::CALL_FAILED)) {   

        PayloadCallFailed payload;
        assert(msg.size() == sizeof(payload));
        memcpy(&payload, msg.body(), sizeof(payload));

        // Announce the failure connection to all of the *other* active calls
        // who may have commanded this connection.
        string prompt = "Unable to connect to node ";
        // Make sure the target isn't too long since we're speaking this
        string target = payload.targetNumber;
        if (target.length() > 10)
            target = target.substr(0, 10);
        prompt += addSpaces(target.c_str());
        prompt += ". ";
        prompt += payload.message;
        prompt += ". ";
        
        _calls.visitIf(
            // Visitor
            [&prompt](BridgeCall& call) { 
                call.requestTTS(prompt.c_str());
                return true;
            },
            // Predicate
            [&msg](const BridgeCall& c) { 
                return c.isActive() && 
                    // Make sure this is a normal conference node (not a parrot)
                    c.isNormal() &&
                    // Make sure this is a call that has been involved in
                    // recent command activity.
                    c.isRecentCommander();
            }
        );

        // Build a message for display on the UI
        char msg[128];
        snprintf(msg, sizeof(msg), "Call to %s failed: %s",
            payload.targetNumber, payload.message);

        _statusMessageText = msg;
        _statusMessageUpdateMs = _clock.timeMs();
        _statusMessageLevel = 2;
    } 
    else if (msg.isSignal(Message::SignalType::CALL_START)) {        

        _lastCallListChangeMs = _clock.timeUs() / 1000;

        // Remove old/existing session for this call (if any)
        _calls.visitIf(
            // Visitor
            RESET_VISITOR,
            // Predicate
            [&msg](const BridgeCall& s) { return s.belongsTo(msg); }
        );
        
        // Add new session for this call
        // #### TODO: CONSIDER POSITIVE ACK ON ACCEPTED CALL AND ELIMINATE
        // #### THE NACK CASE BELOW.
        int newIndex = _calls.firstIndex([](const BridgeCall& s) { return !s.isActive(); });
        if (newIndex == -1) {
            _log.info("Max sessions, rejecting call %d", msg.getSourceCallId());
            // #### TODO: NEED TO TEST THIS AFTER RACE CONDITION IS RESOLVED
            MessageEmpty msg(Message::Type::SIGNAL, Message::SignalType::CALL_TERMINATE,
                0, _clock.time());
            msg.setDest(msg.getSourceBusId(), msg.getSourceCallId());
            //_bus.consume(msg);
        }
        else {                
            PayloadCallStart payload;
            assert(msg.size() == sizeof(payload));
            memcpy(&payload, msg.body(), sizeof(payload));

            _log.info("Call %u:%u started node %s CODEC %08X, jbBypass %d, echo %d, validated %d", 
                msg.getSourceBusId(),
                msg.getSourceCallId(), 
                payload.remoteNumber, 
                payload.codec, 
                payload.bypassJitterBuffer,
                payload.echo, 
                payload.sourceAddrValidated);

            // Check to see if this node should be using the kerchunk filter
            string remoteNodeNumber = payload.remoteNumber;
            auto it = std::find(_kerchunkFilterNodes.begin(), _kerchunkFilterNodes.end(),
                remoteNodeNumber);
            bool useKerchunkFilter = it != _kerchunkFilterNodes.end();
            if (useKerchunkFilter)
                _log.info("Enabling kerchunk filter, delay %u ms", 
                    _kerchunkFilterDelayMs);

            BridgeCall& call = _calls.at(newIndex);
            call.setup(msg.getSourceBusId(), msg.getSourceCallId(), 
                payload.startMs, payload.codec, payload.bypassJitterBuffer, payload.echo, 
                payload.sourceAddrValidated, _defaultMode, 
                payload.remoteNumber, payload.permanent, useKerchunkFilter,
                _kerchunkFilterDelayMs);

            // Play the greeting to the new caller, but not for calls that 
            // we originated in the first place
            if (!payload.originated && call.isNormal()) {
                if (!_greetingText.empty())       
                    call.requestTTS(_greetingText.c_str());
            } 

            // Announce the new connection to all of the *other* active calls
            // who may have commanded this connection.
            string prompt = "Node ";
            prompt += addSpaces(payload.remoteNumber);
            prompt += " connected.";
            
            _calls.visitIf(
                // Visitor
                [&prompt](BridgeCall& call) { 
                    call.requestTTS(prompt.c_str());
                    return true;
                 },
                // Predicate
                [&msg](const BridgeCall& c) { 
                    return c.isActive() && 
                        // Make sure this is a normal conference node (not a parrot)
                        c.isNormal() &&
                        // Make sure this is a call that has been involved in
                        // recent command activity.
                        c.isRecentCommander() &&
                        // Make sure this is NOT the call we just setup above
                        !(c._lineId == msg.getSourceBusId() && c._callId == msg.getSourceCallId());
                }
            );

            // Provide a status message announcing success.
            char msg[64];
            snprintf(msg, 64, "Node %s connected", payload.remoteNumber);
            _statusMessageText = msg;
            _statusMessageUpdateMs = _clock.timeMs();
            _statusMessageLevel = 0;
        }
    }
    else if (msg.isSignal(Message::SignalType::CALL_END)) {

        _lastCallListChangeMs = _clock.timeMs();

        PayloadCallEnd payload;
        assert(msg.size() == sizeof(payload));
        memcpy(&payload, msg.body(), sizeof(payload));

        _log.info("Call ended %u:%u (%s)", msg.getSourceBusId(), msg.getSourceCallId(),
            payload.remoteNumber);

        _calls.visitIf(
            // Visitor
            RESET_VISITOR,
            // Predicate
            [&msg](const BridgeCall& c) { return c.belongsTo(msg); }         
        );

        // Announce the dropped connection to all of the *other* active calls
        string prompt = "Node ";
        prompt += addSpaces(payload.remoteNumber);
        prompt += " disconnected.";
        
        _calls.visitIf(
            // Visitor
            [&prompt](BridgeCall& call) { 
                call.requestTTS(prompt.c_str()); 
                return true;
            },
            // Predicate
            [&msg](const BridgeCall& c) { 
                return c.isActive() && 
                    // Make sure this is a normal conference node (not parrot)
                    c.isNormal() &&
                    // Make sure this is a call that has been involved in
                    // recent command activity.
                    c.isRecentCommander() &&
                    // Make sure this is NOT the call we just dropped above
                    !(c._lineId == msg.getSourceBusId() && c._callId == msg.getSourceCallId());
            }
        );
    }
    // Everything else gets passed directly to the call.
    else {
        _calls.visitIf(
            // Visitor
            [&msg](BridgeCall& call) { 
                call.consume(msg);
                // Stop on first find
                return false;
            },
            // Predicate
            [&msg](const BridgeCall& s) { return s.belongsTo(msg); }
        );
    }
}

/**
 * This function is the heart of the conference bridge. On every audio tick we 
 * do the following:
 * 
 * 1. Ask each active (speaking) conference participant to prepare input audio 
 *    frame to contribute to the final mix.
 * 2. Prepare a mixed audio frame for each conference participant. This is 
 *    customized because not all participants will want to hear their own audio
 *    in the mix.
 * 3. Give each participant an output audio frame.
 * 4. Clear the contributors' audio so its not used the next time.
 *
 * PERFORMANCE CRITICAL AREA!
 */
void Bridge::audioRateTick(uint32_t tickMs) {

    uint64_t startUs = _clock.timeUs();
    
    // ####
    uint64_t tA = 0;
    uint64_t tB = 0;
    uint64_t tC = 0;

    // Tick each call so that we have an input frame for each.
    _visitActiveCalls(
        [tickMs](BridgeCall& call) { 
            // Tick the call to get it to produce an audio frame
            call.audioRateTick(tickMs);
            return true;
        }
    );

    // ####
    tA = _clock.timeUs() - startUs;

    // Perform mixing and create a mixed output for each active call
    for (unsigned i = 0; i < _calls.size(); i++) {

        uint64_t startBUs = _clock.timeUs();
        
        if (!_calls[i].isActive())
            continue;

        // Figure out how many calls we are mixing. Keep in mind that calls only contribute
        // audio to themselves if echo mode is enabled for that call.
        int mixCount = 0;
        for (unsigned j = 0; j < _calls.size(); j++) {
            // Ignore calls that are inactive or are silent
            if (!_calls[j].isActive())
                continue;
            if (!_calls[j].hasInputAudio())
                continue;
            // Ignore ourself if echo is turned off
            if (i == j && !_calls[j].isEcho())
                continue;
            mixCount++;
        }

        // This is the target for the mixing of the conference audio
        int16_t mixedFrame[BLOCK_SIZE_48K] = { 0 };

        // Now do the actual mixing
        if (mixCount > 0) {
            for (unsigned j = 0; j < _calls.size(); j++) {
                // Ignore calls that are inactive or are silent
                if (!_calls[j].isActive())
                    continue;
                if (!_calls[j].hasInputAudio())
                    continue;
                // Ignore ourself if echo is turned off
                if (i == j && !_calls[j].isEcho())
                    continue;
                // NOTE: This appears to be the most time-critical step in this 
                // process at the moment.
                _calls[j].extractInputAudio(mixedFrame, BLOCK_SIZE_48K, mixCount, tickMs);
            }
        }

        uint64_t endBUs = _clock.timeUs();
        tB += (endBUs - startBUs);

        // Output the result
        uint64_t startCUs = _clock.timeUs();

        _calls[i].setConferenceOutput(mixedFrame, BLOCK_SIZE_48K, tickMs, mixCount);

        uint64_t endCUs = _clock.timeUs();
        tC += (endCUs - startCUs);
    }

    // Clear all contributions for this tick
    _visitActiveCalls([](BridgeCall& call) { call.clearInputAudio(); return true; });
    
    uint64_t endUs = _clock.timeUs();
    uint64_t durUs = endUs - startUs;
    if (durUs > _maxTickUs) {
        _maxTickUs = durUs;
        _log.info("Max Bridge output tick: %ld", _maxTickUs);
        _log.info("A=%ld B=%ld C=%ld", tA, tB, tC);
    }
}

void Bridge::oneSecTick() {

    // Tick each call
    _visitActiveCalls(
        [](BridgeCall& call) { 
            call.oneSecTick();
            return true;
        }
    );

    // If there are any calls with recent talking activity then move that 
    // talker ID to all active calls.
    bool foundTalker = false;
    string talkerId;

    _calls.visitIf(
        // Visitor
        [&foundTalker, &talkerId](const BridgeCall& call) { 
            foundTalker = true;
            talkerId = call.getInputTalkerId();
            return false;
        },
        // Predicate
        [](const BridgeCall& c) { 
            return c.isActive() && c.isNormal() && c.isInputActiveRecently(); 
        }
    );

    // Assuming there is an active talker, assert it on all calls. Note
    // that the talker may be blank if the active call has not been 
    // provided with a talker ID.
    if (foundTalker) {
        _calls.visitIf(
            [&talkerId](BridgeCall& call) { 
                call.setOutputTalkerId(talkerId.c_str());
                return true;
            },
            [](const BridgeCall& s) { return s.isActive() && s.isNormal(); }
        );
    }
}

void Bridge::tenSecTick() {
    // Tick each call
    _visitActiveCalls(
        [](BridgeCall& call) { 
            call.tenSecTick();
            return true;
        }
    );
}

void Bridge::_visitActiveCalls(std::function<bool(BridgeCall&)> cb) {
    _calls.visitIf(cb, ACTIVE_PRED);
}

void Bridge::_visitActiveCalls(std::function<bool(const BridgeCall&)> cb) const {
    _calls.visitIf(cb, ACTIVE_PRED);
}

    }
}