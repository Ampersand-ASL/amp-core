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
#include <cmath>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "Message.h"
#include "KerchunkFilter.h"

namespace kc1fsz {

static const int vadPowerThreshold = -40;

KerchunkFilter::KerchunkFilter() {
}

void KerchunkFilter::init(Log* log, Clock* clock) {
    _log = log;
    _clock = clock;
}

void KerchunkFilter::reset() { 
    _state = State::PASSING;
    _lastFrameMs = 0;
    _lastActivityStartMs = 0;
    _lastActivityEndMs = 0;
    _bufferingStartMs = 0;
    _queue = std::queue<Message>();
}

void KerchunkFilter::audioRateTick(uint32_t tickMs) {

    // Look for falling edge of activity
    if (_isActive && _clock->isPast(_lastFrameMs + _debounceIntervalMs)) {
        _isActive = false;
        _lastActivityEndMs = _clock->time();
    }

    // Is trust established?
    if (!_isTrusted && _isActive && 
        _clock->isPast(_bufferingStartMs + _evaluationIntervalMs)) {
        _isTrusted = true;
    }

    // Is trust lost?
    if (_isTrusted && !_isActive && 
        _clock->isPast(_lastActivityEndMs + _trustIntervalMs)) {
        _isTrusted = false;
    }

    // Decide what to do about a buffering connection
    if (_state == State::BUFFERING) {
        if (_isTrusted) {
            _log->info("Not a kerchunk, playing");
            _state = State::DRAINING;
        } 
        else if (_clock->isPast(_bufferingStartMs + _evaluationIntervalMs)) {
            _log->info("Kerchunk was detected, flushing %d ms",
                _queue.size() * 20);
            //_saveAndDiscard(_queue);
            _queue = std::queue<Message>();
            _state = State::PASSING;
        }
    }
    // Look for the end of the playout
    else if (_state == State::DRAINING) {
        if (_queue.empty()) {
            _log->info("Kerchunk queue has been emptied");
            _state = State::PASSING;
        } else {
            _sink(_queue.front());
            _queue.pop();
        }
    }
}

float KerchunkFilter::_framePower(const Message& frame) {
    float sumSquare = 0;
    unsigned count = 0;
    const uint8_t* p = frame.body();
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i += 6, p += 12) {
        int16_t pcm = unpack_int16_le(p);
        float v = (float)pcm / 32767.0f;
        sumSquare += (v * v);
        count++;
    }
    sumSquare /= (float)count;
    return 10.0 * std::log10(sumSquare);
}

void KerchunkFilter::consume(const Message& frame) {

    // When disabled everything just passes through

    if (_enabled && frame.isVoice()) {

        // Level 1 - Discard leading frames that don't appear 
        // to contain valid audio.

        bool isLeadingFrame = _clock->isPast(_lastFrameMs + 10 * 1000);
        if (isLeadingFrame) {
            int power = _framePower(frame);
            if (power < vadPowerThreshold) {
                return;
            }
        }

        // Level 2 - 

        // Detect leading edge of activity
        if (!_isActive) {
            _isActive = true;
            _lastActivityStartMs = _clock->time();
        }

        // Check to see if a state change should be triggered
        if (_state == State::PASSING && !_isTrusted) {
            _log->info("Buffering a possible kerchunk");
            _state = State::BUFFERING;
            _bufferingStartMs = _clock->time();        
        }

        if (_state == State::PASSING) {
            _sink(frame);
        } else if (_state == State::BUFFERING || 
                _state == State::DRAINING) {
            _queue.push(frame);
        }

        _lastFrameMs = _clock->time();
    }
    else {
        _sink(frame);
    }
}

void KerchunkFilter::_saveAndDiscard(std::queue<Message>& q) {
    while (!q.empty()) {
        q.pop();
    }
}

}
