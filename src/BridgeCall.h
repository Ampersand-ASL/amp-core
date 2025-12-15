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

//#include "IAX2Util.h"
#include "Runnable2.h"
#include "MessageConsumer.h"
#include "Message.h"

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

class Bridge;

class BridgeCall {
public:

    /**
     * One-time initialization. Connects the call to the outside world.
     */
    void init(Log* log, Clock* clock) {
        _log = log;
        _clock = clock;
    }

    void setSink(MessageConsumer* sink) {
        _sink = sink;
    }

    void reset() {
        _active = false;
        _lineId = 0;  
        _callId = 0; 
        _startMs = 0; 
        _codec = CODECType::IAX2_CODEC_UNKNOWN;
    }

    void setup(unsigned lineId, unsigned callId, uint32_t startMs, CODECType codec) {
        _active = true;
        _lineId = lineId;  
        _callId = callId; 
        _startMs = startMs; 
        _codec = codec;
    }

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
        return _active && msg.getDestBusId() == _lineId && msg.getDestCallId() == _callId;
    }

    void consume(const Message& frame);
    void audioRateTick();
    void contributeInputAudio(int16_t* pcmBlock, unsigned blockSize, float scale) const;    
    void setOutputAudio(const int16_t* pcmBlock, unsigned blockSize);  

private:

    Log* _log;
    Clock* _clock;
    MessageConsumer* _sink;

    bool _active = false;
    unsigned _lineId = 0;
    unsigned _callId = 0;
    uint32_t _startMs = 0;
    CODECType _codec = CODECType::IAX2_CODEC_UNKNOWN;
    // IMPORTANT: All of the signaling has been handled ahead of this point
    // so _stageIn will either be silence or audio.
    Message _stageIn;
};

    }
}
