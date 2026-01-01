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
#include <iostream>
#include <cstring> 
#include <fstream>
#include <thread>
#include <chrono>

#include <piper.h>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "Bridge.h"
#include "Message.h"

using namespace std;

namespace kc1fsz {
    namespace amp {

// ------ Text To Speach Thread ----------------------------------------------

void Bridge::_tts() {

    pthread_setname_np(pthread_self(), "TTS  ");

    _log.info("TTS thread start");

    piper_synthesizer *synth = piper_create("en_US-amy-low.onnx",
                                            "en_US-amy-low.onnx.json",
                                            "libpiper-aarch64/espeak-ng-data");

    // aplay -r 22050 -c 1 -f FLOAT_LE -t raw output.raw
    // aplay -r 16000 -c 1 -f FLOAT_LE -t raw output.raw
    //std::ofstream audio_stream("/tmp/output.raw", std::ios::binary);

    piper_synthesize_options options = piper_default_synthesize_options(synth);
    // Change options here:
    // options.length_scale = 2;
    // options.speaker_id = 5;

    // Processing loop
    while (true) {

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Attemp to take a TTS request off the request queue
        Message req;
        if (_ttsQueueReq.try_pop(req)) {
            if (req.getType() == Message::Type::TTS_REQ) {

                // Make a null-terminated buffer with a maximum size
                char ttsReq[128];
                int size = min(req.size(), (unsigned)127);
                memcpy(ttsReq, req.body(), size);
                ttsReq[size] = 0;

                _log.info("TTS REQ: %s", ttsReq);

                _ttsResampler.reset();

                // Here is where the actual speech sythensis happens
                piper_synthesize_start(synth, ttsReq, &options);

                // Take the results of the sythensis (float) and put it into 16K LE frames.
                int16_t pcm16[BLOCK_SIZE_16K];
                unsigned pcm16Ptr = 0;
                piper_audio_chunk chunk;

                while (piper_synthesize_next(synth, &chunk) != PIPER_DONE) {
                    for (unsigned i = 0; i < chunk.num_samples; i++) {
                        // Samples are in native float32 format
                        pcm16[pcm16Ptr++] = 32767.0f * chunk.samples[i];
                        // Filled a whole frame?
                        if (pcm16Ptr == BLOCK_SIZE_16K) {
                            _ttsQueueRes.push(_makeTTSAudioMsg(req, pcm16, BLOCK_SIZE_16K));
                            pcm16Ptr = 0;
                        }
                    }
                }

                // Complete the the last block
                if (pcm16Ptr > 0) {
                    for (unsigned i = pcm16Ptr; i < BLOCK_SIZE_16K; i++) 
                        pcm16[i] = 0;
                    _ttsQueueRes.push(_makeTTSAudioMsg(req, pcm16, BLOCK_SIZE_16K));
                }

                // Send a TTS_END signal so the call will know that this TTS process is finished.
                Message res(Message::Type::TTS_END, 0, 0, 0, 0, 0);
                res.setSource(req.getSourceBusId(), req.getSourceCallId());
                _ttsQueueRes.push(res);
                _log.info("TTS complete");
            }
        }
    }

    piper_free(synth);

    _log.info("TTS thread end");
}

/**
 * Converts one block of 16K PCM to an audio Message (TTS_AUDIO) that is encoded in 48K LE.
 */
Message Bridge::_makeTTSAudioMsg(const Message& req, const int16_t* pcm16, unsigned pcm16Len) {

    assert(pcm16Len == BLOCK_SIZE_16K);

    // Up-convert to 48K
    int16_t pcm48[BLOCK_SIZE_48K];
    // NOTE: We are using a resampler that exists at the Bridge level to ensure
    // smooth transitions between consecutive frames.
    _ttsResampler.resample(pcm16, BLOCK_SIZE_16K, pcm48, BLOCK_SIZE_48K);

    Transcoder_SLIN_48K trans;
    uint8_t buf[BLOCK_SIZE_48K * sizeof(int16_t)];
    trans.encode(pcm48, BLOCK_SIZE_48K, buf, BLOCK_SIZE_48K * sizeof(int16_t));

    Message res(Message::Type::TTS_AUDIO, 0, BLOCK_SIZE_48K * sizeof(int16_t), buf, 0, 0);
    res.setSource(req.getSourceBusId(), req.getSourceCallId());
    return res;
}

    }
}