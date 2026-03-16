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

#include <functiomnal>
#include <queue>
#include <cstdint>

#include "PCM16Frame.h"

namespace kc1fsz {

class Log;
class Clock;

class RxMgr : public Runnable2 {
public:

    RxMgr(Log& log, Clock& clock,
        std::function<void(const int16_t* frame, unsigned frameLen)> audioGenerateCb);

    void reset();

    void consumeAudioFrame(const int16_t* frame, unsigned frameLen);
    void setCosState(bool b);
    void setToneState(bool b);

    // Configuration 
    void setCosSquelchEnabled(bool b);
    void setToneSquelchEnabled(bool b);
    void setAudioDelayMs(unsigned ms);
    void setDebounceMs(unsigned ms);

    // #### TODO: QUESTION - TO TONE AND COS NEED TO HAVE INDEPENDENT DELAY SETTINGS?

    // ----- Runnable2 --------------------------------------------------------

    bool run2();
    void audioRateTick();

private:

    Log& _log;
    Clock& _clock;
    const std::function<void(const int16_t* frame, unsigned frameLen)> _audioGenerateCb;

    enum State { 
        // Nothing happening
        STATE_IDLE, 
        // The COS/Tone signals have activated and we are waiting to see if they 
        // are stable throughout the debounce interval.
        STATE_DEBOUNCE,
        // Audio is actively being received but it is going into the delay line 
        // before being allowed into the next stage.
        STATE_PRE_RX, 
        // Audio is actively being received and is actively being played out of the 
        // delay line.
        STATE_RX 
    } _state;

    uint64_t _stateStartMs;

    bool _cosSquelch;
    bool _toneSquelch;
    unsigned _audioDelayMs;
    unsigned _debounceMs;

    bool _cosState;
    bool _toneState;
};

}
