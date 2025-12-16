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

#include <queue>

#include "PCM16Frame.h"
#include "Runnable2.h"
#include "MessageConsumer.h"
#include "Message.h"
#include "BridgeIn.h"
#include "BridgeOut.h"

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

class Bridge;

class BridgeCall {
public:

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;
    static const unsigned SESSION_TIMEOUT_MS = 120 * 1000;
    static const unsigned LINE_ID = 10;
    static const unsigned CALL_ID = 1;

    BridgeCall();

    /**
     * One-time initialization. Connects the call to the outside world.
     */
    void init(Log* log, Clock* clock) {
        _log = log;
        _clock = clock;
        _bridgeIn.init(_log, _clock);
    }

    void setSink(MessageConsumer* sink) {
        _sink = sink;
    }

    void reset();

    void setup(unsigned lineId, unsigned callId, uint32_t startMs, CODECType codec,
        bool bypassJitterBuffer);

    bool isActive() const { 
        return _active; 
    }

    bool equals(const BridgeCall& other) const { 
        return _active && _lineId == other._lineId && _callId == other._callId; 
    }

    bool hasInputAudio() const { 
        return _stageIn.getType() == Message::Type::AUDIO; 
    }

    bool belongsTo(const Message& msg) const {
        return _active && msg.getSourceBusId() == _lineId && msg.getSourceCallId() == _callId;
    }

    void consume(const Message& frame);
    void audioRateTick();
    void contributeInputAudio(int16_t* pcmBlock, unsigned blockSize, float scale) const;    
    void setOutputAudio(const int16_t* pcmBlock, unsigned blockSize);  

private:

    Log* _log;
    Clock* _clock;
    MessageConsumer* _sink;

    enum Mode {
        NORMAL,
        PARROT,
        TONE
    };

    Mode _mode = Mode::PARROT;

    bool _active = false;
    unsigned _lineId = 0;
    unsigned _callId = 0;
    uint32_t _startMs = 0;
    uint32_t _lastAudioMs = 0;

    BridgeIn _bridgeIn;
    BridgeOut _bridgeOut;

    Message _makeMessage(const PCM16Frame& frame, 
        unsigned destLineId, unsigned destCallId) const;

    // ----- Normal Mode Related ----------------------------------------------

    void _processNormalAudio(const Message& msg);
    void _processNormalSignal(const Message& msg);

    // This is the call's contribution to the conference when in normal mode.
    // IMPORTANT: All of the signaling has been handled ahead of this point
    // so _stageIn will either be silence or audio.
    Message _stageIn;

    // ----- Tone Mode Related ------------------------------------------------

    void _toneAudioRateTick();

    bool _toneActive = false;
    float _toneOmega;
    float _tonePhi;
    float _toneLevel;

    // ----- Parrot Mode Related ----------------------------------------------

    void _processParrotAudio(const Message& msg);
    void _processParrotSignal(const Message& msg);

    void _parrotAudioRateTick();

    void _loadAudioFile(const char* fn, std::queue<PCM16Frame>& queue) const;
    void _loadSilence(unsigned ticks, std::queue<PCM16Frame>& queue) const;

    // The audio waiting to be sent to the caller in PCM16 48K format.
    std::queue<PCM16Frame> _playQueue;
    unsigned _playQueueDepth = 0;

    enum ParrotState {
        NONE,
        ACTIVE,
        CONNECTED,
        PLAYING_PROMPT_GREETING,
        WAITING_FOR_RECORD,
        RECORDING,
        PAUSE_AFTER_RECORD,
        PLAYING,
        TIMEDOUT
    };

    ParrotState _parrotState = ParrotState::NONE;
    uint32_t _parrotStateStartMs = 0;
};

    }
}
