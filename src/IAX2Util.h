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

namespace kc1fsz {

/**
 * Creates a date/time following the standard in RFC 5456 
 * Section 8.6.28.
 * 
 * See: https://datatracker.ietf.org/doc/html/rfc5456#section-8.6.28
 */
uint32_t makeIAX2Time();

/**
 * Helpful for debug.
 */
const char* iax2TypeDesc(uint8_t type, uint8_t subclass);

enum FrameType {
    IAX2_TYPE_DTMF = 0x01,
    IAX2_TYPE_VOICE = 0x02,
    IAX2_TYPE_CONTROL = 0x04,
    IAX2_TYPE_IAX = 0x06,
    IAX2_TYPE_TEXT = 0x07,
    // NOT IN RFC
    IAX2_TYPE_DTMF2 = 0x0c
};

enum IAXSubclass {
    IAX2_SUBCLASS_IAX_NEW = 0x01,
    IAX2_SUBCLASS_IAX_PING = 0x02,
    IAX2_SUBCLASS_IAX_PONG = 0x03,
    IAX2_SUBCLASS_IAX_ACK = 0x04,
    IAX2_SUBCLASS_IAX_HANGUP = 0x05,
    IAX2_SUBCLASS_IAX_REJECT = 0x06,
    IAX2_SUBCLASS_IAX_ACCEPT = 0x07,
    IAX2_SUBCLASS_IAX_INVAL = 0x0a,
    IAX2_SUBCLASS_IAX_LAGRQ = 0x0b,
    IAX2_SUBCLASS_IAX_LAGRP = 0x0c,
    IAX2_SUBCLASS_IAX_VNAK = 0x12,
    IAX2_SUBCLASS_IAX_POKE = 0x1e,
    // NOT IN RFC
    IAX2_SUBCLASS_IAX_CALLTOKEN = 40
};

enum ControlSubclass {
    IAX2_SUBCLASS_CONTROL_UNKEY = 0x0d
};

// See: https://datatracker.ietf.org/doc/html/rfc5457#page-19
enum CODECType {
    IAX2_CODEC_UNKNOWN   = 0,
    IAX2_CODEC_G711_ULAW = 0x00000004,
    // (8k 16-bit SLIN, little endian)
    IAX2_CODEC_SLIN      = 0x00000040,
    // (16k 16-bit SLIN, little endian)
    IAX2_CODEC_SLIN_16K  = 0x00008000,
    // (48k 16-bit SLIN, little endian)
    // NOT OFFICIAL!
    IAX2_CODEC_SLIN_48K  = 0x20000000
};

bool codecSupported(CODECType type);
unsigned maxVoiceFrameSize(CODECType type);
unsigned codecSampleRate(CODECType type);
unsigned codecBlockSize(CODECType type);

enum IEType {
    IAX2_IE_FORMAT = 0x09
};

/**
 * A strange kind of comparison that takes into account wrapping,
 * assuming that the two values are within 128 of each other.
 * For example, 0xfd < 0x04 because the RHS value is assumed to 
 * have just wrapped around.
 */
int compareSeqWrap(uint8_t a, uint8_t b);

}
