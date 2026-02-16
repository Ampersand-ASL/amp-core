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
#include <cmath>
#include <cassert>
#include <algorithm>

#include "kc1fsz-tools/Log.h"

#include "Transcoder_G726.h"

using namespace std;

namespace kc1fsz {

Transcoder_G726::Transcoder_G726() {
    reset();
}

void Transcoder_G726::reset() {
    g726_init_state(&_stateEncode);
    g726_init_state(&_stateDecode);
}

bool Transcoder_G726::decode(const uint8_t* source, unsigned sourceLen, 
    int16_t* destPCM, unsigned destLen) {

    assert(sourceLen == BLOCK_SIZE_8K / 2);
    assert(destLen == BLOCK_SIZE_8K);

    for (unsigned i = 0; i < BLOCK_SIZE_8K / 2; i++) {
        // Big-endian packing
        uint8_t hi = (source[i] >> 4) & 0x0f;
        uint8_t lo = source[i] & 0x0f;
        *destPCM = g726_32_decoder(hi, AUDIO_ENCODING_LINEAR, &_stateDecode);
        destPCM++;
        *destPCM = g726_32_decoder(lo, AUDIO_ENCODING_LINEAR, &_stateDecode);
        destPCM++;
    }
   
    return true;
}

bool Transcoder_G726::encode(const int16_t* sourcePCM, unsigned sourceLen, 
    uint8_t* g711Buffer, unsigned destLen) {

    assert(sourceLen == BLOCK_SIZE_8K);
    assert(destLen == BLOCK_SIZE_8K / 2);

    for (unsigned i = 0; i < BLOCK_SIZE_8K; i += 2) {
        // Big-endian
        uint8_t hi = g726_32_encoder(sourcePCM[i],     AUDIO_ENCODING_LINEAR, &_stateEncode);
        uint8_t lo = g726_32_encoder(sourcePCM[i + 1], AUDIO_ENCODING_LINEAR, &_stateEncode);
        *g711Buffer = (hi << 4) | lo;
        g711Buffer++;
    }

    return true;
}

}
