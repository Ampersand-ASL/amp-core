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

#include "MessageConsumer.h"
#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;

/**
 * This is an experimental feature that provides a filter against
 * "kerchunks" and other short bursts of interference that might
 * be experienced on a line. The goal is to improve audio quality.
 *
 * The idea is to buffer the start of any transmission on an 
 * "untrusted" connection and only start to play it out if the activity 
 * is longer than a configurable duration. Any short transmissions 
 * can be discarded. No audio is lost because the playout happens
 * on a lag.
 */
class KerchunkFilter : public MessageConsumer, public Runnable2 {
public:

    static const unsigned BLOCK_SIZE_48K = 160 * 6;

    KerchunkFilter();

    void init(Log* log, Clock* clock);
    void setSink(std::function<void(const Message& msg)> sink) { _sink = sink; }
    void reset();
    void setEnabled(bool e) { _enabled = e; }

    /**
     * @param ms The number of milliseconds the filter waits before deciding
     * whether the transmission is legit. This is also the playout delay
     * for the first transmission.
     */
    void setEvaluationIntervalMs(unsigned ms) { _evaluationIntervalMs = ms; }

    // ----- Runnable2 --------------------------------------------------------

    void audioRateTick(uint32_t tickMs);

    // ----- MessageConsumer ---------------------------------------------------

    virtual void consume(const Message& frame);

private:

    void _saveAndDiscard(std::queue<Message>& q);
    static float _framePower(const Message& frame);

    enum State { 
        PASSING, 
        BUFFERING, 
        DRAINING 
    };

    Log* _log = 0;
    Clock* _clock = 0;
    bool _enabled = false;

    std::queue<MessageCarrier> _queue;
    
    bool _isActive = false;
    bool _isTrusted = false;
    State _state = State::PASSING;

    uint32_t _lastFrameMs = 0;
    uint32_t _lastActivityStartMs = 0;
    uint32_t _lastActivityEndMs = 0;
    uint32_t _bufferingStartMs = 0;

    // How long we wait before making a decision about whether a 
    // kerchunk has happened.
    unsigned _evaluationIntervalMs = 2000;
    // The amount of time that a channel remains trusted.
    unsigned _trustIntervalMs = 1 * 60 * 1000;
    // Used for detecting the trailing edge of activity
    unsigned _debounceIntervalMs = 250;

    std::function<void(const Message& msg)> _sink = nullptr;
};

}
