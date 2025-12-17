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
    _dtmfDetector(clock, BLOCK_SIZE_8K / 2) {
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

 void LineRadio::consume(const Message& msg) {

    if (msg.getType() == Message::Type::AUDIO) {

        // Detect transitions from silence to playing
        if (!_playing) {
            _playStart();
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

        // Call down to do the actual play on the hardware
        _playPCM48k(pcm48k_2, BLOCK_SIZE_48K);

        _lastPlayedFrameMs = _clock.time();
        _playing = true;
    }
}

static float dbVfs(int16_t v) {
    float fv = (float)v / 32767.0;
    if (fv == 0)
        return -96;
    return 20.0 * log10(fv);
}

void LineRadio::oneSecTick() {
    // Stats
    if (_capturePcmValueCount) {
        uint32_t avg = _capturePcmValueSum / _capturePcmValueCount;
        _log.info("TXLEVEL %6u %5.1f %5.1f", _captureClipCount, dbVfs(_capturePcmValueMax), dbVfs(avg));
    }
    /*
    if (_playPcmValueCount) {
        uint32_t avg = _playPcmValueSum / _playPcmValueCount;
        _log.info("RXLEVEL %6u %5.1f %5.1f", _playClipCount, dbVfs(_playPcmValueMax), dbVfs(avg));
    }
    */
    resetStatistics();

    if (_captureGapTotal) {
        //_log.info("Capture gap avg (us) %u", _captureGapTotal / _captureGapCount);
        _captureGapTotal = 0;
        _captureGapCount = 0;
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
    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
        sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
    msg.setSource(_busId, _callId);
    msg.setDest(_destBusId, _destCallId);
    _captureConsumer.consume(msg);
}

void LineRadio::_close() {
    // Generate the same kind of call start message that would
    // come from the IAX2Line after a new connection.
    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_END, 
        0, 0, 0, _clock.time());
    msg.setSource(_busId, _callId);
    msg.setDest(_destBusId, _destCallId);
    _captureConsumer.consume(msg);
}

void LineRadio::_analyzeCapturedAudio(const int16_t* frame, unsigned frameLen) {

    // Jitter stats
    if (!_lastFullCaptureMs == 0) {
        uint32_t gap = _clock.time() - _lastFullCaptureMs;
        _captureGapTotal += gap;
        _captureGapCount++;
    }
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

bool LineRadio::isAudioActive() const {
    return false;
}

void LineRadio::setToneEnabled(bool b) {
}

void LineRadio::setToneFreq(float hz) {
}

void LineRadio::setToneLevel(float dbv) {
}

void LineRadio::resetDelay() {   
}

}
