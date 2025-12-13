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
#include <cstring>
#include <iostream>
#include <algorithm> 
#include <fstream>
#include <cmath>

#include "arm_math.h"

#include "itu-g711-codec/codec.h"
#include "kc1fsz-tools/Clock.h"
#include "kc1fsz-tools/Log.h"
#include "amp/Resampler.h"

#include "IAX2Util.h"
#include "Message.h"
#include "NodeParrot.h"
#include "Transcoder_SLIN_48K.h"

using namespace std;

namespace kc1fsz {

static const unsigned MAX_SESSIONS = 32;

NodeParrot::NodeParrot(Log& log, Clock& clock, MessageConsumer& bus)
:   _log(log),
    _clock(clock),
    _bus(bus),
    _sessions(_sessionsStore, MAX_SESSIONS) {
}

void NodeParrot::reset() {
    _sessions.visitAll(RESET_VISITOR);
}

void NodeParrot::consume(const Message& msg) { 

    if (msg.getType() == Message::SIGNAL && 
        msg.getFormat() == Message::SignalType::CALL_START) {
        
        // Remove old/existing session for this call (if any)
        _sessions.visitIf(
            // Visitor
            RESET_VISITOR,
            // Predicate
            [msg](const Session& s) { return s.belongsTo(msg); });
        // Add new session for this call
        int newIndex = _sessions.firstIndex([](const Session& s) { return !s.active; });
        if (newIndex == -1) {
            _log.info("Max sessions, rejecting call %d", msg.getSourceCallId());
            // #### TODO: NEED TO TEST THIS AFTER RACE CONDITION IS RESOLVED
            Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_TERMINATE, 0, 0,
                _clock.timeUs());
            msg.setDest(msg.getSourceBusId(), msg.getSourceCallId());
            _bus.consume(msg);
        }
        else {                
            PayloadCallStart payload;
            assert(msg.size() == sizeof(payload));
            memcpy(&payload, msg.raw(), sizeof(payload));

            _log.info("Call started %d codec %X", msg.getSourceCallId(), payload.codec);

            Session& s = _sessions.at(newIndex);
            s.active = true;
            s.lineId = msg.getSourceBusId();
            s.callId = msg.getSourceCallId();
            s.callStartTime = _clock.time();
            s.state = State::CONNECTED;
            s.stateStartTime = _clock.time();
            s.adaptorIn.setCodec(payload.codec);
            s.adaptorIn.setSink(
                // This handles Messages coming in from the outside via consume()
                // but AFTER the de-jitter, PLC, and transcoding has happened.
                [this](const Message& msg) {
                    this->_sessions.visitIf(
                        // Visitor
                        [this, msg](Session& s) { 
                            if (msg.getType() == Message::Type::AUDIO) 
                                this->_consumeAudioInSession(s, msg);
                            else if (msg.getType() == Message::Type::SIGNAL)
                                this->_consumeSignalInSession(s, msg);
                            // Can stop after first hit
                            return false;
                        },
                        // Predicate
                        [msg](const Session& s) { return s.belongsTo(msg); }
                    );
                }
            );
            s.adaptorOut.setCodec(payload.codec);
            // This handles frames that need to go out (i.e. playback)
            s.adaptorOut.setSink([this](const Message& msg) {
                this->_bus.consume(msg);
            });
        }
    }
    else if (msg.getType() == Message::SIGNAL && 
             msg.getFormat() == Message::SignalType::CALL_END) {

        _log.info("Call ended %d", msg.getSourceCallId());

        _sessions.visitIf(
            // Visitor
            RESET_VISITOR,
            // Predicate
            [msg](const Session& s) { return s.belongsTo(msg); });
    }
    // These are all the message types that get passed directly to the call's
    // input adaptor for processing.
    else if (msg.getType() == Message::AUDIO || 
             msg.getType() == Message::AUDIO_INTERPOLATE || 
             (msg.getType() == Message::SIGNAL && 
              msg.getFormat() == Message::SignalType::RADIO_UNKEY)) {

        _sessions.visitIf(
            // Visitor
            [msg](Session& s) { 
                // Pass to the input adaptor so that PLC and transcoding can happen
                s.adaptorIn.consume(msg);
                return false;
            },
            // Predicate
            [msg](const Session& s) { return s.belongsTo(msg); }
        );
    }
}

/**
 * This function is called when a signal is received from the input
 * adaptor.
 */
void NodeParrot::_consumeSignalInSession(Session& s, const Message& msg) { 
    if (msg.getType() == Message::SIGNAL &&
        msg.getFormat() == Message::SignalType::RADIO_UNKEY) {
        if (s.state == State::RECORDING) {
            log.info("Record end (UNKEY)");
            s.state = State::PAUSE_AFTER_RECORD;
            s.stateStartTime = clock.time();
        }
    }
}

/**
 * This function will be called by the input adaptor after the PLC and 
 * transcoding has happened.
 */
void NodeParrot::_consumeAudioInSession(Session& s, const Message& msg) { 

    float rms = 0;
    
    // At this point all interpolation is finished and the audio is in
    // the common bus format.
    assert(msg.getType() == Message::AUDIO);
    assert(msg.size() == BLOCK_SIZE_48K * 2);
    assert(msg.getFormat() == CODECType::IAX2_CODEC_SLIN_48K);

    // Convert back to native PCM16
    int16_t pcm48k[BLOCK_SIZE_48K];
    Transcoder_SLIN_48K transcoder;
    transcoder.decode(msg.raw(), BLOCK_SIZE_48K * 2, pcm48k, BLOCK_SIZE_48K);                

    // Compute the power in the frame
    float pcm48k_2[BLOCK_SIZE_48K];
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++)
        pcm48k_2[i] = pcm48k[i] / 32767.0;
    arm_rms_f32(pcm48k_2, BLOCK_SIZE_48K, &rms);

    bool vad = rms > 0.005;

    if (vad)
        s.lastAudioTime = _clock.time();

    if (s.state == State::WAITING_FOR_RECORD)  {
        if (vad) {
            _log.info("Record start");
            s.state = State::RECORDING;
            s.playQueue = std::queue<PCM16Frame>(); 
            s.playQueueDepth = 0;

            // Load up the pre-playback audio
            _loadAudioFile("../media/playback-8k.pcm", s.playQueue);
            _loadSilence(25, s.playQueue);

            s.playQueue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
            s.playQueueDepth++;
        }
    } 
    else if (s.state == State::RECORDING) {
        // Limit the amount of sound
        if (s.playQueueDepth < 1500) {
            s.playQueue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
            s.playQueueDepth++;
        }
    } 
}

void NodeParrot::audioRateTick() {
    _sessions.visitIf(
        // Visitor
        [this](Session& s) {
            s.audioRateTick(this->_log, this->_clock, *this);
            return true;
        },
        // Predicate
        [](const Session& s) { return s.active; }
    );
}

void NodeParrot::tenSecTick() {
    int c = _sessions.countIf([](const Session& s) { return s.active; });
    if (c) 
        _log.info("Sessions: %d", c);
}
   
void NodeParrot::_loadAudioFile(const char* fn, std::queue<PCM16Frame>& queue) const {    
    
    ifstream aud(fn, std::ios::binary);
    if (!aud.is_open()) {
        _log.info("Failed to open %s", fn);
        return;
    }

    int16_t pcm8k[160];
    unsigned pcmPtr = 0;
    char buffer[2];
    amp::Resampler resampler;
    resampler.setRates(8000, 48000);

    while (aud.read(buffer, 2)) {
        pcm8k[pcmPtr++] = unpack_int16_le((const uint8_t*)buffer);
        if (pcmPtr == BLOCK_SIZE_8K) {
            int16_t pcm48k[BLOCK_SIZE_48K];
            resampler.resample(pcm8k, BLOCK_SIZE_8K, pcm48k, BLOCK_SIZE_48K);
            queue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
            pcmPtr = 0;
        }
    }

    // Clean up last frame
    if (pcmPtr < BLOCK_SIZE_8K) {
        for (unsigned i = 0; i < BLOCK_SIZE_8K - pcmPtr; i++)
            pcm8k[pcmPtr++] = 0;
        int16_t pcm48k[BLOCK_SIZE_48K];
        resampler.resample(pcm8k, BLOCK_SIZE_8K, pcm48k, BLOCK_SIZE_48K);
        queue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
        pcmPtr = 0;
    }
}

void NodeParrot::_loadSilence(unsigned ticks, std::queue<PCM16Frame>& queue) const {    
    int16_t pcm48k[BLOCK_SIZE_48K];
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++)
        pcm48k[i] = 0;
    for (unsigned i = 0; i < ticks; i++)
        queue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
}

Message NodeParrot::_makeMessage(const PCM16Frame& frame, 
    unsigned destBusId, unsigned destCallId) const {
    // Convert the PCM16 data into LE mode as defined by the CODEC.
    uint8_t pcm48k[BLOCK_SIZE_48K * 2];
    Transcoder_SLIN_48K transcoder;
    transcoder.encode(frame.data(), frame.size(), pcm48k, BLOCK_SIZE_48K * 2);
    Message msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K, 
        BLOCK_SIZE_48K * 2, pcm48k, _clock.timeUs());
    msg.setOriginUs(_clock.timeUs());
    msg.setSource(0, 0);
    msg.setDest(destBusId, destCallId);
    return msg;
}

void NodeParrot::Session::reset() {
    active = false;
    adaptorIn.reset();
    adaptorOut.reset();
}

bool NodeParrot::Session::belongsTo(const Message& msg) const {
    return msg.getSourceBusId() == lineId && msg.getSourceCallId() == callId;
}

void NodeParrot::Session::audioRateTick(Log& log, Clock& clock, NodeParrot& node) {

    // General timeout
    if (clock.isPast(callStartTime + SESSION_TIMEOUT_MS)) {
        log.info("Timing out call %u", callId);
        state = State::TIMEDOUT;
        Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_TERMINATE, 0, 0,
            clock.timeUs());
        msg.setDest(lineId, callId);
        adaptorOut.consume(msg);
        reset();
    }
    else if (state == State::CONNECTED) {
        // We only start after a bit of silence to address any initial
        // clicks or pops on key.
        if (clock.isPast(stateStartTime + 2000)) {
            // Load the greeting into the play queue
            node._loadAudioFile("../media/greeting-8k.pcm", playQueue);
            // Trigger the greeting playback
            state = State::PLAYING_PROMPT_GREETING;
            log.info("Greeting start");
        }
    }
    else if (state == State::PLAYING_PROMPT_GREETING) {
        if (playQueue.empty()) {
            log.info("Greeting end");
            state = State::WAITING_FOR_RECORD;
            stateStartTime = clock.time();
        } else {
            adaptorOut.consume(node._makeMessage(playQueue.front(), lineId, callId));
            playQueue.pop();
        }
    }
    else if (state == State::RECORDING) {
        if (clock.isPast(lastAudioTime + 500)) {
            log.info("Record end");
            state = State::PAUSE_AFTER_RECORD;
            stateStartTime = clock.time();
        }
    } 
    else if (state == State::PAUSE_AFTER_RECORD) {
        if (clock.isPast(stateStartTime + 750)) {
            log.info("Playback prompt");
            state = State::PLAYING;
            stateStartTime = clock.time();
        }
    }
    else if (state == State::PLAYING) {
        if (playQueue.empty()) {
            log.info("Play end");
            // TODO
            state = State::WAITING_FOR_RECORD;
            stateStartTime = clock.time();

            //state = State::ACTIVE;
            //toneActive = true;
            //toneOmega = 2.0f * 3.14159f * 440.0f / (float)AUDIO_RATE;
            //tonePhi = 0;
            //toneLevel = 0.5;
        } else {
            adaptorOut.consume(node._makeMessage(playQueue.front(), lineId, callId));
            playQueue.pop();
        }
    }
    else if (state == State::ACTIVE) {
        if (toneActive) {
            // Make a tone at 48K
            int16_t data[BLOCK_SIZE_48K];
            for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
                data[i] = (toneLevel * cos(tonePhi)) * 32767.0f;
                tonePhi += toneOmega;
                tonePhi = fmod(tonePhi, 2.0f * 3.14159f);
            }
            PCM16Frame f(data, 160 * 6);
            adaptorOut.consume(node._makeMessage(f, lineId, callId));
        }
    }
}

}

