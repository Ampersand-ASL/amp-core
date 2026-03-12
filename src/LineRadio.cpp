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
#include <cstring>
#include <iostream>
#include <cmath>
#include <cassert>

#include <kc1fsz-tools/Log.h>

#include "IAX2Util.h"
#include "Message.h"
#include "Transcoder_SLIN_48K.h"
#include "LineRadio.h"

using namespace std;

namespace kc1fsz {

/**
 * Floating-point complex number
 */
struct cf32 {

    float r = 0;
    float i = 0;

    cf32() : r(0), i(0) {}
    cf32(float ar, float ai) : r(ar), i(ai) {}
    cf32(const cf32& other) : r(other.r), i(other.i) { }

    float mag() const {
        return std::sqrt(magSquared());
    }
    float magSquared() const {
        return r * r + i * i;
    }
    /**
     * @returns Phase angle in radians.
     */
    float phase() const {
        return std::atan2(i, r);
    }
    /**
     * @returns this number plus b.
     */
    cf32 add(cf32 b) const {
        return cf32(r + b.r, i + b.i);
    }
    /**
     * @returns this number times b.
     */
    cf32 mult(cf32 b) const {
        return cf32(r * b.r - i * b.i, r * b.i + i * b.r);
    }
};

/**
 * This is an out-of-the-book implementation to use for sanity checking.
*/
static void simpleDFT(cf32* in, cf32* out, uint16_t n) {
    for (uint16_t k = 0; k < n; k++) { 
        float sumreal = 0;
        float sumimag = 0;
        for (uint16_t t = 0; t < n; t++) {  // For each input element
            float angle = 2.0 * PI * (float)t * (float)k / (float)n;
            sumreal +=  in[t].r * std::cos(angle) + in[t].i * std::sin(angle);
            sumimag += -in[t].r * std::sin(angle) + in[t].i * std::cos(angle);
        }
        out[k].r = sumreal / (float)n;
        out[k].i = sumimag / (float)n;
    }
}

LineRadio::LineRadio(Log& log, Clock& clock, MessageConsumer& captureConsumer, 
    unsigned busId, unsigned callId,
    unsigned destBusId, unsigned destCallId, 
    unsigned signalDestLineId) 
:   _log(log),
    _clock(clock),
    _captureConsumer(captureConsumer),
    _busId(busId),
    _callId(callId),
    _destBusId(destBusId), 
    _destCallId(destCallId),
    _signalDestLineId(signalDestLineId),
    _startTimeMs(_clock.time()),
    _dtmfDetector(clock, BLOCK_SIZE_8K / 2),
    _tonePhi(0),
    _injectTonePhi(0) {

    _resampler.setRates(48000, 8000);

    _injectToneOmega = 2.0f * 3.1415926f * 400.0f / 48000.0f;
}

void LineRadio::resetStatistics() {
    _captureClipCount = 0;
    _capturePcmValueMax = 0;
    _capturePcmValueSum = 0;
    _capturePcmValueCount = 0;
    _playClipCount = 0;
    _playPcmValueMax = 0;
    _playPcmValueSum = 0;
    _playPcmValueCount = 0;
    _fftMaxMag = 0;
    _fftMaxFreq = 0;
    // Prepare for another capture
    _fftTrigger = true;
}

/**
 * This is where played audio comes in from the outside.
 */
void LineRadio::consume(const Message& msg) {

    // This is a hardware signal coming in from the outside
    if (msg.isSignal(Message::SignalType::COS_ON)) {
        _setCosStatus(true);
    } 
    // This is a hardware signal coming in from the outside
    else if (msg.isSignal(Message::SignalType::COS_OFF)) {
        _setCosStatus(false);
    }    
    // NOTE: Tone generation will override. 
    else if (msg.getType() == Message::Type::AUDIO) {

        // TODO: MIX INBOUND AUDIO WITH TONE?
        if (_toneActive) 
            return;

        // Detect transitions from silence to playing
        if (!_playing)
            _playStart();

        assert(msg.size() == BLOCK_SIZE_48K * 2);

        int16_t pcm48k_2[BLOCK_SIZE_48K];

        // #### TODO: LOOKING INTO REMOVING THIS CASE
        if (msg.getFormat() == CODECType::IAX2_CODEC_SLIN_48K) {
            // Convert the SLIN_48K LE into 16-bit PCM audio
            Transcoder_SLIN_48K transcoder;
            transcoder.decode(msg.body(), msg.size(), pcm48k_2, BLOCK_SIZE_48K);
        }
        else if (msg.getFormat() == CODECType::IAX2_CODEC_PCM_48K) {
            // In this case no conversion is needed
            memcpy(pcm48k_2, msg.body(), BLOCK_SIZE_48K * 2);
        }
        else assert(false);

        // Here is where statistical analysis and/or local recording can take 
        // place for diagnostic purposes.
        _analyzePlayedAudio(pcm48k_2, BLOCK_SIZE_48K);

        // Call down to do the actual play on the hardware
        _playPCM48k(pcm48k_2, BLOCK_SIZE_48K);

        _lastPlayedFrameMs = _clock.time();
        _playing = true;
    }
    else if (msg.isSignal(Message::SignalType::TONE)) {
        PayloadTone payload;
        assert(msg.size() == sizeof(payload));
        memcpy(&payload, msg.body(), msg.size());
        // Enable some tone generation
        _toneActive = true;
        _toneTicks = 50 * 3;
        _toneOmega = 2.0f * 3.1415926f * payload.freq / 48000.0f;
    }    
}

/**
 * This is parallel to consume() above. This is used to generate a frame 
 * of audio when tone is enabled.
 */
void LineRadio::_generateToneFrame() {

    int16_t pcm48k_2[BLOCK_SIZE_48K];

    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
        pcm48k_2[i] = 32767.0f * _toneAmpTarget * std::cos(_tonePhi);
        // IMPORTANT: Phase continuity at all times
        _tonePhi += _toneOmega;
    }

    // Avoids strange artifacts when phi becomes very large and precision problems
    // creep in.
    _tonePhi = std::fmod(_tonePhi, 2.0f * 3.1415926f);

    // Here is where statistical analysis and/or local recording can take 
    // place for diagnostic purposes.
    _analyzePlayedAudio(pcm48k_2, BLOCK_SIZE_48K);

    // Call down to do the actual play on the hardware
    _playPCM48k(pcm48k_2, BLOCK_SIZE_48K);

    _lastPlayedFrameMs = _clock.time();
    _playing = true;
}

void LineRadio::oneSecTick() {

    int radioRxDb = dbVfs(_capturePcmValueMax);
    int radioTxDb = -99;
    
    if (_capturePcmValueCount) {
        uint32_t avg = _capturePcmValueSum / _capturePcmValueCount;
        float magDb = 20.0 * std::log10(2.0f * _fftMaxMag / 32767.0f);
        _log.info("RXLEVEL %6u %5.1f %5.1f %6.0f at %4.0f Hz", 
            _captureClipCount, dbVfs(_capturePcmValueMax), dbVfs(avg),
            magDb, _fftMaxFreq);
    }

    if (_playPcmValueCount) {
        //uint32_t avg = _playPcmValueSum / _playPcmValueCount;
        //_log.info("RXLEVEL %6u %5.1f %5.1f", _playClipCount, dbVfs(_playPcmValueMax), dbVfs(avg));
        radioTxDb = dbVfs(_playPcmValueMax);
    }

    PayloadCallLevels payload;
    payload.rx0Db = radioRxDb;
    payload.rx1Db = -99;
    payload.tx0Db = radioTxDb;
    payload.tx1Db = -99;

    _sendSignal(Message::SignalType::CALL_LEVELS, &payload, sizeof(payload));

    resetStatistics();

    // Send the talker ID if we are actively capturing audio
    if (_clock.isInWindow(_lastCaptureMs, 2000))
        _sendTalkerId();
}

void LineRadio::_sendSignal(Message::SignalType type, void* body, unsigned len) {
    _sendSignal(type, body, len, _destBusId, _destCallId);
}

void LineRadio::_sendSignal(Message::SignalType type, void* body, unsigned len,
    unsigned destLineId, unsigned destCallId) {
    MessageWrapper msg(Message::Type::SIGNAL, type, len, (const uint8_t*)body, 
        0, _clock.time());
    msg.setSource(_busId, _callId);
    msg.setDest(destLineId, destCallId);
    _captureConsumer.consume(msg);
}

void LineRadio::audioRateTick(uint32_t tickMs) {

    _checkTimeouts();

    // Handle audio synthesis if necessary
    if (_toneActive) {     
        if (--_toneTicks == 0) 
            _toneActive = false;
        else {
            if (!_playing) 
                _playStart();
            _generateToneFrame();
        }
    }
}

void LineRadio::_checkTimeouts() {

    // Detect transitions from audio to silence
    if (_playing &&
        _clock.isPast(_lastPlayedFrameMs + _playSilenceIntervalMs)) {
        _playing = false;
        _playEnd();
    }

    if (_capturing &&
        _clock.isPast(_lastCapturedFrameMs + _captureSilenceIntervalMs)) {
        _capturing = false;
        _captureEnd();
    }
}

void LineRadio::_sendTalkerId() {
    char talkerId[32];
    strcpyLimited(talkerId, _callsign.c_str(), 32);
    _sendSignal(Message::SignalType::CALL_TALKERID, talkerId, strlen(talkerId) + 1);
}

void LineRadio::_open(bool echo, float echoGainDb) {    
    // Generate the same kind of call start message that would
    // come from the IAX2Line after a new connection.
    PayloadCallStart payload;
    payload.codec = CODECType::IAX2_CODEC_SLIN_48K;
    payload.bypassJitterBuffer = true;
    payload.echo = echo;
    payload.echoGainDb = echoGainDb;
    payload.startMs = _clock.time();
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "radio");
    payload.originated = true;
    payload.permanent = true;
    _sendSignal(Message::SignalType::CALL_START, &payload, sizeof(payload));
}

void LineRadio::_close() {
    PayloadCallEnd payload;
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "radio");
    _sendSignal(Message::SignalType::CALL_END, &payload, sizeof(payload));
}

void LineRadio::_analyzeCapturedAudio(const int16_t* frame, unsigned frameLen) {

    _lastFullCaptureMs = _clock.time();
    _lastCaptureMs = _clock.timeUs() / 1000;

    // Power
    for (unsigned i = 0; i < frameLen; i++) {
        int16_t sample = abs(frame[i]);
        if (sample > _clipThreshold) {
            _captureClipCount++;
            _captureClips++;
        }
        if (sample > _capturePcmValueMax)
            _capturePcmValueMax = sample;
        _capturePcmValueSum += sample;
        _capturePcmValueCount++;
    }

    // Perform rolling FFT
    if (_fftEnabled && _fftTrigger) {

        // Slide everything to the left to make room for a new block
        //memmove(_fftBlock, _fftBlock + BLOCK_SIZE_48K, sizeof(int16_t) * (FFT_SIZE - BLOCK_SIZE_48K));
        //memcpy(_fftBlock + FFT_SIZE - BLOCK_SIZE_48K, frame, BLOCK_SIZE_48K);

        // NOTE: FFT SIZE IS SMALLER THAN FRAME SIZE!
        cf32 inBlock[FFT_SIZE];
        for (unsigned i = 0; i < FFT_SIZE; i++) {
            inBlock[i].r = frame[i];
            inBlock[i].i = 0;
        }
        cf32 outBlock[FFT_SIZE];
        simpleDFT(inBlock, outBlock, FFT_SIZE);

        // Find largest power
        for (unsigned i = 1; i < FFT_SIZE / 2; i++) {
            if (outBlock[i].mag() > _fftMaxMag) {
                _fftMaxMag = outBlock[i].mag();
                _fftMaxFreq = (48000.0f / (float)FFT_SIZE) * (float)i;
            }
        }

        _fftTrigger = false;
    }
}

void LineRadio::_processCapturedAudio(const int16_t* block, unsigned blockLen,
    uint32_t actualCaptureMs, uint32_t idealCaptureMs) {

    assert(blockLen == BLOCK_SIZE_48K);

    // Decimate from 48k to 8k
    int16_t pcm8k[BLOCK_SIZE_8K];
    assert(_resampler.getInBlockSize() == BLOCK_SIZE_48K);
    assert(_resampler.getOutBlockSize() == BLOCK_SIZE_8K);
    _resampler.resample(block, blockLen, pcm8k, BLOCK_SIZE_8K);

    // Check for DTMF signaling. The detector
    // is configured for a frame size of 80 so we
    // pass the audio in two pieces.
    _dtmfDetector.processBlock(pcm8k);
    _dtmfDetector.processBlock(pcm8k + (BLOCK_SIZE_8K / 2));

    // TODO: SIGNALING
    if (_dtmfDetector.isDetectionPending()) {
        _log.info("DTMF %c", _dtmfDetector.popDetection());
    }

    // Make an SLIN_48K buffer in CODEC format.5
    uint8_t outBuffer[BLOCK_SIZE_48K * 2];
    Transcoder_SLIN_48K transcoder;

    if (_injectToneActive) {
        int16_t toneBlock[BLOCK_SIZE_48K];
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
            toneBlock[i] = _injectToneAmp * std::cos(_injectTonePhi) * 32767.0f;
            _injectTonePhi += _injectToneOmega;
        }
        _injectTonePhi = std::fmod(_injectTonePhi, 2.0f * 3.1415926f);
        transcoder.encode(toneBlock, blockLen, outBuffer, BLOCK_SIZE_48K * 2);
    } else {
        transcoder.encode(block, blockLen, outBuffer, BLOCK_SIZE_48K * 2);
    }

    // Make an audio message and send it to the listeners for processing
    MessageWrapper msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K,
        BLOCK_SIZE_48K * 2, outBuffer, 0, idealCaptureMs);
    msg.setSource(_busId, _callId);
    msg.setDest(_destBusId, _destCallId);

    _captureConsumer.consume(msg);
}

void LineRadio::_analyzePlayedAudio(const int16_t* frame, unsigned frameLen) {   
    for (unsigned i = 0; i < frameLen; i++) {
        int16_t sample = abs(frame[i]);
        if (sample > _clipThreshold) {
            _playClipCount++;
            _playClips++;
        }
        if (sample > _playPcmValueMax)
            _playPcmValueMax = sample;
        _playPcmValueSum += sample;
        _playPcmValueCount++;
    }
    _tsFrameCount++;
}

void LineRadio::_captureStart() {
    if (_record) {
        _captureRecordCounter++;
        _log.info("Started audio capturing %u-%u", _startTimeMs, _captureRecordCounter);
    }
}

void LineRadio::_captureEnd() {
    // Send the unkey message 
    _sendSignal(Message::SignalType::RADIO_UNKEY, 0, 0);
}

void LineRadio::_playStart() {

    // Generate a PTT ON signal
    _sendSignal(Message::SignalType::PTT_ON, 0, 0, _signalDestLineId, 
        Message::UNKNOWN_CALL_ID);

    if (_record) {
        _playRecordCounter++;
        _log.info("Started audio playing %u-%u", _startTimeMs, _playRecordCounter);
    }
    _tsFrameCount = 0;
}

void LineRadio::_playEnd() {

    // Generate a PTT OFF signal
    _sendSignal(Message::SignalType::PTT_OFF, 0, 0, _signalDestLineId, 
        Message::UNKNOWN_CALL_ID);
}

void LineRadio::_setCosStatus(bool cosActive) {   
    // Look for transitions
    if (cosActive && !_cosActive) {

        _log.info("COS active");
        _cosActive = true;    
        
        if (_triggerTone) {
            _injectToneActive = true;
        }

    } else if (!cosActive && _cosActive) {

        _log.info("COS inactive");
        _cosActive = false;
        
        _injectToneActive = false;

        // Generate an UNKEY signal on the negative transition
        _sendSignal(Message::SignalType::RADIO_UNKEY, 0, 0);
    }
}

void LineRadio::setToneEnabled(bool b) {
    _toneActive = b;
}

void LineRadio::setToneFreq(float hz) {
    _toneOmega = 2.0f * 3.1415926f * hz / (float)AUDIO_RATE;
}

/**
 * Sets the target tone amplitude. Target is achieved using a ramp to avoid
 * harsh transitions (i.e. "clicks").
 */
void LineRadio::setToneLevel(float dbv) {
    // A linear transition is used
    _toneRampIncrement = 1.0f / (_toneTransitionLength * (float)AUDIO_RATE);
    // The ramp starts at the previous target
    _toneAmpRamp = _toneAmpTarget;
    _toneAmpTarget = dbvToPeak(dbv);
    // Make sure the ramp is flowing in the right direction
    if (_toneAmpTarget < _toneAmpRamp) 
        _toneRampIncrement *= -1.0f;
}

void LineRadio::resetDelay() {
}

}
