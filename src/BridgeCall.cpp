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
#include <cassert>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <string> 
#include <random>
#include <sstream>

#include "kc1fsz-tools/Common.h"

#include "Message.h"
#include "BridgeCall.h"
#include "Poker.h"
#include "Bridge.h"

// The duration of silence after which the parrot decides the 
// recording should be ended.
#define PARROT_RECORD_TIMEOUT_MS (5000)

// The amount of time that must pass before a call is no longer considered
// a commander (and therefore, will stop receiving status messages)
#define COMMANDER_TIMEOUT_MS (30 * 1000)

// The number of milliseconds of inactivity before a DTMF command tring is 
// considered complete.
#define DTMF_WINDOW_MS (2500)

using namespace std;

namespace kc1fsz {

    namespace amp {

// 1. Seed the random number engine.
// std::random_device provides a non-deterministic source of randomness (hardware entropy) 
// to seed the PRNG differently each time the program runs.
// These things are used in global space because of large stack consumption
static std::random_device rd;
static std::mt19937 gen(rd());
// A vector that holds 2 seconds of pre-made white noise
static std::vector<PCM16Frame> whiteNoise;

void BridgeCall::initializeWhiteNoise() {

    // Generates float values in the range [-1.0, 1.0).
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    float amp = 0.5;
    unsigned ticks = 5 * 1000 / 20;

    int16_t data[BLOCK_SIZE_48K];
    for (unsigned k = 0; k < ticks; k++) {
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
            // We're using a continuous phase here to avoid glitches during 
            // frequency changes
            data[i] = (amp * dist(gen) * 32767.0f);
        }
        // Pass into the output pipeline for transcoding, etc.
        whiteNoise.push_back(PCM16Frame(data, BLOCK_SIZE_48K));
    }
}

BridgeCall::BridgeCall() {
    // The last stage of the BridgeIn pipeline drops the message 
    // into (a) the input staging area in NORMAL mode or (b) the 
    // parrot system in PARROT mode.
    _bridgeIn.setSink([this](const Message& msg) {
        if (_mode == Mode::NORMAL) {
            if (msg.getType() == Message::Type::AUDIO)
                _processNormalAudio(msg);
            else 
                assert(false);
        } else if (_mode == Mode::PARROT) {
            if (msg.getType() == Message::Type::AUDIO)
                _processParrotAudio(msg);
            else 
                assert(false);
        } else if (_mode == Mode::TONE) {
            // No input in tone mode
        } else {
            assert(false);
        }
    });
    // The last stage of the BridgeOut pipeline passes the message
    // out to the sink message bus.
    _bridgeOut.setSink([this](const Message& msg) {
        _sink->consume(msg);
    });
}

void BridgeCall::init(Bridge* bridge, Log* log, Log* traceLog, Clock* clock, 
    MessageConsumer* sink, 
    unsigned bridgeLineId, unsigned bridgeCallId, 
    unsigned ttsLineId, unsigned netTestLineId, const char* netTestBindAddr) {
    _bridge = bridge;
    _log = log;
    _traceLog = traceLog;
    _clock = clock;
    _sink = sink;
    _bridgeLineId = bridgeLineId;
    _bridgeCallId = bridgeCallId;
    _ttsLineId = ttsLineId;
    _netTestLineId = netTestLineId;
    if (netTestBindAddr)
        _netTestBindAddr = netTestBindAddr;
    else 
        _netTestBindAddr = "0.0.0.0";
    _bridgeIn.init(_log, _traceLog, _clock);
    _bridgeOut.init(_log, _clock);
}

void BridgeCall::reset() {

    _active = false;
    _mode = Mode::NORMAL;
    _lineId = 0;  
    _callId = 0; 
    _startMs = 0;
    _lastAudioRxMs = 0;
    _echo = false;
    _sourceAddrValidated = false;
    _permanent = false;
    
    _bridgeIn.reset();
    _bridgeOut.reset();

    _toneActive = false;
    _toneOmega = 0;
    _tonePhi = 0;
    _toneLevel = 0;

    _captureQueue = std::queue<PCM16Frame>();
    _captureQueueDepth = 0;
    _playQueue = std::queue<PCM16Frame>();
    _parrotState = ParrotState::NONE;
    _parrotStateStartMs = 0;
    _lastUnkeyProcessedMs = 0;

    _stageInSet = false;
    _stageOutSet = false;
    _lastCycleGeneratedOutput = false;

    _dtmfAccumulator.clear();
    _lastDtmfRxMs = 0;
    _linkReport.clear();
    _linkReportChangeMs = 0;
    _talkerId.clear();
    _talkerIdChangeMs = 0;
    _outputTalkerId.clear();
    _keyedNode.clear();
    _keyedNodeChangeMs = 0;

    _rx0Db = -99;
    _tx0Db = -99;
    _rx1Db = -99;
    _tx1Db = -99;
}

void BridgeCall::setup(unsigned lineId, unsigned callId, uint32_t startMs, CODECType codec,
    bool bypassJitterBuffer, bool echo, bool sourceAddrValidated, Mode initialMode,
    const char* remoteNodeNumber, bool permanent, bool useKerchunkFilter,
    unsigned kerchunkFilterEvaluationIntervalMs) {

    _active = true;
    _lineId = lineId;  
    _callId = callId; 
    _remoteNodeNumber = remoteNodeNumber;

    _startMs = startMs;
    _lastAudioRxMs = 0;

    _bridgeIn.setCodec(codec);
    _bridgeIn.setBypassJitterBuffer(bypassJitterBuffer);
    _bridgeIn.setStartTime(startMs);
    _bridgeOut.setCodec(codec);

    _echo = echo;
    _sourceAddrValidated = sourceAddrValidated;
    _permanent = permanent;

    _bridgeIn.setKerchunkFilterEnabled(useKerchunkFilter);
    _bridgeIn.setKerchunkFilterEvaluationIntervalMs(kerchunkFilterEvaluationIntervalMs);

    if (initialMode == Mode::PARROT)
        _enterParrotMode();
    else 
        _mode = initialMode;
}

void BridgeCall::_enterParrotMode() {
    _mode = Mode::PARROT;
    _parrotState = ParrotState::CONNECTED;
    _parrotStateStartMs = _clock->time();
}

bool BridgeCall::isRecentCommander() const {
    return _clock->isInWindow(_lastDtmfRxMs, COMMANDER_TIMEOUT_MS);
}

uint64_t BridgeCall::getStatusDocStampMs() const {
    // The idea here is to find the maximum time that anything 
    // has changed.
    uint64_t ms = _bridgeIn.getActiveStatusChangedMs();
    ms = max(ms, _linkReportChangeMs);
    ms = max(ms, _talkerIdChangeMs);
    ms = max(ms, _keyedNodeChangeMs);
    return ms;
}

json BridgeCall::getStatusDoc() const {

    json o2;

    // Static
    o2["lineId"] = _lineId;
    o2["callId"] = _callId;
    o2["remoteNode"] = _remoteNodeNumber;
    o2["permanent"] = _permanent;

    // Dynamic
    o2["rxActive"] = _bridgeIn.isActiveRecently();
    o2["talkerid"] = _talkerId;

    // Build the connection list
    auto b = json::array();
    if (!_linkReport.empty()) {
        unsigned bLen = 0;
        unsigned bLimit = 30;
        // Link report is comma-delimited
        std::istringstream tokenStream(_linkReport);
        string token;
        while (std::getline(tokenStream, token, ',')) {
            trim(token);
            if (token.empty())
                continue;
            // Check to see if limit was exceeded
            if (bLen == bLimit) {
                o2["connectionsLimited"] = true;
                break;
            }
            // Clear off the prefixes. Two important things to note here:
            // 1. This document mentions three valid prefixes: 
            //    https://wiki.allstarlink.org/wiki/IAX_Text_Protocol#Status
            // 2. When connected to the ECR I noticed this link line:
            //    L T521191,T45192,T64746,T633233,KE8VKY,T457030,T56999,T618282,
            //    T648770,TK4GDS,T67151,T29734,T29719,T63005
            //    (Notice that some of these things appear to be callsigns)
            if (token.starts_with("T") || token.starts_with("R") ||
                token.starts_with("C")) {
                token = token.substr(1);
            }
            // Add to list
            json o3;
            o3["node"] = token;
            o3["keyed"] = (token == _keyedNode);
            b.push_back(o3);
            bLen++;
        }
    }
    o2["connections"] = b;

    return o2;
}

bool BridgeCall::hasAudioLevels() const {
    // At the moment, the radio is the hard-coded source of audio level
    // information.
    // #### TODO: Improve this logic
    return _remoteNodeNumber.starts_with("radio");
}

json BridgeCall::getLevelsDoc() const {
    json o;
    o["usb-rx-meter"] = _rx0Db;
    o["usb-tx-meter"] = _tx0Db;
    o["net-rx-meter"] = _rx1Db;
    o["net-tx-meter"] = _tx1Db;
    return o;
}

void BridgeCall::consume(const Message& frame) {
    if (frame.getType() == Message::Type::TTS_AUDIO ||
        frame.getType() == Message::Type::TTS_END) {
        _processTTSAudio(frame);
    } 
    else if (frame.getType() == Message::Type::NET_DIAG_1_RES) {
        assert(frame.size() == sizeof(_netTestResult));
        memcpy(&_netTestResult, frame.body(), sizeof(_netTestResult));
        _log->info("Got a network diagnostic response %d", _netTestResult.code);
        _parrotState = ParrotState::GREETING_0;
        _parrotStateStartMs = _clock->time();
    }
    else if (frame.isSignal(Message::SignalType::LINK_REPORT)) {
        string r((const char*)frame.body(), frame.size());
        // Only update the report if it's different from last time
        if (_linkReport != r) {
            _linkReport = r;
            _linkReportChangeMs = _clock->timeMs();
        }
    } 
    else if (frame.isSignal(Message::SignalType::CALL_TALKERID)) {
        string r((const char*)frame.body(), frame.size());
        // Only update the talker ID if it's different from last time
        // and if there is active audio being received on this call.
        if (_talkerId != r && _bridgeIn.isActiveRecently()) {
            _talkerId = r;
            _talkerIdChangeMs = _clock->timeMs();
            _log->info("Input talker ID from %s set %s", 
                _remoteNodeNumber.c_str(), _talkerId.c_str());
        }
    } 
    else if (frame.isSignal(Message::SignalType::DTMF_PRESS)) {

        assert(frame.size() == sizeof(PayloadDtmfPress));
        PayloadDtmfPress* payload = (PayloadDtmfPress*)frame.body();
        char symbol = (char)payload->symbol;

        if (_mode == Mode::PARROT) {
            if (symbol == '1') {
                _log->info("Starting sweep");
                _loadSweep(_playQueue);
                _parrotState = ParrotState::PLAYING_AFTER_RECORD;            
            } else if (symbol == '2') {
                _log->info("Starting tone");
                // A 5 second tone at 440 Hz
                _loadCw(0.5, 440, 50 * 5, _playQueue);
                _parrotState = ParrotState::PLAYING_AFTER_RECORD;            
            } else if (symbol == '3') {
                _log->info("Generating white noise");
                for (auto it = whiteNoise.begin(); it != whiteNoise.end(); it++)
                    _playQueue.push(PCM16Frame(*it));
                _log->info("Done");
                _parrotState = ParrotState::PLAYING_AFTER_RECORD;            
            }
        } else {
            if (symbol == '*') 
                _dtmfAccumulator.clear();
            _dtmfAccumulator += symbol;
            _lastDtmfRxMs = _clock->timeMs();
        }
    } else if (frame.isVoice() || frame.isSignal(Message::SignalType::RADIO_UNKEY)) {
        _bridgeIn.consume(frame);       
    }
    else if (frame.isSignal(Message::SignalType::CALL_LEVELS)) {
        assert(frame.size() == sizeof(PayloadCallLevels));
        const PayloadCallLevels* payload = (const PayloadCallLevels*)frame.body();
        _rx0Db = payload->rx0Db;
        _tx0Db = payload->tx0Db;
        _rx1Db = payload->rx1Db;
        _tx1Db = payload->tx1Db;
    }
}

void BridgeCall::produceOutput(uint32_t tickMs) {    
    
    // IMPORTANT: The final output for a call is the combination
    // of any synthetic material on the play queue and whether 
    // is coming it from the conference.
    //
    // This should be the ONLY place in this class where a frame
    // of audio is passed to the _bridgeOut.
    // 
    // This is also the place where an UNKEY event is requested on
    // the trailing edge of contributed audio.

    int16_t output[BLOCK_SIZE_48K];
    int16_t sources = 0;

    // If there is anything in the play queue then contribute it to 
    // the final output.
    if (!_playQueue.empty()) {
        sources++;
        assert(_playQueue.front().size() == BLOCK_SIZE_48K);
        memcpy(output, _playQueue.front().data(), BLOCK_SIZE_48K * 2);
        _playQueue.pop();
    } else {
        memset(output, 0, BLOCK_SIZE_48K * 2);
    }

    // If we are in conference mode then mix in the conference output
    if (_mode == Mode::NORMAL && _stageOutSet) {
        sources++;
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++)
            output[i] = (output[i] / sources) + (_stageOut[i] / sources);
        // Clear this flag so that we are ready for the next iteration
        _stageOutSet = false;
    }

    // If there was any audio contributed then make a message and send it
    if (sources > 0) {
        //if (!_lastCycleGeneratedOutput)
        //    _log->info("Leading edge of audio detected");
        // Convert the PCM16 data into LE mode as defined by the CODEC.
        uint8_t pcm48k[BLOCK_SIZE_48K * 2];
        Transcoder_SLIN_48K transcoder;
        transcoder.encode(output, BLOCK_SIZE_48K, pcm48k, BLOCK_SIZE_48K * 2);
        // #### TODO: DO TIMES MATTER HERE?
        Message msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K, 
            BLOCK_SIZE_48K * 2, pcm48k, 0, tickMs);
        msg.setSource(LINE_ID, CALL_ID);
        msg.setDest(_lineId, _callId);
        _bridgeOut.consume(msg);

        _lastCycleGeneratedOutput = true;
    }
    // If there was no audio contributed then consider a UNKEY event on the
    // trailing edge.
    else {
        if (_lastCycleGeneratedOutput) {
            Message msg(Message::Type::SIGNAL, Message::SignalType::RADIO_UNKEY_GEN, 
                0, 0, 0, tickMs);
            msg.setSource(LINE_ID, CALL_ID);
            msg.setDest(_lineId, _callId);
            _bridgeOut.consume(msg);
        }
        _lastCycleGeneratedOutput = false;
    }
}

void BridgeCall::audioRateTick(uint32_t tickMs) {

    _bridgeIn.audioRateTick(tickMs);

    if (_mode == Mode::TONE) {
        _toneAudioRateTick(tickMs);
    } else if (_mode == Mode::PARROT) {
        _parrotAudioRateTick(tickMs);
    }
}

void BridgeCall::oneSecTick() {
    // Has a full DMTF sequence been collected?
    if (!_dtmfAccumulator.empty() && 
         _clock->isPastWindow(_lastDtmfRxMs, DTMF_WINDOW_MS)) {
        _processDtmfCommand(_dtmfAccumulator);
        _dtmfAccumulator.clear();
    }
    // Refresh the talker as long as there is active audio being 
    // transmitted.
    if (_bridgeOut.isActiveRecently())
        _signalTalker();
}

void BridgeCall::setOutputTalkerId(const char* talkerId) {
    if (_outputTalkerId != talkerId) {
        _outputTalkerId = talkerId;
        _log->info("Output talker ID to %s set to %s", 
            _remoteNodeNumber.c_str(), _outputTalkerId.c_str());
    }
}

void BridgeCall::_signalTalker() {
    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_TALKERID, 
        // Include the null termination
        _outputTalkerId.length() + 1, (const uint8_t*)_outputTalkerId.c_str(), 
        0, 0);
    msg.setSource(LINE_ID, CALL_ID);
    msg.setDest(_lineId, _callId);
    _bridgeOut.consume(msg);
}

void BridgeCall::_processTTSAudio(const Message& frame) {

    // Anything that comes back from TTS goes to the play queue without
    // regard for state/mode.
    if (frame.getType() == Message::Type::TTS_AUDIO)
        _loadAudioMessage(frame, _playQueue);

    if (_mode == Mode::PARROT) 
        _processParrotTTSAudio(frame);
}

void BridgeCall::_processDtmfCommand(const string& cmd) {

    // *3xxxx connect to node
    if (cmd.starts_with("*3")) {
        // Parse off the node number
        string targetNode = cmd.substr(2);

        _log->info("Request to call %s -> %s", 
            _bridge->_nodeNumber.c_str(), targetNode.c_str());

        // Create a signal and publish it to the LineIAX2 for processing
        PayloadCall payload;
        strcpyLimited(payload.localNumber, _bridge->_nodeNumber.c_str(), sizeof(payload.localNumber));
        strcpyLimited(payload.targetNumber, targetNode.c_str(), sizeof(payload.targetNumber));
        Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_NODE, 
            sizeof(payload), (const uint8_t*)&payload, 0, 0);
        msg.setDest(_bridge->_networkDestLineId, Message::UNKNOWN_CALL_ID);
        _bridge->_bus.consume(msg);
    }
    // *71 drop all outbound calls
    else if (cmd.starts_with("*71")) {

        _log->info("Request to disconnect all");

        // Create a signal and publish it to the LineIAX2 for processing
        Message msg(Message::Type::SIGNAL, Message::SignalType::DROP_ALL_CALLS_OUTBOUND, 
            0, 0, 0, 0);
        msg.setDest(_bridge->_networkDestLineId, Message::UNKNOWN_CALL_ID);
        _bridge->_bus.consume(msg);
    }
    // *70 status
    else if (cmd.starts_with("*70")) {

        _log->info("Request for status");

        // Get a list of the nodes connected
        vector<string> nodes = _bridge->getConnectedNodes();
        string prompt = "Connected to ";
        bool first = true;
        for (auto n : nodes) {
            if (!first)
                prompt += " and ";
            prompt += Bridge::addSpaces(n.c_str());
            first = false;
        }
        prompt += ".";
        requestTTS(prompt.c_str());
    }
    else if (cmd == "*76") {
        _enterParrotMode();
    }
    else {
        _log->info("Unrecognized DTMF command ignored %s", cmd.c_str());
    }
}

// ===== Conference Mode Related ===============================================

void BridgeCall::_processNormalAudio(const Message& msg) {   
    assert(msg.getType() == Message::Type::AUDIO);
    assert(msg.size() == BLOCK_SIZE_48K * 2);
    assert(msg.getFormat() == CODECType::IAX2_CODEC_SLIN_48K);
    const uint8_t* p = msg.body();
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++, p += 2)
        _stageIn[i] = unpack_int16_le(p);
    _stageInSet = true;
}

/**
 * The Bridge calls this function to collect this call's contribution to the 
 * conference audio. 
 */
void BridgeCall::extractInputAudio(int16_t* pcmBlock, unsigned blockSize, 
    float scale, uint32_t tickMs) {
    assert(blockSize == BLOCK_SIZE_48K);
    if (_stageInSet) {
        for (unsigned i = 0; i < blockSize; i++)
            pcmBlock[i] += scale * (float)_stageIn[i];
    }
}

void BridgeCall::clearInputAudio() {
    _stageInSet = false;
}

/**
 * The bridge calls this function to set the final output audio for this call.
 * Takes 48K PCM and passes it into the BridgeOut pipeline for transcoding, etc.
 */
void BridgeCall::setConferenceOutput(const int16_t* pcm48k, unsigned blockSize, uint32_t tickMs) {
    assert(blockSize == BLOCK_SIZE_48K);
    memcpy(_stageOut, pcm48k, BLOCK_SIZE_48K * 2);
    _stageOutSet = true;
}

// ===== Tone Mode Related ====================================================

void BridgeCall::_toneAudioRateTick(uint32_t tickMs) {
    if (_toneActive) {
        // Make a tone at 48K
        int16_t data[BLOCK_SIZE_48K];
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
            data[i] = (_toneLevel * cos(_tonePhi)) * 32767.0f;
            _tonePhi += _toneOmega;
        }
        _tonePhi = fmod(_tonePhi, 2.0f * 3.14159f);
        // Queue a tick's worth of output
        _playQueue.push(PCM16Frame(data, BLOCK_SIZE_48K));
    }
}

// ===== Parrot Related =======================================================

/**
 * This function will be called by the input adaptor after the PLC and 
 * transcoding has happened.
 */
void BridgeCall::_processParrotAudio(const Message& msg) { 

    float rms = 0;
    
    // At this point all interpolation is finished and the audio is in
    // the common bus format.
    assert(msg.getType() == Message::AUDIO);
    assert(msg.size() == BLOCK_SIZE_48K * 2);
    assert(msg.getFormat() == CODECType::IAX2_CODEC_SLIN_48K);

    // Convert back to native PCM16
    int16_t pcm48k[BLOCK_SIZE_48K];
    Transcoder_SLIN_48K transcoder;
    transcoder.decode(msg.body(), BLOCK_SIZE_48K * 2, pcm48k, BLOCK_SIZE_48K);                

    // Compute the power in the frame
    float pcm48k_2[BLOCK_SIZE_48K];
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++)
        pcm48k_2[i] = pcm48k[i] / 32767.0;
    arm_rms_f32(pcm48k_2, BLOCK_SIZE_48K, &rms);

    bool vad = rms > 0.005;

    if (vad)
        _lastAudioRxMs = _clock->time();

    if (_parrotState == ParrotState::WAITING_FOR_RECORD)  {
        if (vad) {
            _log->info("Record start");

            _parrotState = ParrotState::RECORDING;
            // Synchronize with the last unkey in case we missed one
            _lastUnkeyProcessedMs = _bridgeIn.getLastUnkeyMs();
            _captureQueue = std::queue<PCM16Frame>(); 
            _captureQueueDepth = 0;

            _captureQueue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
            _captureQueueDepth++;
        }
    } 
    else if (_parrotState == ParrotState::RECORDING) {
        // Limit the amount of sound that can be captured
        if (_captureQueueDepth < 1500) {
            _captureQueue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
            _captureQueueDepth++;
        }
        if (_echo)
            _playQueue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
    } 
}

void BridgeCall::_parrotAudioRateTick(uint32_t tickMs) {

    // General timeout
    if (_clock->isPast(_startMs + SESSION_TIMEOUT_MS)) {
        _log->info("Timing out call %u/%d", _lineId, _callId);
        _parrotState = ParrotState::TIMEDOUT;
        Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_TERMINATE, 
            0, 0, 0, tickMs * 1000);
        msg.setSource(LINE_ID, CALL_ID);
        msg.setDest(_lineId, _callId);
        _bridgeOut.consume(msg);
        reset();
    }
    else if (_parrotState == ParrotState::CONNECTED) {

        setOutputTalkerId(PARROT_TALKER_ID);

        // Launch the network test for this new connection
        Poker::Request req;
        strcpyLimited(req.bindAddr, _netTestBindAddr.c_str(), sizeof(_netTestBindAddr));
        strcpyLimited(req.nodeNumber, _remoteNodeNumber.c_str(), sizeof(req.nodeNumber));
        req.timeoutMs = 250;

        Message msg(Message::Type::NET_DIAG_1_REQ, 0, 
            sizeof(req), (const uint8_t*)&req, 0, 0);
        msg.setSource(_bridgeLineId, _bridgeCallId);
        msg.setDest(_netTestLineId, Message::BROADCAST);
        _sink->consume(msg);     

        _netTestResult.code = -99;
        _parrotState = ParrotState::WAITING_FOR_NET_TEST;
        _parrotStateStartMs = _clock->time();
    }
    else if (_parrotState == ParrotState::WAITING_FOR_NET_TEST) {
        // Check for timeout
        if (_clock->isPast(_parrotStateStartMs + 5000))
            _parrotState = ParrotState::GREETING_0;
    } 
    else if (_parrotState == ParrotState::GREETING_0) {
        // We only start after a bit of silence to address any initial
        // clicks or pops on key.
        if (_clock->isPast(_parrotStateStartMs + 250)) {
            // Create the speech that will be sent to the caller
            string prompt;
            prompt = "Parrot connected. Stand by.";
            requestTTS(prompt.c_str());
            // Wait for the TTS_END signal
            _parrotState = ParrotState::TTS_GREETING_0;
            _parrotStateStartMs = _clock->time();
        }
    }
    else if (_parrotState == ParrotState::PLAYING_GREETING_0) {
        if (_playQueue.empty()) {
            _parrotState = ParrotState::GREETING_1;
            _parrotStateStartMs = _clock->time();
        }
    }
    else if (_parrotState == ParrotState::GREETING_1) {
        // Per Jason N8EI, we only start after a bit of silence to let 
        // the telemetry pass.
        if (_clock->isPast(_parrotStateStartMs + 3000)) {
            string prompt;
            if (!_sourceAddrValidated) 
                prompt += "Node is unregistered. ";
            else {
                if (_netTestResult.code == 0) {
                    prompt += "Network test succeeded. ";
                    char msg[64];
                    snprintf(msg, 64, "Ping time %d milliseconds. ", 
                        _netTestResult.pokeTimeMs);
                    prompt += msg;
                }
                else if (_netTestResult.code == -99) {
                    // Do nothing
                } else if (_netTestResult.code <= -8) 
                    prompt += "Your node is unreachable. ";
            }

            if (_bridgeIn.getCodec() == CODECType::IAX2_CODEC_G711_ULAW) 
                prompt += "CODEC is 8K mulaw. ";
            else if (_bridgeIn.getCodec() == CODECType::IAX2_CODEC_SLIN_16K) 
                prompt += "CODEC is 16K linear. ";

            prompt += "Ready to record.";

            // Queue a TTS request
            requestTTS(prompt.c_str());
            // Waiting for TTS_END
            _parrotState = ParrotState::TTS_GREETING_1;
            _parrotStateStartMs = _clock->time();
        }
    }
    else if (_parrotState == ParrotState::PLAYING_GREETING_1) {
        if (_playQueue.empty()) {
            _parrotState = ParrotState::WAITING_FOR_RECORD;
            _parrotStateStartMs = _clock->time();
        }
    }
    else if (_parrotState == ParrotState::RECORDING) {
        if (_clock->isPast(_lastAudioRxMs + PARROT_RECORD_TIMEOUT_MS)) {
            _log->info("Record end (Long silence)");
            _parrotState = ParrotState::PAUSE_AFTER_RECORD;
            _parrotStateStartMs = _clock->time();
        }
        else if (_bridgeIn.getLastUnkeyMs() > _lastUnkeyProcessedMs &&
            _clock->isPast(_bridgeIn.getLastUnkeyMs() + 250)) {
            _lastUnkeyProcessedMs = _bridgeIn.getLastUnkeyMs();
            _log->info("Record end (UNKEY)");
            _parrotState = ParrotState::PAUSE_AFTER_RECORD;
            _parrotStateStartMs = _clock->time();
        }
    } 
    else if (_parrotState == ParrotState::PAUSE_AFTER_RECORD) {
        if (_clock->isPast(_parrotStateStartMs + 750)) {

            // Make a vector of the capture queue for analysis
            std::vector<PCM16Frame> captureCopy;
            while (!_captureQueue.empty()) {
                captureCopy.push_back(_captureQueue.front());
                _captureQueue.pop();
            }

            // Analyze the recording for relevant stats
            float peakPower, avgPower;
            _analyzeRecording(captureCopy, &peakPower, &avgPower);

            // Re-queue the captured frames
            for (auto it = captureCopy.begin(); it != captureCopy.end(); it++)
                _captureQueue.push((*it));

            // Capture the talker that was active during the recording
            _recordedTalkerId = _talkerId;

            // Create the speech that will be sent to the caller
            string prompt;
            char sp[64];
           
            // #### TODO: DO A BETTER JOB ON THE CLIPPING CASE

            int peakPowerInt = std::round(peakPower);
            if (peakPowerInt < -40) {
                snprintf(sp, 64, "Peak is less than minus 40db");
            } else if (peakPowerInt < 0) {                
                snprintf(sp, 64, "Peak is minus %ddb", abs(peakPowerInt));
            } else {
                snprintf(sp, 64, "Peak is 0db");
            }
            prompt += sp;
            prompt += ", ";

            int avgPowerInt = std::round(avgPower);
            if (avgPower < -40) {
                snprintf(sp, 64, "Average is less than minus 40db");
            } else if (avgPower < 0) {
                snprintf(sp, 64, "Average is minus %ddb", abs(avgPowerInt));
            } else {
                snprintf(sp, 64, "Average is 0db");
            }
            prompt += sp;
            prompt += ". ";

            // Now add some subjective commentary (CONTROVERSIAL!)
            if (_bridge->_parrotLevelThresholds.size() >= 4) {
                prompt += "Level is ";
                if (peakPowerInt >= _bridge->_parrotLevelThresholds.at(0))
                    prompt += "very high";
                if (peakPowerInt >= _bridge->_parrotLevelThresholds.at(1))
                    prompt += "high";
                else if (peakPowerInt >= _bridge->_parrotLevelThresholds.at(2)) 
                    prompt += "good";
                else if (peakPowerInt >= _bridge->_parrotLevelThresholds.at(3)) 
                    prompt += "low";
                else 
                    prompt += "very low";
                prompt += ". ";
            }
            prompt += "Playback.";

            // Queue a request for TTS
            requestTTS(prompt.c_str());

            // Assert the talker
            setOutputTalkerId(PARROT_TALKER_ID);

            // Get into the state waiting for the TTS to complete
            _parrotState = ParrotState::TTS_AFTER_RECORD;
            _parrotStateStartMs = _clock->time();
        }
    }
    else if (_parrotState == ParrotState::PLAYING_AFTER_RECORD) {
        if (_playQueue.empty()) {
            _log->info("Play end");
            // TODO
            _parrotState = ParrotState::WAITING_FOR_RECORD;
            _parrotStateStartMs = _clock->time();
        }
    }
}

// These are the places where we are waiting for the TTS to complete
void BridgeCall::_processParrotTTSAudio(const Message& frame) {
    if (_parrotState == ParrotState::TTS_GREETING_0 ||
        _parrotState == ParrotState::TTS_GREETING_1) {
        if (frame.getType() == Message::Type::TTS_END) {
            _log->info("Greeting start");
            if (_parrotState == ParrotState::TTS_GREETING_0)
                _parrotState = ParrotState::PLAYING_GREETING_0;
            else if (_parrotState == ParrotState::TTS_GREETING_1)
                _parrotState = ParrotState::PLAYING_GREETING_1;
            _parrotStateStartMs = _clock->time();
        }        
    }
    else if (_parrotState == ParrotState::TTS_AFTER_RECORD) {
        if (frame.getType() == Message::Type::TTS_END) {

            _loadSilence(25, _playQueue);

            // Move the recording to the end of the playback queue
            while (!_captureQueue.empty()) {
                _playQueue.push(_captureQueue.front());
                _captureQueue.pop();
            }

            // Assert the talker (echo)
            setOutputTalkerId(_recordedTalkerId.c_str());

            _parrotState = ParrotState::PLAYING_AFTER_RECORD;
            _parrotStateStartMs = _clock->time();

            _log->info("Playback start");
        }        
    }
}

void BridgeCall::requestTTS(const char* prompt) {
    Message req(Message::Type::TTS_REQ, 0, strlen(prompt), (const uint8_t*)prompt, 
        0, 0);
    req.setSource(_bridgeLineId, _bridgeCallId);
    req.setDest(_ttsLineId, Message::BROADCAST);
    _sink->consume(req);
}

void BridgeCall::_loadAudioMessage(const Message& msg, std::queue<PCM16Frame>& queue) const {    

    assert(msg.getType() == Message::Type::TTS_AUDIO);
    assert(msg.size() == BLOCK_SIZE_48K * sizeof(int16_t));

    int16_t pcm48k[BLOCK_SIZE_48K];
    const uint8_t* buffer = msg.body();
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
        pcm48k[i] = unpack_int16_le((const uint8_t*)buffer);
        buffer += 2;
    }
    queue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
}

void BridgeCall::_loadSilence(unsigned ticks, std::queue<PCM16Frame>& queue) const {    
    int16_t pcm48k[BLOCK_SIZE_48K];
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++)
        pcm48k[i] = 0;
    for (unsigned i = 0; i < ticks; i++)
        queue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
}

void BridgeCall::_loadAudio(const std::vector<PCM16Frame>& audio, std::queue<PCM16Frame>& queue) const {
    for (auto it = audio.begin(); it != audio.end(); it++) 
        queue.push(*it);
}

void BridgeCall::_loadCw(float amp, float hz, unsigned ticks, std::queue<PCM16Frame>& queue) {
    float toneOmega = 2.0f * 3.14159f * hz / 48000.0f;
    int16_t data[BLOCK_SIZE_48K];
    for (unsigned k = 0; k < ticks; k++) {
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
            // We're using a continuous phase here to avoid glitches during 
            // frequency changes
            data[i] = (amp * cos(_tonePhi)) * 32767.0f;
            _tonePhi += toneOmega;
            _tonePhi = fmod(_tonePhi, 2.0f * 3.14159f);
        }
        // Pass into the output pipeline for transcoding, etc.
        queue.push(PCM16Frame(data, BLOCK_SIZE_48K));
    }
}

void BridgeCall::_loadWhite(float amp, unsigned ticks, std::queue<PCM16Frame>& queue) const {

    // Generates float values in the range [-1.0, 1.0).
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    int16_t data[BLOCK_SIZE_48K];
    for (unsigned k = 0; k < ticks; k++) {
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
            // We're using a continuous phase here to avoid glitches during 
            // frequency changes
            data[i] = (amp * dist(gen) * 32767.0f);
        }
        // Pass into the output pipeline for transcoding, etc.
        queue.push(PCM16Frame(data, BLOCK_SIZE_48K));
    }
}

void BridgeCall::_loadSweep(std::queue<PCM16Frame>& queue) {    
    // Alternating intro
    for (unsigned i = 0; i < 8; i++) {
        _loadCw(0.5, 400, 2, queue);
        _loadCw(0.5, 800, 2, queue);
    }
    unsigned upperHz = 4000;
    if (_bridgeIn.getCodec() == CODECType::IAX2_CODEC_SLIN_16K) 
        upperHz = 8000;
    // Sweep
    for (unsigned f = 0; f < upperHz; f += 100)
        _loadCw(0.5, f, 5, queue);
}

void BridgeCall::_analyzeRecording(const std::vector<PCM16Frame>& audio, 
    float* peakPower, float* avgPower) {

    unsigned blockSize = 160 * 50;

    // Perform the audio analysis on the recording using the David NR9V method. 
    float peak = 0;
    float avgSquareBlock = 0;
    unsigned sampleCountBlock = 0;
    float peakAvgSquare = 0;

    unsigned frameCount = audio.size();

    // Ignore the first 300ms of the recording to avoid distortion due to pops/clips
    unsigned startI = 300 / 20;
    // Per Patrick Perdue (N2DYI), we ignore the last 300ms of the recording to avoid
    // influence of tail.
    unsigned endIgnoreCount = 300 / 20;
    unsigned endI = 0;
    if (frameCount > endIgnoreCount)
        endI = frameCount - endIgnoreCount;
    else 
        endI = 0;
    // Look for the case where there is no audio left to analyze
    if (endI <= startI) {
        *peakPower = -96.0;
        *avgPower = -96.0;
        _log->info("Recording too short to analyze");
        return;
    }

    for (unsigned j = startI; j < endI; j++) {
        assert(audio.at(j).size() == BLOCK_SIZE_48K);
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i += 6) {
            
            int16_t sample = abs(audio.at(j).data()[i]);

            if (sample > peak) {
                peak = sample;
            }

            avgSquareBlock += (float)sample * (float)sample;
            sampleCountBlock++;

            // On every complete block we stop to see if we have a new peak average
            if (sampleCountBlock == blockSize) {
                avgSquareBlock /= (float)sampleCountBlock;
                if (avgSquareBlock > peakAvgSquare)
                    peakAvgSquare = avgSquareBlock;
                sampleCountBlock = 0;
                avgSquareBlock = 0;
            }
        }
    }
    
    if (peak == 0) {
        *peakPower = -96.0;
    } else {
        *peakPower = 10.0 * log10((peak * peak) / (32767.0f * 32767.0f));
    }

    if (peakAvgSquare == 0) {
        *avgPower = -96.0;
    } else {
        *avgPower = 10.0 * log10(peakAvgSquare / (32767.0f * 32767.0f));
    }
}

    }
}
