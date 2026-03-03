/**
 * Copyright (C) 2026, Bruce MacKinnon KC1FSZ
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

#include "PCM16Frame.h"
#include "Line.h"
#include "IAX2Util.h"
#include "Message.h"
#include "MessageConsumer.h"

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

class LineParrot : public Line {
public:

    static const unsigned BLOCK_SIZE_8K = 160;

    /**
     * @param consumer This is the sink interface that received messages
     * will be sent to. 
     */
    LineParrot(Log& log, Clock& clock, unsigned lineId, MessageConsumer& consumer, 
        unsigned audioDestLineId);

    int open();

    /**
     * Resets all calls as a side-effect.
     */
    void close();
    
    void setTrace(bool a) { _trace = a; }

    // ----- Line/MessageConsumer-----------------------------------------------------

    virtual void consume(const Message& m);

    // ----- Runnable -------------------------------------------------------

    virtual bool run2();  
    virtual void audioRateTick(uint32_t tickTimeMs);
    virtual void oneSecTick();

private:

    enum State {
        STATE_INIT,
        STATE_LISTENING,
        STATE_RECORDING,
        STATE_PLAYING
    };

    void _setState(State state);
    void _endRecording();

    // #### TODO: CONSIDER MOVING SOME OF THIS STUFF TO Line
    void _sendSignal(Message::SignalType type, void* body, unsigned len);
    void _sendSignal(Message::SignalType type, void* body, unsigned len,
        unsigned destLineId, unsigned destCallId);

    Log& _log;
    Clock& _clock;
    const unsigned _lineId;
    const unsigned _callId = 1;
    MessageConsumer& _bus;
    // Where the inbound audio gets sent
    const unsigned _audioDestLineId;
    // Enables detailed network tracing
    bool _trace = false;

    State _state = State::STATE_INIT;
    uint64_t _stateStartMs = 0;

    // The audio captured from the caller
    std::queue<PCM16Frame> _captureQueue;
    unsigned _captureQueueDepth = 0;

    std::queue<PCM16Frame> _playQueue;
};
    }
}
