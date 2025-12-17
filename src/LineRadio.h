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
#pragma once

#include <fstream>

#include "kc1fsz-tools/DTMFDetector2.h"
#include "amp/Resampler.h"
// #### TODO: MOVE INCLUDE FILES FOR THIS PROJECT
#include "AudioCoreOutputPort.h"

#include "Line.h"

namespace kc1fsz {

class Log;
class MessageConsumer;
class Clock;

class LineRadio : public Line, public AudioCoreOutputPort {
public:

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;

    LineRadio(Log&, Clock&, MessageConsumer& consumer, unsigned busId, unsigned callId,
        unsigned destBusId, unsigned destCallId);
    void resetStatistics();

    // ----- MessageConsumer -------------------------------------------------
    
    void consume(const Message& frame);

    // ----- Runnable2 ---------------------------------------------------------

    virtual void oneSecTick();

    // ----- AudioCoreOutputPort ------------------------------------------------

    virtual bool isAudioActive() const;
    virtual void setToneEnabled(bool b);
    virtual void setToneFreq(float hz);
    virtual void setToneLevel(float dbv);
    virtual void resetDelay();

protected:

    /**
     * This function is called to do the actual playing of the 48K PCM.
     */
    virtual void _playPCM48k(int16_t* pcm48k_2, unsigned blockSize) = 0;

    void _checkTimeouts();

    void _analyzePlayedAudio(const int16_t* frame, unsigned frameLen);
    void _playStart();
    void _playEnd();

    void _analyzeCapturedAudio(const int16_t* frame, unsigned frameLen);
    void _processCapturedAudio(const int16_t* frame, unsigned frameLen,
        uint32_t actualCaptureMs, uint32_t idealCaptureMs);

    void _captureStart();
    void _captureEnd();

    void _setCosStatus(bool cos);

    void _open(bool echo = false);
    void _close();

    Log& _log;
    Clock& _clock;
    MessageConsumer& _captureConsumer;
    unsigned _busId, _callId;
    unsigned _destBusId, _destCallId;
    uint32_t _startTimeMs;

    // This resampler is configured to go from 48K->8K ahead of the DTMF detection
    amp::Resampler _resampler;
    DTMFDetector2 _dtmfDetector;

    bool _cosActive = false;
    bool _ctcssActive = true;

    bool _playing = false;
    bool _capturing = false;
    uint32_t _lastPlayedFrameMs = 0;
    uint32_t _lastCapturedFrameMs = 0;
    // If we go silent for this amount of time the playback is assumed
    // to have ended. 
    uint32_t _playSilenceIntervalMs = 20 * 4;
    // If we go silent for this amount of time the capture is assumed to have ended. 
    uint32_t _captureSilenceIntervalMs = 20 * 4;

    bool _record = false;
    std::fstream _playFile;
    std::fstream _captureFile;
    unsigned _playRecordCounter = 0;
    unsigned _captureRecordCounter = 0;

    // Statistical analysis
    uint32_t _captureClipCount = 0;
    int16_t _capturePcmValueMax = 0;
    uint32_t _capturePcmValueSum = 0;
    uint32_t _capturePcmValueCount = 0;
    uint32_t _playClipCount = 0;
    int16_t _playPcmValueMax = 0;
    uint32_t _playPcmValueSum = 0;
    uint32_t _playPcmValueCount = 0;
    uint32_t _lastFullCaptureMs = 0;
    uint32_t _captureGapTotal = 0;
    uint32_t _captureGapCount = 0;
    // Following David NR9V's standard
    int16_t _clipThreshold = 32432;
    unsigned _captureClips = 0;
    unsigned _playClips = 0;
};

}
