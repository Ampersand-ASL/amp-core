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
//#include <iostream>

#include "kc1fsz-tools/Log.h"
#include "amp/RetransmissionBufferStd.h"

using namespace std;

namespace kc1fsz {
    namespace amp {

RetransmissionBufferStd::RetransmissionBufferStd()
:   _buffer(_bufferStore, BUFFER_CAPACITY) {
}

void RetransmissionBufferStd::reset() {
    _buffer.clear();
    _nextExpectedSeq = 0;
    _retransmitCount = 0;
}

bool RetransmissionBufferStd::setExpectedSeq(uint8_t n) {

    // Allow our internal high-water mark to advance if the new number
    // is larger. NOTE: There are some cases (PONG) when someone tries 
    // to set a lower expectation than had previously be set - this is 
    // ignored.
    if (compareWrap(n, _nextExpectedSeq) >= 0) {
        _nextExpectedSeq = n;
        // Remove everything that was just acknowledged.
        _buffer.removeIf([mark = _nextExpectedSeq](const IAX2FrameFull& frame) {
            bool remove = compareWrap(frame.getOSeqNo(), mark) < 0;
            return remove;
        });
        return true;
    } else {
        return false;
    }
}

void RetransmissionBufferStd::retransmitIfNecessary(uint32_t elapsedMs, 
    uint8_t expectedInSeqNo, std::function<void(const IAX2FrameFull&)> sink) {    

    _buffer.visitIf(
        // Visitor
        [this, sink, expectedInSeqNo](const IAX2FrameFull& frame) {
            // Make a copy of the frame with the retransmission flag on and 
            // the expected sequence number adjusted to match reality
            IAX2FrameFull rf = frame;
            rf.setRetransmit();
            rf.setISeqNo(expectedInSeqNo);
            _log->info("Call %d/%d retransmitting %d",
                frame.getDestCallId(), frame.getSourceCallId(), 
                frame.getOSeqNo());
            sink(rf);
            _retransmitCount++;
            return true;
        },
        // Predicate - Find the messages that have not been acknowledged yet
        [this, elapsedMs](const IAX2FrameFull& frame) {
            return 
                // This comparison is conceptually the same as:
                // frame.getOSeqNo() >= nextExpectedSeq
                compareWrap(frame.getOSeqNo(), _nextExpectedSeq) >= 0 &&
                // Message needs to have been hanging around long enough to 
                // justify a retransmission.
                elapsedMs > frame.getTimeStamp() + RETRANSMIT_INTERVAL_MS;
        }
    );
}

void RetransmissionBufferStd::retransmitToSeq(uint8_t targetSeq,
    uint8_t expectedInSeqNo, 
    std::function<void(const IAX2FrameFull&)> sink) {

    _buffer.visitIf(
        // Visitor
        [this, sink, expectedInSeqNo](const IAX2FrameFull& frame) {
            // Make a copy of the frame with the retransmission flag on and 
            // the expected sequence number adjusted to match reality
            IAX2FrameFull rf = frame;
            rf.setRetransmit();
            rf.setISeqNo(expectedInSeqNo);
            sink(rf);
            _retransmitCount++;
            return true;
        },
        // Predicate - Find the messages that have not been acknowledged yet
        [this, targetSeq](const IAX2FrameFull& frame) {
            return 
                // This comparison is conceptually the same as:
                // frame.getOSeqNo() >= nextExpectedSeq
                compareWrap(frame.getOSeqNo(), _nextExpectedSeq) >= 0 &&
                // Limit to the target specified
                compareWrap(frame.getOSeqNo(), targetSeq) <= 0;
        }
    );
}

bool RetransmissionBufferStd::consume(const IAX2FrameFull& frame) {
    if (_buffer.hasCapacity()) {
        // Check for duplicates
        if (_buffer.countIf([seq=frame.getOSeqNo()](const IAX2FrameFull& frame) {
                return frame.getOSeqNo() == seq;
            }) == 0) {
            _buffer.push(frame);
            return true;
        } else {
            _log->info("Retransmission buffer rejected duplicate %d", (int)frame.getOSeqNo());
            return false;
        }
    }
    else 
        return false;
}

int RetransmissionBufferStd::compareWrap(uint8_t a, uint8_t b) {
    if (a == b)
        return 0;
    else if (a < 0x80) {
        if (b > a && b < a + 0x80)
            return -1;
        else 
            return 1;
    } else {
        if (b < a && b > a - 0x80)
            return 1;
        else
            return -1;
    }
}
    }
}
