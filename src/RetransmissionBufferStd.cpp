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
#include <iostream>
#include "amp/RetransmissionBufferStd.h"

using namespace std;

namespace kc1fsz {
    namespace amp {

RetransmissionBufferStd::RetransmissionBufferStd()
:   _buffer(_bufferStore, BUFFER_CAPACITY) {
}

void RetransmissionBufferStd::reset() {
    _buffer.clear();
    _nextOutSeq = 0;
    _nextExpectedSeq = 0;
    _lastRetransmitAttemptMs = 0;
    _retransmitCount = 0;
}

bool RetransmissionBufferStd::setExpectedSeq(uint8_t n) {

    // Allow our internal high-water mark to advance if the new number
    // is larger. NOTE: There are some cases (PONG) when someone tries 
    // to set a lower expectation than had previously be set - this is 
    // ignored.
    if (compareWrap(n, _nextExpectedSeq) >= 0) 
        _nextExpectedSeq = n;

    // Remove everything that was just acknowledged.
    _buffer.removeIf([mark = _nextExpectedSeq](const IAX2FrameFull& frame) {
        bool remove = compareWrap(frame.getOSeqNo(), mark) < 0;
        return remove;
    });

    return true;
}

void RetransmissionBufferStd::poll(uint32_t elapsedMs, 
    std::function<void(const IAX2FrameFull&)> sink) {
    
    // Send anything that hasn't been sent yet (i.e. first time transmission)

    bool sentAnything;
    do {
        sentAnything = false;
        _buffer.visitIf(
            // Visitor
            // IMPORTANT: Get a reference here because we will be changing the
            // sequence number.
            [&nextOutSeq = _nextOutSeq, &sentAnything, sink](const IAX2FrameFull& frame) {
                sink(frame);
                // Bump the out sequence high-water mark forward so that we don't transmit 
                // this one again. Yes, this will wrap often.
                if (frame.getOSeqNo() == nextOutSeq) {
                    nextOutSeq++;
                }
                sentAnything = true;
                return true;
            },
            // Predicate - Do we have the next outbound message ready to go yet?
            // IMPORTANT: Get a reference here because we will be changing the
            // sequence number.
            [&nextOutSeq = _nextOutSeq](const IAX2FrameFull& frame) {
                return frame.getOSeqNo() == nextOutSeq;
            }
        );
    } while (sentAnything);

    // Retransmit things that haven't been acknowledged

    if (elapsedMs > _lastRetransmitAttemptMs + RETRANSMIT_INTERVAL_MS) {
        _buffer.visitIf(
            // Visitor
            [sink, context=this](const IAX2FrameFull& frame) {
                // Make a copy of the frame with the retransmission flag on
                IAX2FrameFull rf = frame;
                rf.setRetransmit();
                sink(rf);
                context->_retransmitCount++;
                return true;
            },
            // Predicate - Find the messages that have not been acknowledged yet
            [elapsedMs, nextExpectedSeq = _nextExpectedSeq](const IAX2FrameFull& frame) {
                return 
                    // This comparison is conceptually the same as:
                    // frame.getOSeqNo() >= nextExpectedSeq
                    compareWrap(frame.getOSeqNo(), nextExpectedSeq) >= 0 &&
                    // Message needs to have been hanging around long enough to 
                    // justify a retransmission.
                    elapsedMs > frame.getTimeStamp() + RETRANSMIT_INTERVAL_MS;
            }
        );
        _lastRetransmitAttemptMs = elapsedMs;
    }
}

void RetransmissionBufferStd::retransmitToSeq(uint8_t targetSeq,
    std::function<void(const IAX2FrameFull&)> sink) {

    _buffer.visitIf(
        // Visitor
        [sink, context=this](const IAX2FrameFull& frame) {
            // Make a copy of the frame with the retransmission flag on
            IAX2FrameFull rf = frame;
            rf.setRetransmit();
            sink(rf);
            context->_retransmitCount++;
            return true;
        },
        // Predicate - Find the messages that have not been acknowledged yet
        [targetSeq, nextExpectedSeq = _nextExpectedSeq](const IAX2FrameFull& frame) {
            return 
                // This comparison is conceptually the same as:
                // frame.getOSeqNo() >= nextExpectedSeq
                compareWrap(frame.getOSeqNo(), nextExpectedSeq) >= 0 &&
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
            // ### TODO: REAL LOG
            cout << "Rejected " << (int)frame.getOSeqNo() << " dup " << endl;
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
