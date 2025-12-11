/**
 * Copyright (C) 2025, Bruce MacKinnon KC1FSZ, All Rights Reserved
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

#include "kc1fsz-tools/fixedqueue.h"

#include "IAX2FrameFull.h"
#include "amp/RetransmissionBuffer.h"

namespace kc1fsz {
    namespace amp {
class RetransmissionBufferStd : public RetransmissionBuffer {
public:

    RetransmissionBufferStd();

    // ----- From RetransmissionBuffer ----------------------------------------

    virtual void reset();
    virtual bool setExpectedSeq(uint8_t );
    virtual void poll(uint32_t elapsedMs, 
        std::function<void(const IAX2FrameFull&)> sink);
    virtual void retransmitToSeq(uint8_t seq,
        std::function<void(const IAX2FrameFull&)> sink);
    virtual bool empty() const { return _buffer.empty(); }
    virtual unsigned getRetransmitCount() const { return _retransmitCount; }

    /**
     * A strange kind of comparison that takes into account wrapping,
     * assuming that the two values are within 128 of each other.
     * For example, 0xfd < 0x04 because the RHS value is assumed to 
     * have just wrapped around.
     */
    static int compareWrap(uint8_t a, uint8_t b);

    // ----- From FrameSink --------------------------------------------------

    virtual bool consume(const IAX2FrameFull& frame);

private:

    static const unsigned BUFFER_CAPACITY = 16;
    IAX2FrameFull _bufferStore[BUFFER_CAPACITY];
    fixedqueue<IAX2FrameFull> _buffer;

    uint8_t _nextOutSeq = 0;
    uint8_t _nextExpectedSeq = 0;
    uint32_t _lastRetransmitAttemptMs = 0;
    unsigned _retransmitCount = 0;
    
    // Controls how quickly we start to re-transmit on missing ACK.
    // Is appears that we need to be pretty aggressive about this 
    // to keep Asterisk happy.
    static const unsigned RETRANSMIT_INTERVAL_MS = 2000;
};
    }
}
