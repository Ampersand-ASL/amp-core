/**
 * Copyright (C) 2026, Bruce MacKinnon KC1FSZ
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

class Log;
class Clock;
class MessageConsumer; 
class TraceLog;

    namespace amp {

class VoterUtil {
public:

    /**
     * @returns The 32-bit CRC for given null-terminated string.
     */
    static uint32_t crc32(const char* msg);

    // General header stuff

    static uint16_t getHeaderPayloadType(const uint8_t* packet); 
    static uint32_t getHeaderTimeS(const uint8_t* packet);
    static uint32_t getHeaderTimeNs(const uint8_t* packet);
    static int getHeaderAuthChallenge(const uint8_t* packet,
        char* challenge, unsigned challengeCapacity);
    static uint32_t getHeaderAuthResponse(const uint8_t* packet);

    static uint8_t getType0Flags(const uint8_t* packet);

    static uint8_t getType1RSSI(const uint8_t* packet);
    static int getType1Audio(const uint8_t* packet,
        uint8_t* audio, unsigned audioCapacity);

    static void setHeaderPayloadType(uint8_t* packet, uint16_t t);
    static void setHeaderAuthChallenge(uint8_t* packet, const char* challenge);
    static void setHeaderAuthResponse(uint8_t* packet, uint32_t crc32);
    static void setHeaderTimeS(uint8_t* packet, uint32_t s);
    static void setHeaderTimeNs(uint8_t* packet, uint32_t ns);
};

}
    }
