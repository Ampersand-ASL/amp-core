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

    // General header stuff

    static int getPayloadType(const uint8_t* packet, unsigned packetLen); 
    static uint32_t getHeaderTimeS(const uint8_t* packet, unsigned packetLen);
    static uint32_t getHeaderTimeNs(const uint8_t* packet, unsigned packetLen);
    static int getHeaderAuthChallenge(const uint8_t* packet, unsigned packetLen,
        char* challenge, unsigned challengeCapacity);
    static int getHeaderAuthResponse(const uint8_t* packet, unsigned packetLen,
        char* response, unsigned responseCapacity);

    static uint32_t getType0Flags(const uint8_t* packet, unsigned packetLen);

    static uint8_t getType1RSSI(const uint8_t* packet, unsigned packetLen);
    static int getType1Audio(const uint8_t* packet, unsigned packetLen,
        uint8_t* audio, unsigned audioCapacity);
};

}
    }
