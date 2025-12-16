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

// NOTE: This may be the real ARM library or a mock, depending on the
// platfom that we are building for.
#include <arm_math.h>

#include "Transcoder.h"

namespace kc1fsz {

class Transcoder_SLIN : public Transcoder {
public:

    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_PERIOD_MS = 20;

    virtual bool decode(const uint8_t* source, unsigned sourceLen, 
        int16_t* dest, unsigned destLen);
    virtual bool encode(const int16_t* source, unsigned sourceLen, 
        uint8_t* dest, unsigned destLen);
};

}
