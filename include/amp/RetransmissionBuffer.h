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

namespace kc1fsz {
    namespace amp {

/**
 * The abstract interface for a component that deals with the IAX retransmission
 * protocol. All reliable transmitted messages are sent via an instance of 
 * this module. Messages are saved internally and re-transmitted if necessary.
 */
class RetransmissionBuffer {
public:

    /**
     * Sets back to initial state, used at the start of a call.
     */
    virtual void reset() = 0;

    /**
     * Tells the buffer what the peer's next expected sequence number is.
     * This should be called any time a message is received from the peer
     * so that the peer's receive status can be tracked.
     * 
     * @returns True if the sequence was as expected, false if something
     * isn't right.
     */
    virtual bool setExpectedSeq(uint8_t n) = 0;

    /**
     * Give the buffer a chance to sent out any frames that it needs to. This
     * could either be new frames just consumed or old frames that need to 
     * be re-transmitted.
     * 
     * Should be call frequently, and definitely after the consume() method
     * has been called since it is likely that we'll need to forward that
     * new frame along.
     */
    virtual void poll(uint32_t elapsedMs, 
        std::function<void(const IAX2FrameFull&)> sink) = 0;

    /**
     * (Optional implementation) 
     * Allows the caller to request an immediate retransmission of any 
     * UNACKNOWLEDGED messages up to and including the specified sequence.
     * This could be used to handle VNAK messages in the IAX protocol.
     * 
     * Not doing this isn't the end of the world because the polling
     * logic should eventually come around and service the missing messages.
     */
    virtual void retransmitToSeq(uint8_t seq,
        std::function<void(const IAX2FrameFull&)> sink) { }

    /**
     * Frames can be consumed in any order, but they will be sent out
     * sequentially by sequence number. That means if a number is missing
     * the buffer will get stuck forwever waiting for it to be consumed.
     * 
     * @returns true if the frame was consumed, false if not (i.e. out of 
     * space)
     */
    virtual bool consume(const IAX2FrameFull& frame) = 0;

    /**
     * @returns true if there are no messages being retained for 
     * retransmission. This could be used during call shutdown 
     * to decide when the peer has received everything.
     */
    virtual bool empty() const = 0;

    /**
     * @returns The number of times a message needed to be retransmitted.
     */
    virtual unsigned getRetransmitCount() const = 0;

};
    }
}
