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

#include <cstdint>
#include <cassert>
#include <iostream>

#include "IAX2Util.h"

namespace kc1fsz {

/**
 * Used for passing events/data INTERNALLY within the application.
 */
class Message {
public:

    enum Type {
        NONE,
        AUDIO,
        AUDIO_INTERPOLATE,
        TEXT,
        SIGNAL
    };

    enum SignalType {
        SIGNALTYPE_NONE = 0,
        CALL_START,
        CALL_END,
        CALL_TERMINATE,
        RADIO_KEY,
        RADIO_UNKEY,
        // Requests network call/drop
        CALL_NODE,
        DROP_NODE,
        DROP_ALL_NODES,
        // Tells an audio interface that the COS signal has been activated/deactivated
        COS_ON,
        COS_OFF
    };

    // Message needs to be large enough for 20ms of PCM16 at 48K 
    // (This is 1920 bytes)
    static const unsigned MAX_SIZE = 160 * 6 * 2;

    Message();
    Message(Type type, unsigned format, unsigned size, const uint8_t* body,
        uint32_t origMs, uint32_t rxMs);
    Message(const Message& other);
    Message& operator=(const Message& other);

    Type getType() const { return _type; }
    bool isVoice() const { return _type == Type::AUDIO; }
    bool isSignal(SignalType st) const { return _type == Type::SIGNAL && _format == st; }
    unsigned getFormat() const { return _format; }

    unsigned size() const { return _size; }
    const uint8_t* body() const { return _body; }

    uint32_t getOrigMs() const { return _origMs; }
    uint32_t getRxMs() const { return _rxMs; }

    void setSource(unsigned busId, unsigned callId) { _sourceBusId = busId; _sourceCallId = callId; }
    void setDest(unsigned busId, unsigned callId){ _destBusId = busId; _destCallId = callId; }
    unsigned getSourceBusId() const { return _sourceBusId; }
    unsigned getSourceCallId() const { return _sourceCallId; }
    unsigned getDestBusId() const { return _destBusId; }
    unsigned getDestCallId() const { return _destCallId; }

    void clear();

    /**
     * Shortcut constructor
     */
    static Message signal(SignalType st) { return Message(Type::SIGNAL, st, 0, 0, 0, 0); }

private:

    Type _type;
    unsigned _format;
    unsigned _size;
    uint8_t _body[MAX_SIZE];
    uint32_t _origMs;
    uint32_t _rxMs;
    // Routing stuff
    unsigned _sourceBusId = 0, _sourceCallId = 0;
    unsigned _destBusId = 0, _destCallId = 0;
};

/**
 * The body of a CALL_START message type
 */
struct PayloadCallStart {
    CODECType codec;
    bool bypassJitterBuffer = false;
    uint32_t startMs;
    bool echo = false;
};

struct PayloadCall {
    char localNumber[16];
    char targetNumber[16];
};

}
