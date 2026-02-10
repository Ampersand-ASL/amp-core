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

#include "kc1fsz-tools/Common.h"

namespace kc1fsz {

class fixedstring;

class IAX2FrameFull {
public:

    static constexpr unsigned MAX_BUF_LEN = 1500;
    static constexpr unsigned MAX_BODY_LEN = MAX_BUF_LEN - 12;

    IAX2FrameFull();
    IAX2FrameFull(const uint8_t* buf, unsigned bufLen);
    IAX2FrameFull(const IAX2FrameFull& other);
    
    const uint8_t* buf() const { return _buf; }
    unsigned size() const { return _bufLen; }

    void setHeader(uint16_t sourceCallId, uint16_t destCallId,
        uint32_t timeStamp, uint8_t oSeqNo, uint8_t iSeqNo,
        uint8_t type, uint8_t subclass);
    void setBody(const uint8_t* body, unsigned bodyLen);

    uint16_t getSourceCallId() const;
    uint16_t getDestCallId() const;
    uint8_t getOSeqNo() const { return _buf[8]; }
    void setOSeqNo(uint8_t s) {_buf[8] = s; }
    uint8_t getISeqNo() const { return _buf[9]; }
    void setISeqNo(uint8_t s) {_buf[9] = s; }
    bool isRetransmit() const { return (_buf[2] & 0b10000000) != 0; }
    void setRetransmit() { _buf[2] |= 0b10000000; }
    bool isTypeClass(uint8_t t, uint8_t c) const { return _buf[10] == t && _buf[11] == c; }
    bool isACK() const { return _buf[10] == 6 && _buf[11] == 4; }
    bool isNEW() const { return _buf[10] == 6 && _buf[11] == 1; }
    bool isACCEPT() const { return _buf[10] == 6 && _buf[11] == 7; }
    bool isVOICE() const { return _buf[10] == 2; }
    uint8_t getType() const { return _buf[10]; }
    uint8_t getSubclass() const { return _buf[11]; }
    uint32_t getTimeStamp() const { return unpack_uint32_be(_buf + 4); }
    void setTimeStamp(uint32_t ts) { pack_uint32_be(ts, _buf + 4); }

    bool isACKRequired() const;
    bool shouldIncrementSequence() const;

    /**
     * Experimental - Tracking the message types that we shouldn't ACK
     */
    bool isNoACKRequired() const;

    /**
     * Gets the value of the information element 
     * @param id 
     * @returns true if found
     */
    bool getIE_uint16(uint8_t id, uint16_t* result) const;

    /**
     * Gets the value of the information element 
     * @param id 
     * @returns true if found
     */
    bool getIE_uint32(uint8_t id, uint32_t* result) const;

    /**
     * Extracts the raw bytes of an IE. The result is not null
     * terminated.
     * @returns -1 if not found, otherwise the number of bytes
     * available.
     */
    int getIE_raw(uint8_t id, uint8_t* buf, unsigned bufCapacity) const;

    /**
     * Gets the value of the information element in 
     * string format.  Null-terminates the result.
     * 
     * @param id 
     * @returns true if found and if space is sufficient.
     */
    bool getIE_str(uint8_t id, char* buf, unsigned bufMaxLen) const;

    /**
     * Adds an information element
     */
    void addIE_uint32(uint8_t id, uint32_t value);

    /**
     * Adds an information element
     */
    void addIE_uint16(uint8_t id, uint16_t value);

    /**
     * Adds an information element
     */
    void addIE_uint8(uint8_t id, uint8_t value);

    /**
     * Adds an information element
     */
    void addIE_str(uint8_t id, const char* value, unsigned valueLen);

    /**
     * Adds an information element
     * @param value A null-terminated string.
     */
    void addIE_str(uint8_t id, const char* value);

    void addIE_str(uint8_t id, const fixedstring& value);

    /**
     * Adds an IE in raw form.
     */
    void addIE_raw(uint8_t id, const uint8_t* buf, unsigned bufLen);

private:

    unsigned _spaceLeft() const;

    // Always have at least the header
    unsigned _bufLen = 12;
    uint8_t _buf[MAX_BUF_LEN];
};

/**
 * @returns Number of bytes extracted and written to
 * buf, or -1 if the id is not found.
 */
int extractIE(const uint8_t* packet, unsigned packetLen, 
    uint8_t id, uint8_t* buf, unsigned bufMaxLen);

}
