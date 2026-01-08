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
#include <cmath>

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

    /**
     * @param cardNumber The ALSA card number. So the device name is hd:<cardNumber>.
     * @param playLevelL Used to set the "Speaker Playback Volume" control on the left
     * side. Range is 0 to 100.
     * @param playLevelR Used to set the "Speaker Playback Volume" control on the right
     * side. Range is 0 to 100.
     * @param captureLevel Used to set the "Mic Capture Volume" control. Range is 
     * 0 to 100.
     */
    int open(int cardNumber, int playLevelL, int playLevelR, int captureLevel);

    void close();

    unsigned getUnderrunCount() const { return _underrunCount; }

    // ----- MessageConsumer --------------------------------------------------

    virtual void consume(const Message& frame);

    // ----- Runnable ---------------------------------------------------------

    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);
    virtual bool run2();
    virtual void tenSecTick();

protected:

    void _playStart();

    /**
     * This function is called to do the actual playing of the 48K PCM.
     */
    virtual void _playPCM48k(int16_t* pcm48k_2, unsigned blockSize);

private:

    void _captureIfPossible();
    void _playIfPossible();

    static void _sndCloser(snd_pcm_t* h) { snd_pcm_close(h); }

    uint32_t _captureStartMs = 0;
    snd_pcm_t* _playH = 0;
    snd_pcm_t* _captureH = 0;

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

    bool _startOfTs = false;
    uint32_t _captureCount = 0;
    bool _inError = false;

    // ----- Diagnostic/Statistical Data ----------------------------------------

    unsigned _playFrameCount = 0;
    unsigned _underrunCount = 0;
    unsigned _underrunCountReported = 0;
    unsigned _captureErrorCount = 0;
    unsigned _playErrorCount = 0;
};

// ====== Utility Functions ======================================================

/**
 * Sets the value on a mixer element
 */
int setMixer(const char* deviceName, const char *paramName, unsigned count, int v1, int v2);
int setMixer1(const char* deviceName, const char *paramName, int v);
int setMixer2(const char* deviceName, const char *paramName, int v1, int v2);

/**
 * @returns 0 if the range was obtained. -1 means that the device name was invalid. -2 means 
 * that the parameter is not found.
 */
int getMixerRange(const char* deviceName, const char* paramName, int* minV, int* maxV);

int getConvertMixerValueToDb(const char* deviceName, const char* paramName, int value, float* db);

}
