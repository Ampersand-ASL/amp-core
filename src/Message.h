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
        CALL_TERMINATE
    };

    // Message needs to be large enough for 20ms of PCM16 at 48K 
    static const unsigned MAX_SIZE = 160 * 6 * 2;

    Message();
    Message(Type type, unsigned format, unsigned size, const uint8_t* content,
        uint64_t originUs = 0);
    Message(const Message& other);
    Message& operator=(const Message& other);

    Type getType() const { return _type; }
    unsigned getFormat() const { return _format; }
    unsigned size() const { return _size; }
    const uint8_t* raw() const { return _body; }
    uint64_t getOriginUs() const { return _originUs; }
    void setOriginUs(uint64_t us) { _originUs = us; }

    void setSource(unsigned busId, unsigned callId) { _sourceBusId = busId; _sourceCallId = callId; }
    void setDest(unsigned busId, unsigned callId){ _destBusId = busId; _destCallId = callId; }
    unsigned getSourceBusId() const { return _sourceBusId; }
    unsigned getSourceCallId() const { return _sourceCallId; }
    unsigned getDestBusId() const { return _destBusId; }
    unsigned getDestCallId() const { return _destCallId; }

    static const unsigned BROADCAST = 0xffffffff;
    
private:

    Type _type = Type::NONE;
    unsigned _format = 0;
    unsigned _size = 0;
    uint8_t _body[MAX_SIZE];
    uint64_t _originUs = 0;
    // Routing stuff
    unsigned _sourceBusId = 0, _sourceCallId = 0;
    unsigned _destBusId = 0, _destCallId = 0;
};

/**
 * The body of a CALL_START message type
 */
struct PayloadCallStart {
    CODECType codec;
};

}
