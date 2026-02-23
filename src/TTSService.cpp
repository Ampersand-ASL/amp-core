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
#include <cmath>

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

    MessageCarrier res(Message::Type::TTS_AUDIO, 0, BLOCK_SIZE_48K * sizeof(int16_t), buf, 
        0, 0);
    res.setSource(req.getDestBusId(), req.getDestCallId());
    res.setDest(req.getSourceBusId(), req.getSourceCallId());
    return res;
}

int loadComfortNoise(const Message& req, unsigned ms,
    threadsafequeue2<MessageCarrier>* ttsQueueRes) {    

    float omega = 10.0f * 2.0f * 3.1415926f / 48000.0f;
    float phi = 0;
    float amp = 0.01;
    Transcoder_SLIN_48K trans;

    for (unsigned i = 0; i < ms / 20; i++) {

        int16_t pcm48k[BLOCK_SIZE_48K];

        // For now we are using a low tone
        for (unsigned j = 0; j < BLOCK_SIZE_48K; j++) {
            pcm48k[j] = amp * 32767.0f * std::cos(phi);
            phi += omega;
        }

        // Transcode to SLIN 48K
        // #### TODO: CHANGE TO PCM 48K
        uint8_t slin48k[BLOCK_SIZE_48K * sizeof(int16_t)];
        trans.encode(pcm48k, BLOCK_SIZE_48K, 
            slin48k, BLOCK_SIZE_48K * sizeof(int16_t));
        // Queue a message
        MessageCarrier res(Message::Type::TTS_AUDIO, 0, 
            BLOCK_SIZE_48K * sizeof(int16_t), slin48k, 
            0, 0);
        res.setSource(req.getDestBusId(), req.getDestCallId());
        res.setDest(req.getSourceBusId(), req.getSourceCallId());
        ttsQueueRes->push(res);
    }

    return 0;
}

int loadAudioFile(const Message& req, const char* fullPath, 
    threadsafequeue2<MessageCarrier>* ttsQueueRes) {    
    
    ifstream aud(fullPath, std::ios::binary);
    if (!aud.is_open()) {
        return -1;
    }

    int16_t pcm8k[BLOCK_SIZE_8K];
    unsigned pcmPtr = 0;
    // Stero 16-bit
    char buffer[4];
    amp::Resampler resampler;
    resampler.setRates(8000, 48000);
    Transcoder_SLIN_48K trans;

    while (aud.read(buffer, 4)) {
        // Only use one of the stero channels
        pcm8k[pcmPtr++] = unpack_int16_le((const uint8_t*)buffer);
        if (pcmPtr == BLOCK_SIZE_8K) {
            // Convert 8K to 48K
            int16_t pcm48k[BLOCK_SIZE_48K];
            resampler.resample(pcm8k, BLOCK_SIZE_8K, pcm48k, BLOCK_SIZE_48K);
            // Transcode to SLIN 48K
            // #### TODO: CHANGE TO PCM 48K
            uint8_t slin48k[BLOCK_SIZE_48K * sizeof(int16_t)];
            trans.encode(pcm48k, BLOCK_SIZE_48K, 
                slin48k, BLOCK_SIZE_48K * sizeof(int16_t));
            // Queue a message
            MessageCarrier res(Message::Type::TTS_AUDIO, 0, 
                BLOCK_SIZE_48K * sizeof(int16_t), slin48k, 
                0, 0);
            res.setSource(req.getDestBusId(), req.getDestCallId());
            res.setDest(req.getSourceBusId(), req.getSourceCallId());
            ttsQueueRes->push(res);
            pcmPtr = 0;
        }
    }

    // Clean up last frame
    if (pcmPtr < BLOCK_SIZE_8K) {
        // Fill with silence
        for (unsigned i = 0; i < BLOCK_SIZE_8K - pcmPtr; i++)
            pcm8k[pcmPtr++] = 0;
        int16_t pcm48k[BLOCK_SIZE_48K];
        resampler.resample(pcm8k, BLOCK_SIZE_8K, pcm48k, BLOCK_SIZE_48K);

        // Transcode to SLIN 48K
        // #### TODO: CHANGE TO PCM 48K
        uint8_t slin48k[BLOCK_SIZE_48K * sizeof(int16_t)];
        trans.encode(pcm48k, BLOCK_SIZE_48K, 
            slin48k, BLOCK_SIZE_48K * sizeof(int16_t));
        // Queue a message
        MessageCarrier res(Message::Type::TTS_AUDIO, 0, 
            BLOCK_SIZE_48K * sizeof(int16_t), slin48k, 
            0, 0);
        res.setSource(req.getDestBusId(), req.getDestCallId());
        res.setDest(req.getSourceBusId(), req.getSourceCallId());
        ttsQueueRes->push(res);

        pcmPtr = 0;
    }

    return 0;
}

// ------ Text To Speech Thread ----------------------------------------------

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
            else if (req.getType() == Message::Type::TTS_FILE_REQ) {

                // Make a null-terminated buffer with a maximum size
                char ttsReq[128];
                int size = min(req.size(), (unsigned)127);
                memcpy(ttsReq, req.body(), size);
                ttsReq[size] = 0;

                log.info("TTS file request: %s", ttsReq);

                // Some pre-noise to allow everyone to key up
                loadComfortNoise(req, 1000, ttsQueueRes);
                // The actual file
                loadAudioFile(req, ttsReq, ttsQueueRes);

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