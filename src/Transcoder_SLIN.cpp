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

#include "kc1fsz-tools/Common.h"

#include "Transcoder_SLIN.h"

using namespace std;

namespace kc1fsz {

bool Transcoder_SLIN::decode(const uint8_t* source, unsigned sourceLen, 
    int16_t* destPCM, unsigned destLen) {            
    assert(sourceLen == BLOCK_SIZE_8K * 2);
    assert(destLen == BLOCK_SIZE_8K);

    int16_t pcm8k_1[BLOCK_SIZE_8K];
    const uint8_t* p = source;
    for (unsigned i = 0; i < BLOCK_SIZE_8K; i++) {
        pcm8k_1[i] = unpack_int16_le(p);
        p += 2;
    }
    return true;
}

bool Transcoder_SLIN::encode(const int16_t* source, unsigned sourceLen, 
    uint8_t* dest, unsigned destLen) {
    assert(sourceLen == BLOCK_SIZE_8K);
    assert(destLen == BLOCK_SIZE_8K * 2);
    uint8_t* p = dest;
    for (unsigned i = 0; i < BLOCK_SIZE_8K; i++) {
        pack_int16_le(source[i], p);
        p += 2;
    }
    return true;
}

}
