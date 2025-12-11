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

namespace kc1fsz {

class Transcoder {
public:

    virtual void reset() { };

    /**
     * Converts from the source code to a PCM frame in the "natural"
     * sampling rate of the code. DOES NOT RE-SAMPLE.
     */
    virtual bool decode(const uint8_t* source, unsigned sourceLen, 
        int16_t* destPCM, unsigned destLen) = 0;

    /**
     * Called when the source code is missing a frame. Will generally 
     * invoke the relevant PLC function. DOES NOT RE-SAMPLE.
     */
    virtual bool decodeGap(int16_t* destPCM, unsigned destLen) = 0;

    /**
     * Converts from PCM in the "natural" rate of the code to the target 
     * code. DOES NOT RE-SAMPLE.
     */
    virtual bool encode(const int16_t* sourcePCM, unsigned sourceLen, 
        uint8_t* dest, unsigned destLen) = 0;
};

}
