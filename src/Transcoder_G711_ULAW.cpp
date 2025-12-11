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

#include <kc1fsz-tools/Log.h>
#include <itu-g711-codec/codec.h>

#include "Transcoder_G711_ULAW.h"

using namespace std;

namespace kc1fsz {

Transcoder_G711_ULAW::Transcoder_G711_ULAW() {
    _plc.setSampleRate(8000);
}

bool Transcoder_G711_ULAW::decode(const uint8_t* source, unsigned sourceLen, 
    int16_t* destPCM, unsigned destLen) {
    assert(sourceLen == BLOCK_SIZE_8K);
    assert(destLen == BLOCK_SIZE_8K);
    
    // Convert the G711 uLaw encoding into 16-bit PCM audio
    int16_t pcm8k_1[BLOCK_SIZE_8K];
    for (unsigned i = 0; i < BLOCK_SIZE_8K; i++)
        pcm8k_1[i] = decode_ulaw(source[i]);

    // Pass audio through the PLC mechanism
    _plc.goodFrame(pcm8k_1, destPCM, BLOCK_SIZE_8K / 2);
    _plc.goodFrame(pcm8k_1 + BLOCK_SIZE_8K / 2, destPCM + BLOCK_SIZE_8K / 2, 
        BLOCK_SIZE_8K / 2);

    return true;
}

bool Transcoder_G711_ULAW::decodeGap(int16_t* destPCM, unsigned destLen) {

    assert(destLen == BLOCK_SIZE_8K);

    // Ask PLC to fill in the missing frame (in two 10ms sections).  
    _plc.badFrame(destPCM, BLOCK_SIZE_8K / 2);
    _plc.badFrame(destPCM + BLOCK_SIZE_8K / 2, BLOCK_SIZE_8K / 2);
    return true;
}

bool Transcoder_G711_ULAW::encode(const int16_t* sourcePCM, unsigned sourceLen, 
    uint8_t* g711Buffer, unsigned destLen) {

    assert(sourceLen == BLOCK_SIZE_8K);
    assert(destLen == BLOCK_SIZE_8K);

    // Make an 8k G711 buffer using the CODEC
    for (unsigned i = 0; i < BLOCK_SIZE_8K; i++)
        g711Buffer[i] = encode_ulaw(sourcePCM[i]);
    return true;
}

}
