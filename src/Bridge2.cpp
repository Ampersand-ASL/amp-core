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

        // Attemp to take a TTS request off the queue
        Message req;
        if (_ttsQueueReq.try_pop(req)) {
            if (req.getType() == Message::Type::TTS_REQ) {

                cout << "TTS REQ: " << (const char*)req.body() << endl;

                /*
                piper_synthesize_start(synth, "Parrot connected, CODEC is 16k linear. Ready to record!    Peak -12dB, average -35dB. Test1.",
                                    &options);

                unsigned pcmCapacity = 160 * 2;
                int16_t pcm[160 * 2];
                unsigned pcmPtr = 0;

                piper_audio_chunk chunk;
                while (piper_synthesize_next(synth, &chunk) != PIPER_DONE) {
                    // Convert to 16-bin PCM
                    cout << chunk.num_samples << endl;
                    //audio_stream.write(reinterpret_cast<const char *>(chunk.samples),
                    //                   chunk.num_samples * sizeof(float));
                }
                */

                Message res(Message::Type::TTS_END, 0, 0, 0, 0, 0);
                res.setDest(req.getSourceBusId(), req.getSourceCallId());
                _ttsQueueRes.push(req);
            }
        }
    }

    piper_free(synth);

    _log.info("TTS thread end");
}

    }
}