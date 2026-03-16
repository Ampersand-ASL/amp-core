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

#include <functional>
#include <queue>
#include <cstdint>

#include "PCM16Frame.h"

namespace kc1fsz {

class Log;
class Clock;

class TxMgr : public Runnable2 {
public:

    TxMgr(Log& log, Clock& clock,
        std::function<void(const int16_t* frame, unsigned frameLen)> audioGenerateCb,
        std::function<void(bool active)> pttControlCb,
        std::function<void(bool active, bool secondary)> toneControlCb);

    void reset();

    void consumeConferenceFrame(const int16_t* frame, unsigned frameLen);
    void consumeTelemetryFrame(const int16_t* frame, unsigned frameLen);

    void setCourtesy(bool b);
    void setToneOnTelemetry(bool b);
    void setAudioDelayMs(unsigned ms);
    void setHangMs(unsigned ms);
    void setChickenDelayMs(unsigned ms);
    void setToneBreak(bool b);

    enum ToneType { TONE_NONE, TONE_CTCSS, TONE_DCS };
    
    // ----- Runnable2 --------------------------------------------------------

    bool run2();
    void audioRateTick();

private:

    Log& _log;
    Clock& _clock;
    const std::function<void(const int16_t* frame, unsigned frameLen)> _audioGenerateCb;
    const std::function<void(bool active)> _pttControlCb;
    const std::function<void(bool active, bool secondary)> _toneControlCb;

    enum State { 
        // In this state nothing is happening
        STATE_IDLE, 
        // This state is used when audio starts flowing in. The transmitter
        // is keyed and tone is being generated (warming up), but audio is not 
        // allowed to flow forward yet (delay buffer filling).
        STATE_PRE_TX, 
        // In this state the audio is flowing out
        STATE_TX, 
        // In this state tone is turned off or reversed but the transmitter is 
        // still keyed. The goal is to get the receivers to stop as cleanly 
        // as possible. Audio is still allowed to flow forward, but some amount
        // of it will be cut off once the chicken interval expires.
        STATE_CHICKEN 
    } _state;

    uint64_t _stateStartMs;

    unsigned _audioDelayMs;
    unsigned _hangMs;
    unsigned _chickenDelayMs;
    unsigned _toneBreak;
};

}
