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
#include <cstring>
#include <cassert>
#include <iostream>
#include <algorithm>

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/fixedstring.h"

#include "IAX2Util.h"
#include "IAX2FrameFull.h"

using namespace std;

namespace kc1fsz {

const unsigned IAX2FrameFull::MAX_BUF_LEN;

IAX2FrameFull::IAX2FrameFull() {
}

IAX2FrameFull::IAX2FrameFull(const uint8_t* buf, unsigned bufLen) {
    // Be careful here, don't take more than we can hold
    unsigned acceptableLen = std::min(bufLen, MAX_BUF_LEN);
    std::memcpy(_buf, buf, acceptableLen);
    _bufLen = acceptableLen;
}

IAX2FrameFull::IAX2FrameFull(const IAX2FrameFull& other) {
    std::memcpy(_buf, other._buf, other._bufLen);
    _bufLen = other._bufLen;
}

uint16_t IAX2FrameFull::getSourceCallId() const {
    // Mask off the frame type flag
    return unpack_uint16_be(_buf + 0) & 0x7fff;
}

uint16_t IAX2FrameFull::getDestCallId() const {
    // Mask off the retransmission flag
    return unpack_uint16_be(_buf + 2) & 0x7fff;
}

bool IAX2FrameFull::isNoACKRequired() const {
    return isTypeClass(IAX2_TYPE_IAX, IAX2_SUBCLASS_IAX_ACK) ||
        isTypeClass(IAX2_TYPE_IAX, IAX2_SUBCLASS_IAX_VNAK) ||
        isTypeClass(IAX2_TYPE_IAX, IAX2_SUBCLASS_IAX_PING) ||
        isTypeClass(IAX2_TYPE_IAX, IAX2_SUBCLASS_IAX_LAGRQ);
}

/**
    When the following messages are received, an ACK MUST be sent in
    return: NEW, HANGUP, REJECT, ACCEPT, PONG, AUTHREP, REGREL, REGACK,
    REGREJ, TXREL.  ACKs SHOULD not be expected by any peer and their
    purpose is purely to force the transport layer to be up to date.
*/
bool IAX2FrameFull::isACKRequired() const {
    return 
        // NEW
        (getType() == 6 && getSubclass() == 1) ||
        // HANGUP
        (getType() == 6 && getSubclass() == 5) ||
        // REJECT
        (getType() == 6 && getSubclass() == 6) ||
        // ACCEPT
        (getType() == 6 && getSubclass() == 7) ||
        // LAGRP
        (getType() == 6 && getSubclass() == 12) ||
        // PONG
        (getType() == 6 && getSubclass() == 3) ||
        // ANSWER
        (getType() == 4 && getSubclass() == 4) ||
        // KEY
        (getType() == 4 && getSubclass() == 12) ||
        // UNKEY
        (getType() == 4 && getSubclass() == 13) ||
        // STOP_SOUNDS
        (getType() == 4 && getSubclass() == 255) ||
        // AUTHREP
        (getType() == 6 && getSubclass() == 9) ||
        // TEXT
        (getType() == 7 && getSubclass() == 0) ||
        // COMFORT
        (getType() == 10 ) ||
        // VOICE
        (getType() == 2 && getSubclass() == 4) ||
        // DTMF
        (getType() == 1) ||
        (getType() == 12);
}

// See RFC Section 7
// https://datatracker.ietf.org/doc/html/rfc5456#section-7
bool IAX2FrameFull::shouldIncrementSequence() const {
    if (isACK() || isTypeClass(0x6, 0xa))
        return false;
    else
        return true;
}

void IAX2FrameFull::setHeader(uint16_t sourceCallId, uint16_t destCallId,
    uint32_t callMs, uint8_t oSeqNo, uint8_t iSeqNo,
    uint8_t type, uint8_t subclass) {
    _buf[0] = 0b10000000 | ((sourceCallId >> 8) & 0b01111111);
    _buf[1] = sourceCallId & 0xff;
    _buf[2] = 0b00000000 | ((destCallId >> 8) & 0b01111111);
    _buf[3] = destCallId & 0xff;
    _buf[4] = (callMs >> 24) & 0xff;
    _buf[5] = (callMs >> 16) & 0xff;
    _buf[6] = (callMs >> 8) & 0xff;
    _buf[7] = (callMs >> 0) & 0xff;
    _buf[8] = oSeqNo;
    _buf[9] = iSeqNo;
    _buf[10] = type;
    _buf[11] = subclass;
}

void IAX2FrameFull::setBody(const uint8_t* body, unsigned bodyLen) {
    assert(bodyLen <= MAX_BODY_LEN);
    memcpy(_buf + 12, body, bodyLen);
    _bufLen = 12 + bodyLen;
}

unsigned IAX2FrameFull::_spaceLeft() const {
    return MAX_BUF_LEN - _bufLen;
}

void IAX2FrameFull::addIE_uint32(uint8_t id, uint32_t value) {
    assert(_spaceLeft() >= 6); 
    _buf[_bufLen] = id;
    _buf[_bufLen + 1] = 4;
    pack_uint32_be(value, _buf + _bufLen + 2);
    _bufLen += 6;
}

void IAX2FrameFull::addIE_uint16(uint8_t id, uint16_t value) {
    assert(_spaceLeft() >= 4); 
    _buf[_bufLen] = id;
    _buf[_bufLen + 1] = 2;
    pack_uint16_be(value, _buf + _bufLen + 2);
    _bufLen += 4;
}

void IAX2FrameFull::addIE_uint8(uint8_t id, uint8_t value) {
    assert(_spaceLeft() >= 3); 
    _buf[_bufLen] = id;
    _buf[_bufLen + 1] = 1;
    _buf[_bufLen + 2] = value;
    _bufLen += 3;
}

void IAX2FrameFull::addIE_str(uint8_t id, const char* value, unsigned valueLen) {
    assert(_spaceLeft() >= 2 + valueLen && valueLen < 255); 
    _buf[_bufLen] = id;
    _buf[_bufLen + 1] = valueLen;
    for (unsigned i = 0; i < valueLen; i++)
        _buf[_bufLen + 2 + i] = value[i];
    _bufLen += (2 + valueLen);
}

void IAX2FrameFull::addIE_str(uint8_t id, const char* value) {
    addIE_str(id, value, strlen(value));
}

void IAX2FrameFull::addIE_str(uint8_t id, const fixedstring& value) {
    addIE_str(id, value.c_str(), value.size());
}

void IAX2FrameFull::addIE_raw(uint8_t id, const uint8_t* value, unsigned valueLen) {
    assert(_spaceLeft() >= 2 + valueLen && valueLen < 255); 
    _buf[_bufLen] = id;
    _buf[_bufLen + 1] = valueLen;
    for (unsigned i = 0; i < valueLen; i++)
        _buf[_bufLen + 2 + i] = value[i];
    _bufLen += (2 + valueLen);
}

int extractIE(const uint8_t* packet, unsigned packetLen, 
    uint8_t id, uint8_t* buf, unsigned bufMaxLen) {
    // States
    // 0: At ID
    // 1: At len for the target ID
    // 2: At len for an ID we don't care about
    // 3: At a byte that we should accumulate
    // 4: At a byte that we should skip past
    unsigned state = 0;
    unsigned len = 0;
    unsigned j = 0;
    for (unsigned i = 0; i < packetLen; i++) {
        //cout << i << " " << state << endl;
        if (state == 0) {
            if (packet[i] == id) {
                state = 1;
            } else {
                state = 2;
            }
        } else if (state == 1) {
            len = packet[i];
            if (len == 0) {
                // Found what we want, but no data there
                return 0;
            } else {
                // Collect
                j = 0;
                state = 3;
            }
        } else if (state == 2) {
            len = packet[i];
            if (len == 0) {
                state = 0;
            } else {
                // Ignore
                j = 0;
                state = 4;
            }
        } else if (state == 3) {
            if (j < bufMaxLen)
                buf[j++] = packet[i];            
            else {
                // Problem, ran out of space
                return -1;
            }
            if (j == len) {
                return len;
            }
        } else if (state == 4) {
            j++;
            if (j == len) {
                state = 0;
            }
        }
    }
    // Didn't find the target
    return -1;
}

bool IAX2FrameFull::getIE_uint16(uint8_t id, uint16_t* result) const {
    uint8_t buf[2];
    int rc = extractIE(_buf + 12, _bufLen - 12, id, buf, 2);
    if (rc == 2) {
        *result = (buf[0] << 8) | buf[1];
        return true;
    }
    return false;
}

bool IAX2FrameFull::getIE_uint32(uint8_t id, uint32_t* result) const {
    uint8_t buf[4];
    int rc = extractIE(_buf + 12, _bufLen - 12, id, buf, 4);
    if (rc == 4) {
        *result = (buf[0] << 24) |(buf[1] << 16) | (buf[2] << 8) | buf[3];
        return true;
    }
    return false;
}

bool IAX2FrameFull::getIE_str(uint8_t id, char* buf, unsigned bufMaxLen) const {
    // Leave space for the null that will be added
    int rc = extractIE(_buf + 12, _bufLen - 12, id, (uint8_t*)buf, bufMaxLen - 1);
    if (rc == -1) {
        return false;
    } else {
        // Apply the null-termination
        buf[rc] = 0;
        return true;
    }
}

int IAX2FrameFull::getIE_raw(uint8_t id, uint8_t* buf, unsigned bufCapacity) const {
    int rc = extractIE(_buf + 12, _bufLen - 12, id, buf, bufCapacity);
    if (rc == -1)
        return -1;
    else 
        return rc;
}

}
