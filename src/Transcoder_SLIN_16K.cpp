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
#include <iostream>
#include <cassert>
#include <cstring> 

#include "kc1fsz-tools/Common.h"

#include "Transcoder_SLIN_16K.h"

using namespace std;

namespace kc1fsz {

bool Transcoder_SLIN_16K::decode(const uint8_t* source, unsigned sourceLen, 
    int16_t* destPCM, unsigned destLen) {            
    assert(sourceLen == BLOCK_SIZE_16K * 2);
    assert(destLen == BLOCK_SIZE_16K);

    int16_t pcm_1[BLOCK_SIZE_16K];
    const uint8_t* p = source;
    for (unsigned i = 0; i < BLOCK_SIZE_16K; i++) {
        pcm_1[i] = unpack_int16_le(p);
        p += 2;
    }

    // Pass audio through the PLC mechanism
    //_plc.goodFrame(pcm8k_1, destPCM, BLOCK_SIZE_8K / 2);
    //_plc.goodFrame(pcm8k_1 + BLOCK_SIZE_8K / 2, destPCM + BLOCK_SIZE_8K / 2, 
    //    BLOCK_SIZE_8K / 2);
    memcpy(destPCM, pcm_1, BLOCK_SIZE_16K * 2);

    return true;
}

bool Transcoder_SLIN_16K::decodeGap(int16_t* destPCM, unsigned destLen) {
    assert(destLen == BLOCK_SIZE_16K);
    // Ask PLC to fill in the missing frame (in two 10ms sections).  
    //_plc.badFrame(destPCM, BLOCK_SIZE_8K / 2);
    //_plc.badFrame(destPCM + BLOCK_SIZE_8K / 2, BLOCK_SIZE_8K / 2);
    memset(destPCM, 0, BLOCK_SIZE_16K * 2);
    return true;
}

bool Transcoder_SLIN_16K::encode(const int16_t* source, unsigned sourceLen, 
    uint8_t* dest, unsigned destLen) {
    assert(sourceLen == BLOCK_SIZE_16K);
    assert(destLen == BLOCK_SIZE_16K * 2);
    uint8_t* p = dest;
    for (unsigned i = 0; i < BLOCK_SIZE_16K; i++) {
        pack_int16_le(source[i], p);
        p += 2;
    }
    return true;
}

}
