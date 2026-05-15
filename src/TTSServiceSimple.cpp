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
#include <cstring>

#include "kc1fsz-tools/Log.h"

#include "Message.h"
#include "TTSServiceSimple.h"

#include "amp/Ampersand.h"
#include "amp/Resampler.h"

#define BLOCK_SIZE_48K (960)

using namespace std;

extern unsigned char _amp_core_clips_clip_calling_node_raw[];
extern unsigned int _amp_core_clips_clip_calling_node_raw_len;

namespace kc1fsz {

TTSServiceSimple::TTSServiceSimple(Log& log, Clock& clock, MessageConsumer& bus, 
    unsigned lineId, unsigned destLineId)
:   _log(log),
    _clock(clock),
    _bus(bus),
    _lineId(lineId),
    _destLineId(destLineId) {
}

void TTSServiceSimple::consume(const Message& msg) {  
    if (msg.getType() == Message::Type::TTS_REQ) {

        _log.info("TTSimpleService request");

        PayloadTTS payload;
        assert(msg.size() == sizeof(payload));
        memcpy(&payload, msg.body(), msg.size());
        _log.infoDump("Payload", &payload, sizeof(payload));

        // Stream back some audio
        unsigned blocks = (_amp_core_clips_clip_calling_node_raw_len / 2) / 160;
        const int16_t* audio = (const int16_t*)_amp_core_clips_clip_calling_node_raw;
        unsigned ptr = 0;

        amp::Resampler resampler;
        resampler.setRates(8000, 48000);

        for (unsigned i = 0; i < blocks; i++) {

            int16_t pcm48[BLOCK_SIZE_48K];
            // Pull an 8K block
            int16_t pcm8[BLOCK_SIZE_8K] = { 0 };
            for (unsigned k = 0; k < BLOCK_SIZE_8K; k++)
                if (ptr + k < _amp_core_clips_clip_calling_node_raw_len)
                    pcm8[k] = audio[ptr + k];
            // Convert to 48K
            resampler.resample(pcm8, BLOCK_SIZE_8K, pcm48, BLOCK_SIZE_48K);

            MessageCarrier res(Message::Type::TTS_AUDIO, CODECType::IAX2_CODEC_PCM_48K, 
                BLOCK_SIZE_48K * sizeof(int16_t), (const uint8_t*)pcm48, 0, 0);
            res.setSource(msg.getDestBusId(), msg.getDestCallId());
            res.setDest(msg.getSourceBusId(), msg.getSourceCallId());
            _bus.consume(res);

            ptr += 160;
        }

        MessageCarrier res(Message::Type::TTS_END, 0, 0, 0, 0, 0);
        res.setSource(msg.getDestBusId(), msg.getDestCallId());
        res.setDest(msg.getSourceBusId(), msg.getSourceCallId());
        _bus.consume(res);
    }
}

void TTSServiceSimple::audioRateTick(uint32_t tickMs) {   
}

}

