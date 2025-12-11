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
#include "kc1fsz-tools/Common.h"

#include "Transcoder_SLIN_48K.h"

namespace kc1fsz {

bool Transcoder_SLIN_48K::decode(const uint8_t* source, unsigned sourceLen, 
    int16_t* dest, unsigned destLen) {            
    if (sourceLen != BLOCK_SIZE_48K * 2)
        return false;
    if (destLen != BLOCK_SIZE_48K)
        return false;
    const uint8_t* p = source;
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
        dest[i] = unpack_int16_le(p);
        p += 2;
    }
    return true;
}

bool Transcoder_SLIN_48K::decodeGap(int16_t* dest, unsigned destLen) {
    if (destLen != BLOCK_SIZE_48K)
        return false;
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++)
        dest[i] = 0;
    return true;
}

bool Transcoder_SLIN_48K::encode(const int16_t* source, unsigned sourceLen, 
    uint8_t* dest, unsigned destLen) {
    if (sourceLen != BLOCK_SIZE_48K)
        return false;
    if (destLen != BLOCK_SIZE_48K * 2)
        return false;
    uint8_t* p = dest;
    for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
        pack_int16_le(source[i], p);
        p += 2;
    }
    return true;
}

}
