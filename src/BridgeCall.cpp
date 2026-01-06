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

void BridgeCall::reset() {

    _active = false;
    _mode = Mode::NORMAL;
    _lineId = 0;  
    _callId = 0; 
    _startMs = 0;
    _lastAudioMs = 0;
    _echo = false;
    _sourceAddrValidated = false;
    
    _bridgeIn.reset();
    _bridgeOut.reset();

    _toneActive = false;
    _toneOmega = 0;
    _tonePhi = 0;
    _toneLevel = 0;

    _captureQueue = std::queue<PCM16Frame>();
    _captureQueueDepth = 0;
    _playQueue = std::queue<PCM16Frame>();
    _echoQueue = std::queue<PCM16Frame>();
    _parrotState = ParrotState::NONE;
    _parrotStateStartMs = 0;
    _lastUnkeyProcessedMs = 0;
}

void BridgeCall::setup(unsigned lineId, unsigned callId, uint32_t startMs, CODECType codec,
    bool bypassJitterBuffer, bool echo, bool sourceAddrValidated, Mode initialMode) {

    _active = true;
    _lineId = lineId;  
    _callId = callId; 
    _startMs = startMs;
    _lastAudioMs = 0;

    _bridgeIn.setCodec(codec);
    _bridgeIn.setBypassJitterBuffer(bypassJitterBuffer);
    _bridgeIn.setStartTime(startMs);
    _bridgeOut.setCodec(codec);

    _echo = echo;
    _sourceAddrValidated = sourceAddrValidated;
    _mode = initialMode;

    if (_mode == Mode::PARROT) {
        _parrotState = ParrotState::CONNECTED;
        _parrotStateStartMs = _clock->time();
    }
}

void BridgeCall::consume(const Message& frame) {
    if (frame.getType() == Message::Type::TTS_AUDIO ||
        frame.getType() == Message::Type::TTS_END) {
        _processTTSAudio(frame);
    } else if (frame.isSignal(Message::SignalType::DTMF_PRESS)) {
        assert(frame.size() == sizeof(PayloadDtmfPress));
        PayloadDtmfPress* payload = (PayloadDtmfPress*)frame.body();
        if (_mode == Mode::PARROT) {
            if (payload->symbol == '1') {
                _log->info("Starting sweep");
                _loadSweep(_playQueue);
                _parrotState = ParrotState::PLAYING;            
            }
        }
    } else if (frame.isVoice() || frame.isSignal(Message::SignalType::RADIO_UNKEY)) {
        _bridgeIn.consume(frame);       
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

Message BridgeCall::_makeMessage(const PCM16Frame& frame, uint32_t rxMs,
    unsigned destLineId, unsigned destCallId) const {
    // Convert the PCM16 data into LE mode as defined by the CODEC.
    uint8_t pcm48k[BLOCK_SIZE_48K * 2];
    Transcoder_SLIN_48K transcoder;
    transcoder.encode(frame.data(), frame.size(), pcm48k, BLOCK_SIZE_48K * 2);
    // #### TODO: DO TIMES MATTER HERE?
    Message msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K, 
        BLOCK_SIZE_48K * 2, pcm48k, 0, rxMs);
    msg.setSource(LINE_ID, CALL_ID);
    msg.setDest(destLineId, destCallId);
    return msg;
}

void BridgeCall::_processTTSAudio(const Message& frame) {
    if (_mode == Mode::PARROT) 
        _processParrotTTSAudio(frame);
}

// ===== Conference Mode Related ===============================================

void BridgeCall::_processNormalAudio(const Message& msg) {   
    _stageIn = msg;
}

/**
 * The Bridge calls this function to collect this call's contribution to the 
 * conference audio. 
 */
void BridgeCall::extractInputAudio(int16_t* pcmBlock, unsigned blockSize, 
    float scale, uint32_t tickMs) {
    assert(_stageIn.getType() == Message::Type::AUDIO);
    assert(_stageIn.size() == BLOCK_SIZE_48K * 2);
    assert(_stageIn.getFormat() == CODECType::IAX2_CODEC_SLIN_48K);
    const uint8_t* p = _stageIn.body();
    for (unsigned i = 0; i < blockSize; i++, p += 2)
        pcmBlock[i] += scale * (float)unpack_int16_le(p);
    // Clear the staging area so that we don't contribute this frame again
    _stageIn.clear();
}

/**
 * The bridge calls this function to set the final output audio for this call.
 * Takes 48K PCM and passes it into the BridgeOut pipeline for transcoding, etc.
 */
void BridgeCall::setOutputAudio(const int16_t* pcm48k, unsigned blockSize, uint32_t tickMs) {
    if (_mode == Mode::NORMAL) {
        _bridgeOut.consume(_makeMessage(PCM16Frame(pcm48k, blockSize), tickMs, _lineId, _callId));
    }
}

// ===== Tone Mode Related ====================================================

void BridgeCall::_toneAudioRateTick(uint32_t tickMs) {
    if (_toneActive) {
        // Make a tone at 48K
        int16_t data[BLOCK_SIZE_48K];
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
            data[i] = (_toneLevel * cos(_tonePhi)) * 32767.0f;
            _tonePhi += _toneOmega;
            _tonePhi = fmod(_tonePhi, 2.0f * 3.14159f);
        }
        // Pass into the output pipeline for transcoding, etc.
        _bridgeOut.consume(_makeMessage(PCM16Frame(data, BLOCK_SIZE_48K), tickMs, _lineId, _callId));
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
        _lastAudioMs = _clock->time();

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
        // #### TODO: PLAY?
        if (_echo)
            _echoQueue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
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
        // We only start after a bit of silence to address any initial
        // clicks or pops on key.
        if (_clock->isPast(_parrotStateStartMs + 1500)) {

            // Create the speech that will be sent to the caller
            string prompt;
            prompt = "Parrot connected. ";

            if (!_sourceAddrValidated) 
                prompt += "Node is unregistered. ";

            if (_bridgeIn.getCodec() == CODECType::IAX2_CODEC_G711_ULAW) 
                prompt += "CODEC is 8K mulaw. ";
            else if (_bridgeIn.getCodec() == CODECType::IAX2_CODEC_SLIN_16K) 
                prompt += "CODEC is 16K linear. ";

            prompt += "Ready to record.";

            // Queue a TTS request
            Message req(Message::Type::TTS_REQ, 0, prompt.length(), (const uint8_t*)prompt.c_str(), 
                0, 0);
            req.setSource(_lineId, _callId);
            req.setDest(0, 0);
            _ttsQueueReq->push(req);

            _parrotState = ParrotState::TTS_AFTER_CONNECTED;
            _parrotStateStartMs = _clock->time();
        }
    }
    else if (_parrotState == ParrotState::PLAYING_PROMPT_GREETING) {
        if (_playQueue.empty()) {
            _log->info("Greeting end");
            _parrotState = ParrotState::WAITING_FOR_RECORD;
            _parrotStateStartMs = _clock->time();
        } else {
            _bridgeOut.consume(_makeMessage(_playQueue.front(), tickMs, _lineId, _callId));
            _playQueue.pop();
        }
    }
    else if (_parrotState == ParrotState::RECORDING) {
        if (_clock->isPast(_lastAudioMs + 5000)) {
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
        if (!_echoQueue.empty()) {
            _bridgeOut.consume(_makeMessage(_echoQueue.front(), tickMs, _lineId, _callId));
            _echoQueue.pop();
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

            // Create the speech that will be sent to the caller
            string prompt;
            char sp[64];
           
            // #### TODO: DO A BETTER JOB ON THE CLIPPING CASE

            int peakPowerInt = (int)peakPower;

            if (peakPowerInt < -40) {
                snprintf(sp, 64, "Peak is less than minus 40db");
            } else if (peakPowerInt < 0) {                
                snprintf(sp, 64, "Peak is minus %ddb", abs(peakPowerInt));
            } else {
                snprintf(sp, 64, "Peak is 0db");
            }
            prompt += sp;
            prompt += ", ";

            int avgPowerInt = (int)avgPower;

            if (avgPower < -40) {
                snprintf(sp, 64, "Average is less than minus 40db");
            } else if (avgPower < 0) {
                snprintf(sp, 64, "Average is minus %ddb", abs(avgPowerInt));
            } else {
                snprintf(sp, 64, "Average is 0db");
            }
            prompt += sp;
            prompt += ". ";

            prompt += "Playback.";

            // Queue a request for TTS
            Message req(Message::Type::TTS_REQ, 0, prompt.length(), (const uint8_t*)prompt.c_str(), 
                0, 0);
            req.setSource(_lineId, _callId);
            req.setDest(0, 0);
            _ttsQueueReq->push(req);

            // Get into the state waiting for the TTS to complete
            _parrotState = ParrotState::TTS_AFTER_RECORD;
            _parrotStateStartMs = _clock->time();
        }
    }
    else if (_parrotState == ParrotState::PLAYING) {
        if (_playQueue.empty()) {
            _log->info("Play end");
            // TODO
            _parrotState = ParrotState::WAITING_FOR_RECORD;
            _parrotStateStartMs = _clock->time();
        } else {
            _bridgeOut.consume(_makeMessage(_playQueue.front(), tickMs, _lineId, _callId));
            _playQueue.pop();
        }
    }
    else if (_parrotState == ParrotState::SWEEP_ACTIVE) {
        if (_clock->isPast(_sweepTime + 1000)) {
            _log->info("Sweep end");
            _parrotState = ParrotState::WAITING_FOR_RECORD;
            _parrotStateStartMs = _clock->time();
        }
        else {
            // Generate audio 
            _toneOmega = 2.0f * 3.14159f * 400.0f / 48000.0f;
            int16_t data[BLOCK_SIZE_48K];
            for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
                data[i] = (0.5 * cos(_tonePhi)) * 32767.0f;
                _tonePhi += _toneOmega;
                _tonePhi = fmod(_tonePhi, 2.0f * 3.14159f);
            }
            // Pass into the output pipeline for transcoding, etc.
            _bridgeOut.consume(_makeMessage(PCM16Frame(data, BLOCK_SIZE_48K), tickMs, _lineId, _callId));
        }
    }
}

void BridgeCall::_processParrotTTSAudio(const Message& frame) {
    if (_parrotState == ParrotState::TTS_AFTER_CONNECTED) {
        if (frame.getType() == Message::Type::TTS_AUDIO) {
            _loadAudioMessage(frame, _playQueue);
        } else if (frame.getType() == Message::Type::TTS_END) {
            _log->info("Greeting start");
            _parrotState = ParrotState::PLAYING_PROMPT_GREETING;
            _parrotStateStartMs = _clock->time();
        }        
    }
    else if (_parrotState == ParrotState::TTS_AFTER_RECORD) {
        if (frame.getType() == Message::Type::TTS_AUDIO) {
            _loadAudioMessage(frame, _playQueue);
        } else if (frame.getType() == Message::Type::TTS_END) {

            _loadSilence(25, _playQueue);

            // Move the recording to the end of the playback queue
            while (!_captureQueue.empty()) {
                _playQueue.push(_captureQueue.front());
                _captureQueue.pop();
            }

            _log->info("Playback start");
            _parrotState = ParrotState::PLAYING;
            _parrotStateStartMs = _clock->time();
        }        
    }
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

void BridgeCall::_loadAudioFile(const char* fn, std::queue<PCM16Frame>& queue) const {    
    
    string fullPath("../media");
    if (getenv("AMP_MEDIA_DIR"))
        fullPath = getenv("AMP_MEDIA_DIR");
    fullPath += "/16k/";
    fullPath += fn;
    fullPath += ".pcm";

    ifstream aud(fullPath, std::ios::binary);
    if (!aud.is_open()) {
        _log->info("Failed to open %s", fullPath.c_str());
        return;
    }

    int16_t pcm16k[BLOCK_SIZE_16K];
    unsigned pcmPtr = 0;
    char buffer[2];
    amp::Resampler resampler;
    resampler.setRates(16000, 48000);

    while (aud.read(buffer, 2)) {
        pcm16k[pcmPtr++] = unpack_int16_le((const uint8_t*)buffer);
        if (pcmPtr == BLOCK_SIZE_16K) {
            int16_t pcm48k[BLOCK_SIZE_48K];
            resampler.resample(pcm16k, BLOCK_SIZE_16K, pcm48k, BLOCK_SIZE_48K);
            queue.push(PCM16Frame(pcm48k, BLOCK_SIZE_48K));
            pcmPtr = 0;
        }
    }

    // Clean up last frame
    if (pcmPtr < BLOCK_SIZE_16K) {
        for (unsigned i = 0; i < BLOCK_SIZE_16K - pcmPtr; i++)
            pcm16k[pcmPtr++] = 0;
        int16_t pcm48k[BLOCK_SIZE_48K];
        resampler.resample(pcm16k, BLOCK_SIZE_16K, pcm48k, BLOCK_SIZE_48K);
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

    // Ignore the first 100ms of the recording to avoid distortion due to pops/clips
    unsigned startI = 100 / 20;
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
