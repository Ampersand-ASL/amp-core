/**
 * Copyright (C) 2026, Bruce MacKinnon KC1FSZ
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
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cmath>

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/NetUtils.h"
#include "kc1fsz-tools/Log.h"

#include "MessageConsumer.h"
#include "Message.h"
#include "LineParrot.h"

// #### TODO: CONSOLIDATE
#define BLOCK_SIZE_8K (160)
#define BLOCK_SIZE_16K (160 * 2)
#define BLOCK_SIZE_48K (160 * 6)

#define MAX_CAPTURE_FRAMES (750)
#define SILENCE_TIMEOUT_MS (2000)
#define PAUSE_AFTER_RECORD_MS (1000)

using namespace std;

namespace kc1fsz {
    namespace amp {

LineParrot::LineParrot(Log& log, Clock& clock, unsigned lineId,
    MessageConsumer& bus, unsigned audioDestLineId, unsigned ttsLineId)
:   _log(log),
    _clock(clock),
    _lineId(lineId),
    _bus(bus),
    _audioDestLineId(audioDestLineId),
    _ttsLineId(ttsLineId) {
}

int LineParrot::open() {

    close();

    // Generate a start of call message so that the bridge will accept audio
    // when it arrives.
    PayloadCallStart payload;
    // NOTE: Parrot uses the bridge audio rate/format to improve efficiency
    payload.codec = CODECType::IAX2_CODEC_PCM_48K;
    payload.bypassJitterBuffer = true;
    payload.echo = false;
    payload.startMs = _clock.time();
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "Parrot");
    payload.originated = true;
    payload.permanent = true;

    MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
        sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
    msg.setSource(_lineId, _callId);
    msg.setDest(_audioDestLineId, Message::BROADCAST);
    _bus.consume(msg);

    _enabled = true;

    return 0;
}

void LineParrot::close() {   
    PayloadCallEnd payload;
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "Parrot");
    _sendSignal(Message::SignalType::CALL_END, &payload, sizeof(payload));
    _enabled = false;
} 

void LineParrot::consume(const Message& msg) {   

    if (!_enabled)
        return;

    if (msg.isVoice()) {

        assert(msg.getFormat() == CODECType::IAX2_CODEC_PCM_48K);
        assert(msg.size() == BLOCK_SIZE_48K * 2);

        // Used to manage timeout
        _lastAudioRxMs = _clock.timeMs();

        // Look for a new talkspurt to record
        if (_state == State::STATE_LISTENING) {

            _log.info("LineParrot started recording");

            _captureQueue = std::queue<PCM16Frame>();
            _captureQueueDepth = 0;

            _setState(State::STATE_RECORDING);
        } 

        if (_state == State::STATE_RECORDING) {
            if (_captureQueueDepth < MAX_CAPTURE_FRAMES) {
                _captureQueue.push(PCM16Frame((const int16_t*)msg.body(), BLOCK_SIZE_48K));
                _captureQueueDepth++;
            }
        }
    }
    else if (msg.getType() == Message::Type::SIGNAL) {
        // This is the case where an UNKEY is requested by something 
        // on the internal bus. Create an UNKEY frame and send it out.
        if (msg.getFormat() == Message::SignalType::RADIO_UNKEY_GEN) {                        
            if (_state == State::STATE_RECORDING) {
                _endRecording();
            }
        }
        // This is the case when the TALKERID is being asserted
        else if (msg.getFormat() == Message::SignalType::CALL_TALKERID) {                        
        }
    }
    else if (msg.getType() == Message::Type::TTS_AUDIO) {
        if (_state == State::STATE_POST_RECORDING_TTS) {
            assert(msg.getFormat() == CODECType::IAX2_CODEC_PCM_48K);
            assert(msg.size() == BLOCK_SIZE_48K * 2);
            // This goes right onto the playback queue
            _playQueue.push(PCM16Frame((const int16_t*)msg.body(), BLOCK_SIZE_48K));
        }
    }
    else if (msg.getType() == Message::Type::TTS_END) {
        if (_state == State::STATE_POST_RECORDING_TTS)
            _endAnalysisTTS();
    }
}

void LineParrot::audioRateTick(uint32_t ms) {

    if (!_enabled)
        return;

    if (_state == State::STATE_RECORDING) {
        // Look for a timeout on the recording
        if (_clock.isPastWindow(_lastAudioRxMs, SILENCE_TIMEOUT_MS)) {
            _endRecording();
        }
    }
    else if (_state == State::STATE_PAUSE_AFTER_RECORDING) {
        // Enforce short delay after recording
        if (_clock.isPastWindow(_stateStartMs, PAUSE_AFTER_RECORD_MS)) {
            _setState(State::STATE_PLAYING);
        }
    }
    else if (_state == State::STATE_PLAYING) {
        if (!_playQueue.empty()) {
            // Make a message and transmit to the Bridge
            MessageWrapper msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_PCM_48K, 
                BLOCK_SIZE_48K * 2, (const uint8_t*)_playQueue.front().data(), 0, ms);
            msg.setSource(_lineId, _callId);
            msg.setDest(_audioDestLineId, Message::UNKNOWN_CALL_ID);
            _bus.consume(msg);
            _playQueue.pop();
        } 
        // Finished playing
        else {
            _log.info("LineParrot playback complete");
            _setState(State::STATE_LISTENING);
        }
    }
}

void LineParrot::_endRecording() {

    _log.info("LineParrot recording ended");

    // Clear the playback queue since we're starting a new cycle
    _playQueue = std::queue<PCM16Frame>();

    // Make a vector of the capture queue for analysis
    std::vector<PCM16Frame> captureCopy;
    while (!_captureQueue.empty()) {
        captureCopy.push_back(_captureQueue.front());
        _captureQueue.pop();
    }

    // Do the analysis of the recording and prepare an audio prompt
    AudioStats stats = analyzeRecording(captureCopy);

    // Re-queue the captured frames
    for (auto it = captureCopy.begin(); it != captureCopy.end(); it++)
        _captureQueue.push((*it));

    string prompt = summarizeAnalysis(stats, _levelThresholds);
    prompt += "Playback. ";

    _requestTTS(prompt.c_str());

    _setState(State::STATE_POST_RECORDING_TTS);
}

void LineParrot::_endAnalysisTTS() {

    // Move the recorded audio over to the playback queue
    while (!_captureQueue.empty()) {
        _playQueue.push(_captureQueue.front());
        _captureQueue.pop();
    }

    // Trigger playback
    _setState(State::STATE_PAUSE_AFTER_RECORDING);
}

void LineParrot::_sendSignal(Message::SignalType type, void* body, unsigned len) {
    _sendSignal(type, body, len, _audioDestLineId, Message::UNKNOWN_CALL_ID);
}

void LineParrot::_sendSignal(Message::SignalType type, void* body, unsigned len,
    unsigned destLineId, unsigned destCallId) {
    MessageWrapper msg(Message::Type::SIGNAL, type, len, (const uint8_t*)body, 
        0, _clock.time());
    msg.setSource(_lineId, _callId);
    msg.setDest(destLineId, destCallId);
    _bus.consume(msg);
}

void LineParrot::_requestTTS(const char* arg) {
    PayloadTTS payload;
    strcpyLimited(payload.req, arg, sizeof(payload.req));
    payload.preSilenceMs = 500;
    payload.postSilenceMs = 500;
    MessageWrapper req(Message::Type::TTS_REQ, 0, 
        sizeof(payload), (const uint8_t*)&payload, 0, 0);
    req.setSource(_lineId, _callId);
    req.setDest(_ttsLineId, Message::BROADCAST);
    _bus.consume(req);
}

void LineParrot::_setState(State state) {
    _state = state;
    _stateStartMs = _clock.timeMs();
}

string LineParrot::summarizeAnalysis(const AudioStats& stats, 
    vector<int>& levelThresholds) {

    // Create the speech that will be sent to the caller
    string prompt;
    
    // #### TODO: DO A BETTER JOB ON THE CLIPPING CASE

    int peakPowerInt = std::round(stats.peakPower);
    char sp[64];
    if (peakPowerInt < -40) {
        snprintf(sp, 64, "Peak is less than minus 40db");
    } else if (peakPowerInt < 0) {                
        snprintf(sp, 64, "Peak is minus %ddb", abs(peakPowerInt));
    } else {
        snprintf(sp, 64, "Peak is 0db");
    }
    prompt += sp;
    prompt += ", ";

    int avgPowerInt = std::round(stats.avgPower);
    if (avgPowerInt < -40) {
        snprintf(sp, 64, "Average is less than minus 40db");
    } else if (avgPowerInt < 0) {
        snprintf(sp, 64, "Average is minus %ddb", abs(avgPowerInt));
    } else {
        snprintf(sp, 64, "Average is 0db");
    }
    prompt += sp;
    prompt += ". ";

    // Now add some subjective commentary (CONTROVERSIAL!)
    if (levelThresholds.size() >= 4) {
        prompt += "Level is ";
        if (peakPowerInt >= levelThresholds.at(0))
            prompt += "very high";
        else if (peakPowerInt >= levelThresholds.at(1))
            prompt += "high";
        else if (peakPowerInt >= levelThresholds.at(2)) 
            prompt += "good";
        else if (peakPowerInt >= levelThresholds.at(3)) 
            prompt += "low";
        else 
            prompt += "very low";
        prompt += ". ";
    }

    return prompt;
}

LineParrot::AudioStats LineParrot::analyzeRecording(const std::vector<PCM16Frame>& audio) {

    AudioStats result;

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
        result.peakPower = -96.0;
        result.avgPower = -96.0;
        return result;
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
        result.peakPower = -96.0;
    } else {
        result.peakPower = 10.0 * log10((peak * peak) / (32767.0f * 32767.0f));
    }

    if (peakAvgSquare == 0) {
        result.avgPower = -96.0;
    } else {
        result.avgPower = 10.0 * log10(peakAvgSquare / (32767.0f * 32767.0f));
    }

    result.good = true;
    return result;
}

    }
}
