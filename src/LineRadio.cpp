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

LineRadio::LineRadio(Log& log, Clock& clock, MessageConsumer& captureConsumer, 
    unsigned busId, unsigned callId,
    unsigned destBusId, unsigned destCallId) 
:   _log(log),
    _clock(clock),
    _captureConsumer(captureConsumer),
    _busId(busId),
    _callId(callId),
    _destBusId(destBusId), 
    _destCallId(destCallId),
    _startTimeMs(_clock.time()),
    _dtmfDetector(clock, BLOCK_SIZE_8K / 2),
    _tonePhi(0) {

    _resampler.setRates(48000, 8000);
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
}

/**
 * This is where played audio comes in from the outside.
 */
void LineRadio::consume(const Message& msg) {

    // NOTE: Tone generation will override. 
    // TODO: MIX INBOUND AUDIO WITH TONE?
    if (!_toneActive && msg.getType() == Message::Type::AUDIO) {

        // Detect transitions from silence to playing
        bool leadingEdge = false;
        if (!_playing) {
            _playStart();
            leadingEdge = true;
        }

        assert(msg.size() == BLOCK_SIZE_48K * 2);
        assert(msg.getFormat() == CODECType::IAX2_CODEC_SLIN_48K);

        // Convert the SLIN_48K LE into 16-bit PCM audio
        int16_t pcm48k_2[BLOCK_SIZE_48K];
        Transcoder_SLIN_48K transcoder;
        transcoder.decode(msg.body(), msg.size(), pcm48k_2, BLOCK_SIZE_48K);

        // Here is where statistical analysis and/or local recording can take 
        // place for diagnostic purposes.
        _analyzePlayedAudio(pcm48k_2, BLOCK_SIZE_48K);

        // Apply a cross-fade to the start of the block if this is the first frame
        // after silence.
        if (leadingEdge) {
            const unsigned fadeSamples = BLOCK_SIZE_48K / 4;
            for (unsigned i = 0; i < fadeSamples; i++) {
                float scale = (float)i / (float)fadeSamples;
                float n = pcm48k_2[i] * scale;
                pcm48k_2[i] = n;
            }
        }

        // Call down to do the actual play on the hardware
        _playPCM48k(pcm48k_2, BLOCK_SIZE_48K);

        _lastPlayedFrameMs = _clock.time();
        _playing = true;
    }
}

/**
 * This is parallel to consume() above. This is used to generate a frame 
 * of audio when tone is enabled.
 */
void LineRadio::_generateToneFrame() {

    int16_t pcm48k_2[BLOCK_SIZE_48K];

    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
        pcm48k_2[i] = 32767.0f * _toneAmpRamp * std::cos(_tonePhi);
        // IMPORTANT: Phase continuity at all times
        _tonePhi += _toneOmega;
        _toneAmpRamp += _toneRampIncrement;
        // Check to see if the target has been achieved.  If so, turn 
        // off the ramp.
        if (_toneRampIncrement > 0 && _toneAmpRamp >= _toneAmpTarget) {
            _toneAmpRamp = _toneAmpTarget;
            _toneRampIncrement = 0;
        } else if (_toneRampIncrement < 0 && _toneAmpRamp <= _toneAmpTarget) {
            _toneAmpRamp = _toneAmpTarget;
            _toneRampIncrement = 0;
        }
    }

    // Avoids strange artifacts when phi becomes very large and precision problems
    // creep in.
    _tonePhi = std::fmod(_tonePhi, 2.0f * 3.1415926f);

    // Here is where statistical analysis and/or local recording can take 
    // place for diagnostic purposes.
    _analyzePlayedAudio(pcm48k_2, BLOCK_SIZE_48K);

    // Call down to do the actual play on the hardware
    _playPCM48k(pcm48k_2, BLOCK_SIZE_48K);
}

void LineRadio::oneSecTick() {

    int radioRxDb = -99;
    int radioTxDb = -99;

    if (_capturePcmValueCount) {
        uint32_t avg = _capturePcmValueSum / _capturePcmValueCount;
        _log.info("TXLEVEL %6u %5.1f %5.1f", _captureClipCount, dbVfs(_capturePcmValueMax), dbVfs(avg));
        radioRxDb = dbVfs(_capturePcmValueMax);
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

    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_LEVELS, 
        sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
    msg.setSource(_busId, _callId);
    msg.setDest(_destBusId, _destCallId);
    _captureConsumer.consume(msg);

    resetStatistics();
}

void LineRadio::audioRateTick(uint32_t tickMs) {

    _checkTimeouts();

    // Handle audio synthesis if necessary
    if (_toneActive) {
        _generateToneFrame();
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

void LineRadio::_open(bool echo) {    
    // Generate the same kind of call start message that would
    // come from the IAX2Line after a new connection.
    PayloadCallStart payload;
    payload.codec = CODECType::IAX2_CODEC_SLIN_48K;
    payload.bypassJitterBuffer = true;
    payload.echo = echo;
    payload.startMs = _clock.time();
    payload.localNumber[0] = 0;
    strcpy(payload.remoteNumber, "Radio");
    payload.originated = true;
    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
        sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
    msg.setSource(_busId, _callId);
    msg.setDest(_destBusId, _destCallId);
    _captureConsumer.consume(msg);
}

void LineRadio::_close() {
    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_END, 
        0, 0, 0, _clock.time());
    msg.setSource(_busId, _callId);
    msg.setDest(_destBusId, _destCallId);
    _captureConsumer.consume(msg);
}

void LineRadio::_analyzeCapturedAudio(const int16_t* frame, unsigned frameLen) {

    _lastFullCaptureMs = _clock.time();

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

    if (_record) {
        for (unsigned i = 0; i < frameLen; i++)
                _captureFile << frame[i] << endl;
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

    // Make an SLIN_48K buffer in CODEC format.
    uint8_t outBuffer[BLOCK_SIZE_48K * 2];
    Transcoder_SLIN_48K transcoder;
    transcoder.encode(block, blockLen, outBuffer, BLOCK_SIZE_48K * 2);

    // Make an audio message and send it to the listeners for processing
    Message msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K,
        BLOCK_SIZE_48K * 2, outBuffer, 0, idealCaptureMs);
    msg.setSource(_busId, _callId);
    msg.setDest(_destBusId, _destCallId);

    _captureConsumer.consume(msg);
}

void LineRadio::_analyzePlayedAudio(const int16_t* frame, unsigned frameLen) {   
    for (unsigned i = 0; i < frameLen; i++) {
        if (_record)
            _playFile << frame[i] << endl;
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
        char fname[32];
        snprintf(fname, 32, "capture-%u-%06u.txt", _startTimeMs, _captureRecordCounter);
        _captureFile.open(fname, std::ios_base::out);
    }
}

void LineRadio::_captureEnd() {

    if (_record) {
        _log.info("Ended audio capturing");
        _captureFile.close();
    }

    // Send the unkey message 
    Message msg(Message::Type::SIGNAL, Message::SignalType::RADIO_UNKEY, 0, 0, 0, _clock.time());
    msg.setSource(_busId, _callId);
    msg.setDest(_destBusId, _destCallId);
    _captureConsumer.consume(msg);
}

void LineRadio::_playStart() {
    if (_record) {
        _playRecordCounter++;
        _log.info("Started audio playing %u-%u", _startTimeMs, _playRecordCounter);
        char fname[32];
        snprintf(fname, 32, "play-%u-%06u.txt", _startTimeMs, _playRecordCounter);
        _playFile.open(fname, std::ios_base::out);
    }
    _tsFrameCount = 0;
}

void LineRadio::_playEnd() {
    if (_record) {
        _log.info("Ended audio playing");
        _playFile.close();
    }
}

void LineRadio::_setCosStatus(bool cosActive) {   
    // Look for transitions
    if (cosActive && !_cosActive) {

        _log.info("COS active");
        _cosActive = true;    

    } else if (!cosActive && _cosActive) {

        _log.info("COS inactive");
        _cosActive = false;

        // Generate an UNKEY signal on the negative transition
        Message msg(Message::Type::SIGNAL, Message::SignalType::RADIO_UNKEY, 
            0, 0, 0, _clock.time());
        msg.setSource(_busId, _callId);
        msg.setDest(_destBusId, _destCallId);
        _captureConsumer.consume(msg);
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
