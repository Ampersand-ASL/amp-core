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

#include "Message.h"
#include "BridgeCall.h"

using namespace std;

namespace kc1fsz {

    namespace amp {

BridgeCall::BridgeCall() {
    // The last stage of the BridgeIn pipeline drops the message 
    // into (a) the input staging area in NORMAL mode or (b) the 
    // parrot system in PARROT mode.
    _bridgeIn.setSink([this](const Message& msg) {
        if (_mode == Mode::NORMAL)
            this->_stageIn = msg;
        else if (_mode == Mode::PARROT)
            _consumeParrotAudio(msg);
    });
    // The last stage of the BridgeOut pipeline passes the message
    // to the sink message bus.
    _bridgeOut.setSink([this](const Message& msg) {
        this->_sink->consume(msg);
    });
}

void BridgeCall::reset() {
    _active = false;
    _lineId = 0;  
    _callId = 0; 
    _bridgeIn.reset();
    _bridgeOut.reset();
}

void BridgeCall::setup(unsigned lineId, unsigned callId, uint32_t startMs, CODECType codec) {
    _active = true;
    _lineId = lineId;  
    _callId = callId; 
    _startMs = startMs;
    _bridgeIn.setCodec(codec);
    _bridgeIn.setStartTime(startMs);
    _bridgeOut.setCodec(codec);
}

void BridgeCall::consume(const Message& frame) {
    _bridgeIn.consume(frame);       
}

void BridgeCall::audioRateTick() {
    if (_mode == Mode::NORMAL) {
        _bridgeIn.audioRateTick();
    } else if (_mode == Mode::TONE) {
        _toneAudioRateTick();
    } else if (_mode == Mode::PARROT) {
        _parrotAudioRateTick();
    }
}

void BridgeCall::contributeInputAudio(int16_t* pcmBlock, unsigned blockSize, float scale) const {
    if (_stageIn.getType() == Message::Type::AUDIO) {
        assert(_stageIn.size() == BLOCK_SIZE_48K * 2);
        assert(_stageIn.getFormat() == CODECType::IAX2_CODEC_SLIN_48K);
        const uint8_t* p = _stageIn.body();
        for (unsigned i = 0; i < blockSize; i++, p += 2)
            pcmBlock[i] += scale * (float)unpack_int16_le(p);
    }
}

/**
 * Takes 48K PCM and passes it into the BridgeOut pipeline for transcoding, etc.
 */
void BridgeCall::setOutputAudio(const int16_t* source, unsigned blockSize) {
    // Make a message with the new audio
    assert(blockSize == BLOCK_SIZE_48K);
    uint8_t encoded[BLOCK_SIZE_48K * 2];
    uint8_t* p = encoded;
    for (unsigned i = 0; i < blockSize; i++, p += 2)
        pack_int16_le(source[i], p);
    Message audioOut(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K, 
        BLOCK_SIZE_48K * 2, encoded, 0, 0);
    audioOut.setSource(10, 1);
    audioOut.setDest(_lineId, _callId);
    _bridgeOut.consume(audioOut);
}

// ===== Tone Mode Related ====================================================

void BridgeCall::_toneAudioRateTick() {
    if (_toneActive) {
        // Make a tone at 48K
        int16_t data[BLOCK_SIZE_48K];
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
            data[i] = (_toneLevel * cos(_tonePhi)) * 32767.0f;
            _tonePhi += _toneOmega;
            _tonePhi = fmod(_tonePhi, 2.0f * 3.14159f);
        }
        // Pass into the output pipeline for transcoding, etc.
        _bridgeOut.consume(_makeMessage(PCM16Frame(data, 160 * 6), _lineId, _callId));
    }
}

// ===== Parrot Related =======================================================

/**
 * This function will be called by the input adaptor after the PLC and 
 * transcoding has happened.
 */
void BridgeCall::_consumeParrotAudio(const Message& msg) { 

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

void BridgeCall::_parrotAudioRateTick() {

    // General timeout
    if (_clock->isPast(_startMs + SESSION_TIMEOUT_MS)) {
        _log->info("Timing out call %u/%d", _lineId, _callId);
        _state = State::TIMEDOUT;
        Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_TERMINATE, 
            0, 0, 0, _clock->timeUs());
        msg.setDest(_lineId, _callId);
        _bridgeOut.consume(msg);
        reset();
    }
    else if (_state == State::CONNECTED) {
        // We only start after a bit of silence to address any initial
        // clicks or pops on key.
        if (_clock->isPast(_stateStartMs + 2000)) {
            // Load the greeting into the play queue
            _loadAudioFile("../media/greeting-8k.pcm", playQueue);
            // Trigger the greeting playback
            _state = State::PLAYING_PROMPT_GREETING;
            _log->info("Greeting start");
        }
    }
    else if (_state == State::PLAYING_PROMPT_GREETING) {
        if (_playQueue.empty()) {
            _log->info("Greeting end");
            _state = State::WAITING_FOR_RECORD;
            _stateStartMs = _clock->time();
        } else {
            _bridgeOut.consume(_makeMessage(playQueue.front(), _lineId, _callId));
            _playQueue.pop();
        }
    }
    else if (_state == State::RECORDING) {
        if (_clock->isPast(_lastAudioMs + 5000)) {
            _log->info("Record end (Long silence)");
            _state = State::PAUSE_AFTER_RECORD;
            _stateStartMs = _clock->time();
        }
    } 
    else if (_state == State::PAUSE_AFTER_RECORD) {
        if (_clock->isPast(_stateStartMs + 750)) {
            _log->info("Playback prompt");
            _state = State::PLAYING;
            _stateStartMs = _clock->time();
        }
    }
    else if (_state == State::PLAYING) {
        if (_playQueue.empty()) {
            _log->info("Play end");
            // TODO
            _state = State::WAITING_FOR_RECORD;
            _stateStartMs = _clock->time();
        } else {
            _bridgeOut.consume(_makeMessage(_playQueue.front(), _lineId, _callId));
            _playQueue.pop();
        }
    }
}

void BridgeCall::_loadAudioFile(const char* fn, std::queue<PCM16Frame>& queue) const {    
    
    ifstream aud(fn, std::ios::binary);
    if (!aud.is_open()) {
        _log->info("Failed to open %s", fn);
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

void BridgeCall::_loadSilence(unsigned ticks, std::queue<PCM16Frame>& queue) const {    
    int16_t pcm48k[BLOCK_SIZE_48K];
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++)
        pcm48k[i] = 0;
    for (unsigned i = 0; i < ticks; i++)
        queue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
}

Message BridgeCall::_makeMessage(const PCM16Frame& frame, 
    unsigned destBusId, unsigned destCallId) const {
    // Convert the PCM16 data into LE mode as defined by the CODEC.
    uint8_t pcm48k[BLOCK_SIZE_48K * 2];
    Transcoder_SLIN_48K transcoder;
    transcoder.encode(frame.data(), frame.size(), pcm48k, BLOCK_SIZE_48K * 2);
    Message msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K, 
        BLOCK_SIZE_48K * 2, pcm48k, 0, _clock->timeUs());
    msg.setSource(0, 0);
    msg.setDest(destBusId, destCallId);
    return msg;
}

    }
}
