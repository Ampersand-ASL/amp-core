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

#include "Message.h"

#include "tests/TestAudioGenerator.h"
#include "MessageConsumer.h"

#define BLOCK_SIZE_48K (960)
#define PI2 (2.0f * 3.1415926f)

using namespace std;

namespace kc1fsz {

TestAudioGenerator::TestAudioGenerator(Log& log, Clock& clock, MessageConsumer& bus, unsigned destLineId)
:   _log(log),
    _clock(clock),
    _bus(bus),
    _destLineId(destLineId) {
    _testToneHz = 440;
    _omega = PI2 * _testToneHz / (float)_audioRate;
    _amp = 0.25;
    _phi = 0;
}

void TestAudioGenerator::audioRateTick(uint32_t tickMs) {   
    
    // 2 seconds on, 1 second off

    if (_stateCounter % 150 < 100) {
        // Generate a 48K block of phase-continuous tone
        int16_t pcm48[BLOCK_SIZE_48K];
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
            float sample = _amp * cos(_phi);
            _phi += _omega;
            // The standard audio format that is passed through the Ampersand system 
            // is 16-bit PCM at 48K.
            int16_t pcm16 = 32767.0f * sample;
            pcm48[i] = pcm16;
        }
        _phi = fmod(_phi, PI2);

        MessageCarrier msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_PCM_48K, 
            BLOCK_SIZE_48K * sizeof(int16_t), (const uint8_t*)pcm48, 0, 0);
        msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
        _bus.consume(msg);
    }

    _stateCounter++;
}

}

