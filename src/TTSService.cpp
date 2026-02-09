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
#include "kc1fsz-tools/threadsafequeue2.h"

#include "Message.h"
#include "ThreadUtil.h"
#include "Transcoder_SLIN_48K.h"
#include "amp/Resampler.h"

using namespace std;

namespace kc1fsz {

    namespace amp {

static const unsigned AUDIO_RATE = 48000;
static const unsigned BLOCK_SIZE_8K = 160;
static const unsigned BLOCK_SIZE_16K = 160 * 2;
static const unsigned BLOCK_SIZE_48K = 160 * 6;
static const unsigned BLOCK_PERIOD_MS = 20;

/**
 * Converts one block of 16K PCM to an audio Message (TTS_AUDIO) that is encoded in 48K LE.
 */
static MessageCarrier makeTTSAudioMsg(const Message& req, 
    const int16_t* pcm16, unsigned pcm16Len, amp::Resampler& ttsResampler) {

    assert(pcm16Len == BLOCK_SIZE_16K);

    // Up-convert to 48K
    int16_t pcm48[BLOCK_SIZE_48K];
    // NOTE: We are using a resampler that exists at a higher level to ensure
    // smooth transitions between consecutive frames.
    ttsResampler.resample(pcm16, BLOCK_SIZE_16K, pcm48, BLOCK_SIZE_48K);

    Transcoder_SLIN_48K trans;
    uint8_t buf[BLOCK_SIZE_48K * sizeof(int16_t)];
    trans.encode(pcm48, BLOCK_SIZE_48K, buf, BLOCK_SIZE_48K * sizeof(int16_t));

    MessageCarrier res(Message::Type::TTS_AUDIO, 0, BLOCK_SIZE_48K * sizeof(int16_t), buf, 0, 0);
    res.setSource(req.getDestBusId(), req.getDestCallId());
    res.setDest(req.getSourceBusId(), req.getSourceCallId());
    return res;
}

// ------ Text To Speach Thread ----------------------------------------------

void ttsLoop(Log* loga, threadsafequeue2<MessageCarrier>* ttsQueueReq,
    threadsafequeue2<MessageCarrier>* ttsQueueRes, std::atomic<bool>* runFlag) {

    Log& log = *loga;

    setThreadName("TTS");
    lowerThreadPriority();

    log.info("Start TTS thread");

    // #### TODO: ADD PRIORITY LOWER CODE

    char path0[128];
    snprintf(path0, 128, "%s/en_US-amy-low.onnx", getenv("AMP_PIPER_DIR"));
    char path1[128];
    snprintf(path1, 128, "%s/en_US-amy-low.onnx.json", getenv("AMP_PIPER_DIR"));
    char path2[128];
    snprintf(path2, 128, "%s/espeak-ng-data", getenv("AMP_PIPER_DIR"));

    piper_synthesizer *synth = piper_create(path0, path1, path2);
    if (synth == 0) {
        log.error("Failed to initialize piper TTS");
        return;
    }

    // Change options here:
    // options.length_scale = 2;
    // options.speaker_id = 5;
    piper_synthesize_options options = piper_default_synthesize_options(synth);

    // Used for converting TTS audio to 48k
    amp::Resampler ttsResampler;
    ttsResampler.setRates(16000, 48000);

    // Processing loop
    while (runFlag->load()) {

        // Attempt to take a TTS request off the request queue. Use a long
        // timeout to avoid high CPU
        MessageCarrier req;
        if (ttsQueueReq->try_pop(req, 500)) {
            if (req.getType() == Message::Type::TTS_REQ) {

                // Make a null-terminated buffer with a maximum size
                char ttsReq[128];
                int size = min(req.size(), (unsigned)127);
                memcpy(ttsReq, req.body(), size);
                ttsReq[size] = 0;

                log.info("TTS request: \"%s\"", ttsReq);

                // There is state here so reset at the start of new speech
                ttsResampler.reset();

                // Here is where the actual speech synthesis happens
                piper_synthesize_start(synth, ttsReq, &options);

                // Take the results of the synthesis (floats) and put it into 16K LE frames.
                int16_t pcm16[BLOCK_SIZE_16K];
                unsigned pcm16Ptr = 0;
                piper_audio_chunk chunk;

                while (piper_synthesize_next(synth, &chunk) != PIPER_DONE) {
                    for (unsigned i = 0; i < chunk.num_samples; i++) {
                        // Samples are in native float32 format
                        pcm16[pcm16Ptr++] = 32767.0f * chunk.samples[i];
                        // Filled a whole frame?
                        if (pcm16Ptr == BLOCK_SIZE_16K) {
                            ttsQueueRes->push(makeTTSAudioMsg(req, pcm16, BLOCK_SIZE_16K, ttsResampler));
                            pcm16Ptr = 0;
                        }
                    }
                }

                // Complete the the last block
                if (pcm16Ptr > 0) {
                    for (unsigned i = pcm16Ptr; i < BLOCK_SIZE_16K; i++) 
                        pcm16[i] = 0;
                    ttsQueueRes->push(makeTTSAudioMsg(req, pcm16, BLOCK_SIZE_16K, ttsResampler));
                }

                // Send a TTS_END signal so the call will know that this TTS process is finished.
                MessageEmpty res(Message::Type::TTS_END, 0, 0, 0);
                res.setSource(req.getDestBusId(), req.getDestCallId());
                res.setDest(req.getSourceBusId(), req.getSourceCallId());
                ttsQueueRes->push(res);

                log.info("TTS complete");
            }
        }
    }

    piper_free(synth);

    log.info("End TTS thread");
}

    }
}