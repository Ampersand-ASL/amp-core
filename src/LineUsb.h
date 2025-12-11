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

// NOTE: This may be the real ALSA library or a mock, depending on the
// platfom that we are builing for.
#include <alsa/asoundlib.h>
// NOTE: This may be the real ARM library or a mock, depending on the
// platfom that we are building for.
#include <arm_math.h>

#include "kc1fsz-tools/DTMFDetector2.h"
#include "kc1fsz-tools/fixedqueue.h"
#include "amp/Resampler.h"

#include "Message.h"
#include "Line.h"

namespace kc1fsz {

class Log;
class MessageConsumer;
class Clock;

class LineUsb : public Line {
public:

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;

    LineUsb(Log&, Clock&, MessageConsumer& consumer, unsigned busId, unsigned callId,
        unsigned destBusId, unsigned destCallId);

    virtual void consume(const Message& frame);

    int open(const char* alsaDeviceName, const char* hidName);
    void close();
    void resetStatistics();

    /**
     * Utility
     */
    //static void g711ToPcm48(arm_fir_instance_q15* upsampleFilter,
    //    const uint8_t* inG711, unsigned inG711Len,
    //    int16_t* outPcm48, unsigned outPcm48Len);

    // ----- Runnable ---------------------------------------------------------

    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);
    virtual bool run2();
    virtual void audioRateTick();
    virtual void oneSecTick();

private:

    void _pollHidStatus();
    void _captureIfPossible();
    void _checkTimeouts();

    void _processCapturedAudio(const int16_t* frame, unsigned frameLen,
        uint64_t actualCaptureUs, uint64_t idealCaptureUs);
    void _play(const Message& msg);
    void _playIfPossible();

    void _analyzeCapturedAudio(const int16_t* frame, unsigned frameLen);
    void _analyzePlayedAudio(const int16_t* frame, unsigned frameLen);
    void _playStart();
    void _playEnd();
    void _captureStart();
    void _captureEnd();

    Log& _log;
    Clock& _clock;
    MessageConsumer& _captureConsumer;
    unsigned _busId, _callId;
    unsigned _destBusId, _destCallId;
    uint32_t _startTimeMs;
    uint64_t _captureStartUs = 0;
    int64_t _captureSkewUs = 0;
    snd_pcm_t* _playH = 0;
    snd_pcm_t* _captureH = 0;
    int _hidFd = 0;

    bool _record = false;

    bool _toneActive = false;
    float _toneAmp = 32767.0 * 0.5;
    float _toneOmega = 0;
    float _tonePhi = 0;
    uint32_t _toneStopMs = 0;

    // Buffer used to capture a full audio block. This is 
    // a mono 48k PCM buffer.
    int16_t _captureAccumulator[BLOCK_SIZE_48K];
    unsigned _captureAccumulatorSize = 0;

    // Buffer used to play a full audio block. We leave a bit of extra
    // space in case of slight timing differences between the SequencingBuffer
    // playout clock and the USB clock.
    static const unsigned PLAY_ACCUMULATOR_CAPACITY = BLOCK_SIZE_48K * 3; 
    int16_t _playAccumulator[PLAY_ACCUMULATOR_CAPACITY];
    unsigned _playAccumulatorSize = 0;

    bool _playing = false;
    bool _firstPlayOfTalkspurt = false;
    uint32_t _lastPlayedFrameMs = 0;
    // If we go silent for this amount of time the playback is assumed
    // to have ended. 
    uint32_t _playSilenceIntervalMs = 20 * 4;
    unsigned _playRecordCounter = 0;
    std::fstream _playFile;

    uint32_t _captureCount = 0;
    bool _capturing = false;
    uint32_t _lastCapturedFrameMs = 0;
    // If we go silent for this amount of time the capture is assumed
    // to have ended. 
    uint32_t _captureSilenceIntervalMs = 20 * 4;
    unsigned _captureRecordCounter = 0;
    std::fstream _captureFile;

    // HID controls
    unsigned _hidCOSOffset = 0;
    uint8_t _hidCOSMask = 0x02;
    uint8_t _hidCOSActiveValue = 0x02;

    unsigned _hidPollCount = 0;
    bool _cosActive = false;
    // TODO
    bool _ctcssActive = true;

    // This resampler is configured to go from 48K->8K ahead of the DTMF detection
    amp::Resampler _resampler;
    DTMFDetector2 _dtmfDetector;

    // ----- Diagnostic/Statistical Data ----------------------------------------

    unsigned _playFrameCount = 0;
    unsigned _underrunCount = 0;
    // Following David NR9V's standard
    int16_t _clipThreshold = 32432;
    unsigned _captureClips = 0;
    unsigned _playClips = 0;

    // Statistical analysis
    uint32_t _captureClipCount = 0;
    int16_t _capturePcmValueMax = 0;
    uint32_t _capturePcmValueSum = 0;
    uint32_t _capturePcmValueCount = 0;
    uint32_t _playClipCount = 0;
    int16_t _playPcmValueMax = 0;
    uint32_t _playPcmValueSum = 0;
    uint32_t _playPcmValueCount = 0;

    uint32_t _lastFullCaptureUs = 0;
    uint32_t _captureGapTotal = 0;
    uint32_t _captureGapCount = 0;

    unsigned _captureErrorCount = 0;
    unsigned _playErrorCount = 0;
};

}
