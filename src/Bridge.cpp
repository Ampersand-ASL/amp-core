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
#include <iostream>
#include <cstring> 

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "Bridge.h"

using namespace std;

namespace kc1fsz {
    namespace amp {

Bridge::Bridge(Log& log, Clock& clock, BridgeCall::Mode defaultMode) 
:   _log(log),
    _clock(clock),
    _defaultMode(defaultMode),
    _calls(_callSpace, MAX_CALLS) { 
    for (unsigned i = 0; i < MAX_CALLS; i++) 
        _callSpace[i].init(&log, &clock);
}

void Bridge::reset() {
    _calls.visitAll(RESET_VISITOR);
}

void Bridge::setSink(MessageConsumer* sink) {
    _sink = sink;
    for (unsigned i = 0; i < MAX_CALLS; i++) 
        _callSpace[i].setSink(sink);
}

unsigned Bridge::getCallCount() const {
    unsigned result = 0;
    _calls.visitIf(
        // Visitor
        [&result](const BridgeCall& call) { 
            result++;
            return true;
        },
        // Predicate
        [](const BridgeCall& s) { return s.isActive(); }
    );
    return result;
}

void Bridge::consume(const Message& msg) {
    if (msg.getType() == Message::SIGNAL && 
        msg.getFormat() == Message::SignalType::CALL_START) {
        
        // Remove old/existing session for this call (if any)
        _calls.visitIf(
            // Visitor
            RESET_VISITOR,
            // Predicate
            [msg](const BridgeCall& s) { return s.belongsTo(msg); }
        );
        
        // Add new session for this call
        // #### TODO: CONSIDER POSITIVE ACK ON ACCEPTED CALL AND ELIMINATE
        // #### THE NACK CASE BELOW.
        int newIndex = _calls.firstIndex([](const BridgeCall& s) { return !s.isActive(); });
        if (newIndex == -1) {
            _log.info("Max sessions, rejecting call %d", msg.getSourceCallId());
            // #### TODO: NEED TO TEST THIS AFTER RACE CONDITION IS RESOLVED
            Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_TERMINATE, 0, 0,
                0, _clock.time());
            msg.setDest(msg.getSourceBusId(), msg.getSourceCallId());
            //_bus.consume(msg);
        }
        else {                
            PayloadCallStart payload;
            assert(msg.size() == sizeof(payload));
            memcpy(&payload, msg.body(), sizeof(payload));

            _log.info("Call %u started CODEC %X, jbBypass %d, echo %d", 
                msg.getSourceCallId(), payload.codec, payload.bypassJitterBuffer,
                payload.echo);

            BridgeCall& call = _calls.at(newIndex);
            call.setup(msg.getSourceBusId(), msg.getSourceCallId(), 
                payload.startMs, payload.codec, payload.bypassJitterBuffer, payload.echo, 
                _defaultMode);
        }
    }
    else if (msg.getType() == Message::SIGNAL && 
             msg.getFormat() == Message::SignalType::CALL_END) {

        _log.info("Call ended %d", msg.getSourceCallId());

        _calls.visitIf(
            // Visitor
            RESET_VISITOR,
            // Predicate
            [msg](const BridgeCall& c) { return c.belongsTo(msg); }
        );
    }
    // These are all the message types that get passed directly to the call.
    else if (msg.getType() == Message::AUDIO || 
             msg.getType() == Message::AUDIO_INTERPOLATE || 
             (msg.getType() == Message::SIGNAL && 
              msg.getFormat() == Message::SignalType::RADIO_UNKEY)) {

        _calls.visitIf(
            // Visitor
            [msg](BridgeCall& call) { 
                call.consume(msg);
                return false;
            },
            // Predicate
            [msg](const BridgeCall& s) { return s.belongsTo(msg); }
        );
    }
}

/**
 * This function is the heart of the conference bridge. On every audio tick we 
 * do the following:
 * 
 * 1. Ask each active (speaking) conference participant to prepare input audio 
 *    frame to contribute to the final mix.
 * 2. Prepare a mixed audio frame for each conference participant. This is 
 *    customized because not all participants will want to hear their own audio
 *    in the mix.
 * 3. Give each participant an output audio frame.
 */
void Bridge::audioRateTick(uint32_t tickMs) {

    // Tick each call so that we have an input frame for each.
    _calls.visitIf(
        // Visitor
        [tickMs](BridgeCall& call) { 
            // Tick the call to get it to produce an audio frame
            call.audioRateTick(tickMs);
            return true;
        },
        // Predicate
        [](const BridgeCall& s) { return s.isActive(); }
    );

    // Perform mixing and create an output for each active call
    for (unsigned i = 0; i < MAX_CALLS; i++) {
        
        if (!_calls[i].isActive())
            continue;

        // Figure out how many calls we are mixing. Keep in mind that calls only contribute
        // audio to themselves if echo mode is enabled for that call.
        unsigned mixCount = 0;
        for (unsigned j = 0; j < MAX_CALLS; j++) {
            if (!_calls[j].isActive() || !_calls[j].hasInputAudio() || 
                (!_calls[i].isEcho() && i == j))
                continue;
            mixCount++;
        }

        if (mixCount > 0) {

            // Figure out the scaling factor, which depends on how many calls
            // are contributing audio. 
            float mixScale = (mixCount == 0) ? 0 : 1.0f / (float)mixCount;

            // Now do the actual mixing
            int16_t mixedFrame[BLOCK_SIZE_48K];
            memset(mixedFrame, 0, BLOCK_SIZE_48K * sizeof(int16_t));
            for (unsigned j = 0; j < MAX_CALLS; j++) {
                if (!_calls[j].isActive() || !_calls[j].hasInputAudio() || 
                    (!_calls[i].isEcho() && i == j))
                    continue;
                _calls[j].extractInputAudio(mixedFrame, BLOCK_SIZE_48K, mixScale, tickMs);
            }

            // Output the result
            _calls[i].setOutputAudio(mixedFrame, BLOCK_SIZE_48K, tickMs);
        }
    }
}

void Bridge::oneSecTick() {
}

    }
}