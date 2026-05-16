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
#include <functional>

#include "kc1fsz-tools/Log.h"

#include "amp/Resampler.h"
#include "Message.h"
#include "TTSServiceSimple.h"

#include "amp/Ampersand.h"
#include "amp/Resampler.h"

#define BLOCK_SIZE_48K (960)

using namespace std;

extern unsigned char _amp_core_clips_clip_0_raw[];
extern unsigned int  _amp_core_clips_clip_0_raw_len;
extern unsigned char _amp_core_clips_clip_0_end_raw[];
extern unsigned int  _amp_core_clips_clip_0_end_raw_len;

extern unsigned char _amp_core_clips_clip_1_raw[];
extern unsigned int  _amp_core_clips_clip_1_raw_len;
extern unsigned char _amp_core_clips_clip_1_end_raw[];
extern unsigned int  _amp_core_clips_clip_1_end_raw_len;

extern unsigned char _amp_core_clips_clip_2_raw[];
extern unsigned int  _amp_core_clips_clip_2_raw_len;
extern unsigned char _amp_core_clips_clip_2_end_raw[];
extern unsigned int  _amp_core_clips_clip_2_end_raw_len;

extern unsigned char _amp_core_clips_clip_3_raw[];
extern unsigned int  _amp_core_clips_clip_3_raw_len;
extern unsigned char _amp_core_clips_clip_3_end_raw[];
extern unsigned int  _amp_core_clips_clip_3_end_raw_len;

extern unsigned char _amp_core_clips_clip_4_raw[];
extern unsigned int  _amp_core_clips_clip_4_raw_len;
extern unsigned char _amp_core_clips_clip_4_end_raw[];
extern unsigned int  _amp_core_clips_clip_4_end_raw_len;

extern unsigned char _amp_core_clips_clip_5_raw[];
extern unsigned int  _amp_core_clips_clip_5_raw_len;
extern unsigned char _amp_core_clips_clip_5_end_raw[];
extern unsigned int  _amp_core_clips_clip_5_end_raw_len;

extern unsigned char _amp_core_clips_clip_6_raw[];
extern unsigned int  _amp_core_clips_clip_6_raw_len;
extern unsigned char _amp_core_clips_clip_6_end_raw[];
extern unsigned int  _amp_core_clips_clip_6_end_raw_len;

extern unsigned char _amp_core_clips_clip_7_raw[];
extern unsigned int  _amp_core_clips_clip_7_raw_len;
extern unsigned char _amp_core_clips_clip_7_end_raw[];
extern unsigned int  _amp_core_clips_clip_7_end_raw_len;

extern unsigned char _amp_core_clips_clip_8_raw[];
extern unsigned int  _amp_core_clips_clip_8_raw_len;
extern unsigned char _amp_core_clips_clip_8_end_raw[];
extern unsigned int  _amp_core_clips_clip_8_end_raw_len;

extern unsigned char _amp_core_clips_clip_9_raw[];
extern unsigned int  _amp_core_clips_clip_9_raw_len;
extern unsigned char _amp_core_clips_clip_9_end_raw[];
extern unsigned int  _amp_core_clips_clip_9_end_raw_len;

extern unsigned char _amp_core_clips_clip_calling_node_raw[];
extern unsigned int  _amp_core_clips_clip_calling_node_raw_len;

struct Clip {
    unsigned char* data;
    unsigned int len;
};

static Clip Clips[] = {

    { .data = _amp_core_clips_clip_0_raw, .len = _amp_core_clips_clip_0_raw_len },
    { .data = _amp_core_clips_clip_1_raw, .len = _amp_core_clips_clip_1_raw_len },
    { .data = _amp_core_clips_clip_2_raw, .len = _amp_core_clips_clip_2_raw_len },
    { .data = _amp_core_clips_clip_3_raw, .len = _amp_core_clips_clip_3_raw_len },
    { .data = _amp_core_clips_clip_4_raw, .len = _amp_core_clips_clip_4_raw_len },
    { .data = _amp_core_clips_clip_5_raw, .len = _amp_core_clips_clip_5_raw_len },
    { .data = _amp_core_clips_clip_6_raw, .len = _amp_core_clips_clip_6_raw_len },
    { .data = _amp_core_clips_clip_7_raw, .len = _amp_core_clips_clip_7_raw_len },
    { .data = _amp_core_clips_clip_8_raw, .len = _amp_core_clips_clip_8_raw_len },
    { .data = _amp_core_clips_clip_9_raw, .len = _amp_core_clips_clip_9_raw_len },

    { .data = _amp_core_clips_clip_0_end_raw, .len = _amp_core_clips_clip_0_end_raw_len },
    { .data = _amp_core_clips_clip_1_end_raw, .len = _amp_core_clips_clip_1_end_raw_len },
    { .data = _amp_core_clips_clip_2_end_raw, .len = _amp_core_clips_clip_2_end_raw_len },
    { .data = _amp_core_clips_clip_3_end_raw, .len = _amp_core_clips_clip_3_end_raw_len },
    { .data = _amp_core_clips_clip_4_end_raw, .len = _amp_core_clips_clip_4_end_raw_len },
    { .data = _amp_core_clips_clip_5_end_raw, .len = _amp_core_clips_clip_5_end_raw_len },
    { .data = _amp_core_clips_clip_6_end_raw, .len = _amp_core_clips_clip_6_end_raw_len },
    { .data = _amp_core_clips_clip_7_end_raw, .len = _amp_core_clips_clip_7_end_raw_len },
    { .data = _amp_core_clips_clip_8_end_raw, .len = _amp_core_clips_clip_8_end_raw_len },
    { .data = _amp_core_clips_clip_9_end_raw, .len = _amp_core_clips_clip_9_end_raw_len },

    { .data = _amp_core_clips_clip_calling_node_raw, .len = _amp_core_clips_clip_calling_node_raw_len }
};

struct Step {
    unsigned clipIx;
    unsigned preSilence;
    unsigned postSilence;
};

namespace kc1fsz {

// Takes a series of steps and emits 48K audio frames
void makeStatement8k(Step* steps, unsigned stepCount, 
    std::function<void(const int16_t* pcm8, unsigned len)> cb) {

    int16_t pcm8[BLOCK_SIZE_8K];
    // Pointer in pcm8 block
    unsigned p = 0;
    
    for (unsigned s = 0; s < stepCount; s++) {

        // The PCM for this step
        const int16_t* stepAudio = (const int16_t*)Clips[steps[s].clipIx].data;
        // Length in 16-bit samples
        const unsigned stepLength = Clips[steps[s].clipIx].len / 2;

        // Add pre-silence
        for (unsigned i = 0; i < (steps[s].preSilence * 8000) / 1000; i++) {
            pcm8[p++] = 0;
            if (p == BLOCK_SIZE_8K) {
                cb(pcm8, BLOCK_SIZE_8K);
                p = 0;
            }
        }

        // Add clip
        for (unsigned i = 0; i < stepLength; i++) {
            pcm8[p++] = stepAudio[i];
            if (p == BLOCK_SIZE_8K) {
                cb(pcm8, BLOCK_SIZE_8K);
                p = 0;
            }
        }

        // Add post-silence
        for (unsigned i = 0; i < (steps[s].postSilence * 8000) / 1000; i++) {
            pcm8[p++] = 0;
            if (p == BLOCK_SIZE_8K) {
                cb(pcm8, BLOCK_SIZE_8K);
                p = 0;
            }
        }
    }

    // Pad last block
    if (p > 0) {
        while (p < BLOCK_SIZE_8K)
            pcm8[p++] = 0;
        cb(pcm8, BLOCK_SIZE_8K);
    }
}

// Takes a series of steps and emits 48K audio frames
void makeStatement48k(Step* steps, unsigned stepCount, 
    std::function<void(const int16_t* pcm8, unsigned len)> cb) {

    amp::Resampler resampler;
    resampler.setRates(8000, 48000);

    makeStatement8k(steps, stepCount, 
        [&resampler, &cb](const int16_t* pcm8, unsigned len) {
            // Convert to 48K
            int16_t pcm48[BLOCK_SIZE_48K];
            resampler.resample(pcm8, len, pcm48, BLOCK_SIZE_48K);
            cb(pcm48, BLOCK_SIZE_48K);
        }
    );
}

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

        /*
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
    */
    }
}

void TTSServiceSimple::audioRateTick(uint32_t tickMs) {   
}

}

