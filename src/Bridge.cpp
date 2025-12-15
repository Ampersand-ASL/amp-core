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
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "Bridge.h"

using namespace std;

namespace kc1fsz {
    namespace amp {

Bridge::Bridge(Log& log, Clock& clock) 
:   _log(log),
    _clock(clock),
    _calls(_callSpace, MAX_CALLS) { 
    for (unsigned i = 0; i < MAX_CALLS; i++) 
        _callSpace[i].init(&log, &clock);
}

void Bridge::setSink(MessageConsumer* sink) {
    _sink = sink;
    for (unsigned i = 0; i < MAX_CALLS; i++) 
        _callSpace[i].setSink(sink);
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
        int newIndex = _calls.firstIndex([](const BridgeCall& s) { return !s.isActive(); });
        if (newIndex == -1) {
            _log.info("Max sessions, rejecting call %d", msg.getSourceCallId());
            // #### TODO: NEED TO TEST THIS AFTER RACE CONDITION IS RESOLVED
            Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_TERMINATE, 0, 0,
                0, _clock.timeUs());
            msg.setDest(msg.getSourceBusId(), msg.getSourceCallId());
            //_bus.consume(msg);
        }
        else {                
            PayloadCallStart payload;
            assert(msg.size() == sizeof(payload));
            memcpy(&payload, msg.body(), sizeof(payload));

            _log.info("Call started %d CODEC %X", msg.getSourceCallId(), payload.codec);

            BridgeCall& call = _calls.at(newIndex);
            call.setup(msg.getSourceBusId(), msg.getSourceCallId(), msg.getRxUs() / 1000,
                payload.codec);
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

void Bridge::audioRateTick() {

    // Tick each call so that we have an input frame for each.
    _calls.visitIf(
        // Visitor
        [](BridgeCall& call) { 
            // Tick the call to get it to produce an audio frame
            call.audioRateTick();
            return true;
        },
        // Predicate
        [](const BridgeCall& s) { return s.isActive(); }
    );

    // Perform mixing and create an output for each active call
    _calls.visitIf(
        // Visitor
        [this](BridgeCall& call) { 

            // Figure out how many calls we are mixing. Keep in mind that calls don't contribute
            // audio to themselves.
            unsigned mixCount = 0;
            this->_calls.visitIf(
                // Visitor
                [&mixCount](const BridgeCall& innerCall) { 
                    mixCount++;
                    return true;
                },
                // Predicate
                [call](const BridgeCall& innerCall) { 
                    return innerCall.isActive() && 
                        innerCall.hasInputAudio() &&
                        // NOTE: A call will not contribute audio to itself!
                        !innerCall.equals(call);
                }
            );

            // Figure out the scaling factor, which depends on how many calls
            // are contributing audio. 
            float mixScale = (mixCount == 0) ? 0 : 1.0f / (float)mixCount;

            // Now do the actual mixing
            int16_t mixedFrame[BLOCK_SIZE_48K];
            memset(mixedFrame, 0, BLOCK_SIZE_48K * sizeof(int16_t));

            this->_calls.visitIf(
                // Visitor
                [&mixedFrame, mixScale](const BridgeCall& innerCall) { 
                    innerCall.contributeInputAudio(mixedFrame, BLOCK_SIZE_48K, mixScale);
                    return true;
                },
                // Predicate
                [call](const BridgeCall& innerCall) { 
                    return innerCall.isActive() && 
                        innerCall.hasInputAudio() &&
                        // NOTE: A call will not contribute audio to itself!
                        !innerCall.equals(call);
                }
            );

            call.setOutputAudio(mixedFrame, BLOCK_SIZE_48K);

            return true;
        },
        // Predicate
        [](const BridgeCall& s) { return s.isActive(); }
    );
}

    }
}