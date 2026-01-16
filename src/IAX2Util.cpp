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
#include <ctime>
#include <cstdint>

#include "IAX2Util.h"

namespace kc1fsz {

uint32_t makeIAX2Time() {
    /*
        The data
        field of a DATETIME information element is four octets long and
        stores the time as follows: the 5 least significant bits are seconds,
        the next 6 least significant bits are minutes, the next least
        significant 5 bits are hours, the next least significant 5 bits are
        the day of the month, the next least significant 4 bits are the
        month, and the most significant 7 bits are the year.  The year is
        offset from 2000, and the month is a 1-based index (i.e., January ==
        1, February == 2, etc.).  The timezone of the clock MUST be UTC to
        avoid confusion between the peers.    
    */
    time_t now_c_style = time(0);
    tm* t = gmtime(&now_c_style);
    // Extract individual components.  
    uint32_t result = 0;
    // Please see: https://cplusplus.com/reference/ctime/tm    
    result |= (t->tm_sec  &       0b11111);
    result |= (t->tm_min  &       0b111111) << 5;
    result |= (t->tm_hour &       0b11111) << (5 + 6);
    result |= (t->tm_mday &       0b11111) << (5 + 6 + 5);
    // tm month is 0-based and IAX2 month is 1-based
    result |= ((t->tm_mon + 1) &  0b1111) << (5 + 6 + 5 + 5);
    // tm year is offset from 1900 and IAX2 year is offset from 2000
    result |= ((t->tm_year - 100) & 0b1111111) << (5 + 6 + 5 + 5 + 4);
    return result;
}

const char* iax2TypeDesc(uint8_t type, uint8_t subclass) {
    if (type == 2) {
        if (subclass == 4) 
            return "VOICE";
    } else if (type == 4) {
        if (subclass == 4) 
            return "ANSWER";
        else if (subclass == 12) 
            return "KEY_RADIO";
        else if (subclass == 13) 
            return "UNKEY_RADIO";
        // NOT IN RFC
        else if (subclass == 255) 
            return "STOP_SOUNDS";
    } else if (type == 6) {
        if (subclass == 1) 
            return "NEW";
        else if (subclass == 2) 
            return "PING";
        else if (subclass == 3) 
            return "PONG";
        else if (subclass == 4) 
            return "ACK";
        else if (subclass == 5) 
            return "HANGUP";
        else if (subclass == 6) 
            return "REJECT";
        else if (subclass == 7) 
            return "ACCEPT";
        else if (subclass == 8) 
            return "AUTHREQ";
        else if (subclass == 9) 
            return "AUTHREP";
        else if (subclass == 0x0a) 
            return "INVAL";
        else if (subclass == 11) 
            return "LAGRQ";
        else if (subclass == 12) 
            return "LAGRP";
        else if (subclass == 0x12) 
            return "VNAK";
        else if (subclass == IAX2_SUBCLASS_IAX_POKE) 
            return "POKE";
        // NOT IN THE RFC
        else if (subclass == IAX2_SUBCLASS_IAX_CALLTOKEN) 
            return "CALLTOKEN";
    } else if (type == 7) {
        if (subclass == 0) 
            return "TEXT";
    } else if (type == 12) {
        return "DTMFPRESS";
    } else if (type == 1) {
        return "DTMF";
    } else if (type == 10) {
        return "COMFORT";
    }
    return "(UNKNOWN)";
}

int compareSeqWrap(uint8_t a, uint8_t b) {
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

bool codecSupported(CODECType type) {
    if (type == CODECType::IAX2_CODEC_G711_ULAW)
        return true;
    else if (type == CODECType::IAX2_CODEC_SLIN_8K)
        return true;
    else if (type == CODECType::IAX2_CODEC_SLIN_16K)
        return true;
    else 
        return false;
}

unsigned maxVoiceFrameSize(CODECType type) {
    if (type == CODECType::IAX2_CODEC_G711_ULAW)
        return 160;
    else if (type == CODECType::IAX2_CODEC_SLIN_8K)
        return 160 * 2;
    else if (type == CODECType::IAX2_CODEC_SLIN_16K)
        return 160 * 2 * 2;
    else 
        return 0;
}

unsigned codecSampleRate(CODECType type) {
    if (type == CODECType::IAX2_CODEC_G711_ULAW)
        return 8000;
    else if (type == CODECType::IAX2_CODEC_SLIN_8K)
        return 8000;
    else if (type == CODECType::IAX2_CODEC_SLIN_16K)
        return 16000;
    else if (type == CODECType::IAX2_CODEC_SLIN_48K)
        return 48000;
    else 
        return 0;
}

unsigned codecBlockSize(CODECType type) {
    return codecSampleRate(type) / 50;
}

}
