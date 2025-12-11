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

#include "kc1fsz-tools/fixedqueue.h"

#include "Message.h"
#include "LineRadio.h"

namespace kc1fsz {

class Log;
class MessageConsumer;
class Clock;

class LineUsb : public LineRadio {
public:

    LineUsb(Log&, Clock&, MessageConsumer& consumer, unsigned busId, unsigned callId,
        unsigned destBusId, unsigned destCallId);

    virtual void consume(const Message& frame);

    int open(const char* alsaDeviceName, const char* hidName);
    void close();

    // ----- Runnable ---------------------------------------------------------

    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);
    virtual bool run2();
    virtual void audioRateTick();

private:

    void _pollHidStatus();
    void _captureIfPossible();
    void _checkTimeouts();

    void _play(const Message& msg);
    void _playIfPossible();

    uint64_t _captureStartUs = 0;
    int64_t _captureSkewUs = 0;
    snd_pcm_t* _playH = 0;
    snd_pcm_t* _captureH = 0;
    int _hidFd = 0;

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

    uint32_t _captureCount = 0;
    bool _capturing = false;
    uint32_t _lastCapturedFrameMs = 0;
    // If we go silent for this amount of time the capture is assumed
    // to have ended. 
    uint32_t _captureSilenceIntervalMs = 20 * 4;

    // HID controls
    unsigned _hidCOSOffset = 0;
    uint8_t _hidCOSMask = 0x02;
    uint8_t _hidCOSActiveValue = 0x02;

    unsigned _hidPollCount = 0;
    bool _cosActive = false;
    // TODO
    bool _ctcssActive = true;

    // ----- Diagnostic/Statistical Data ----------------------------------------

    unsigned _playFrameCount = 0;
    unsigned _underrunCount = 0;
    unsigned _captureErrorCount = 0;
    unsigned _playErrorCount = 0;
};

}
