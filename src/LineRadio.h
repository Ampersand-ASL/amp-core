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
#include "IDToneGenerator.h"
#include "Tx.h"
#include "TxControl.h"

#include "Line.h"

namespace kc1fsz {

class Log;
class MessageConsumer;
class Clock;

class LineRadio : public Line, public AudioCoreOutputPort, public Tx {
public:

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;

    LineRadio(Log&, Clock&, MessageConsumer& consumer, unsigned busId, unsigned callId,
        unsigned destBusId, unsigned destCallId);
    void resetStatistics();

    /**
     * Example for sanity: 0dBv is 0.5 Vp.
     */
    static float dbvToPeak(float dbv) {
        return pow(10, (dbv / 20)) * 0.5;
    }

    static float dbVfs(int16_t v) {
        float fv = (float)v / 32767.0;
        if (fv == 0)
            return -96;
        return 20.0 * log10(fv);
    }

    // ----- MessageConsumer -------------------------------------------------
    
    void consume(const Message& frame);

    // ----- Runnable2 ---------------------------------------------------------
    
    virtual void oneSecTick();
    virtual void audioRateTick(uint32_t tickMs);

    // ----- AudioCoreOutputPort ------------------------------------------------

    virtual bool isAudioActive() const { return _playing; }
    virtual void setToneEnabled(bool b);
    virtual void setToneFreq(float hz);
    virtual void setToneLevel(float dbv);
    virtual void resetDelay();

    // ----- Tx ------------------------------------------------------------------

    // #### TODO: CONSOLIDATE RUNNABLE STUFF
    virtual void run() { }
    virtual int getId() const { return 0; }
    virtual void setEnabled(bool en) { }
    virtual bool getEnabled() const { return true; }
    virtual void setPtt(bool ptt) { }
    virtual bool getPtt() const { return false; } 
    // Tx Configuration 
    virtual void setPLToneMode(PLToneMode mode) { }
    virtual void setPLToneFreq(float hz) { }
    virtual void setPLToneLevel(float db) { }
    virtual void setCtMode(CourtesyToneGenerator::Type ctType) { }
    // ### TODO: IS THIS NEEDED ANYMORE?
    virtual CourtesyToneGenerator::Type getCourtesyType() const { 
        return CourtesyToneGenerator::Type::FAST_UPCHIRP; 
    }

protected:

    /**
     * This function is called to do the actual playing of the 48K PCM.
     */
    virtual void _playPCM48k(int16_t* pcm48k_2, unsigned blockSize) = 0;

    void _checkTimeouts();

    void _generateToneFrame();
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

    bool _toneActive = false;
    float _toneAmp = 32767.0 * 0.5;
    float _toneOmega = 0;
    float _tonePhi = 0;

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

    TxControl _txControl;

private:

    bool _playing = false;
};

}
