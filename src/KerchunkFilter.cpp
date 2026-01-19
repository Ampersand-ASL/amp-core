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

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "Message.h"
#include "KerchunkFilter.h"

namespace kc1fsz {

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
            _saveAndDiscard(_queue);
            //_queue = std::queue<Message>();
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

void KerchunkFilter::consume(const Message& frame) {

    if (frame.isVoice()) {

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
    _captureFile.open("./kerchunk-capture.pcm", 
        std::ios::out | std::ios::app | std::ios::binary);
    while (!q.empty()) {
        Message m = q.front();
        q.pop();
        _captureFile.write((const char*)m.body(), m.size());
    }
    _captureFile.close();
}

}
