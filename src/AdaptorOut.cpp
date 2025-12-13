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
#include <cassert>
#include <iostream>

#include "kc1fsz-tools/Log.h"

#include "Message.h"
#include "AdaptorOut.h"

using namespace std;

namespace kc1fsz {

void AdaptorOut::setCodec(CODECType codecType) {
    _codecType = codecType;
    unsigned rate = codecSampleRate(_codecType);
    if (rate == 0)
        assert(false);
    _resampler.setRates(48000, rate);
}

void AdaptorOut::consume(const Message& frame) {

    if (frame.getType() == Message::Type::AUDIO) {

        assert(frame.getFormat() == CODECType::IAX2_CODEC_SLIN_48K);
        assert(frame.size() == BLOCK_SIZE_48K * 2);
 
        if (_codecType == CODECType::IAX2_CODEC_G711_ULAW ||
            _codecType == CODECType::IAX2_CODEC_SLIN ||
            _codecType == CODECType::IAX2_CODEC_SLIN_16K) {             
            
            Transcoder* t1 = 0;
            unsigned codeSize = maxVoiceFrameSize(_codecType);
            unsigned blockSize = codecBlockSize(_codecType);
            if (_codecType == CODECType::IAX2_CODEC_G711_ULAW) 
                t1 = &_transcoder1a;
            else if (_codecType == CODECType::IAX2_CODEC_SLIN)
                t1 = &_transcoder1b;
            else if (_codecType == CODECType::IAX2_CODEC_SLIN_16K)
                t1 = &_transcoder1c;

            // Make PCM data
            int16_t pcm48k[BLOCK_SIZE_48K];
            _transcoder0.decode(frame.raw(), frame.size(), pcm48k, BLOCK_SIZE_48K);

            // Resample PCM data 
            // NOTE: MAKE THIS LARGE ENOUGH
            int16_t pcm_low[BLOCK_SIZE_8K * 2];
            _resampler.resample(pcm48k, BLOCK_SIZE_48K, pcm_low, blockSize);

            // NOTE: Make this big enough for any format!
            uint8_t code[BLOCK_SIZE_8K * 4];
            t1->encode(pcm_low, blockSize, code, codeSize);
            
            Message outFrame(Message::Type::AUDIO, _codecType,
                codeSize, code, frame.getOriginUs());
            outFrame.setSource(frame.getSourceBusId(), frame.getSourceCallId());
            outFrame.setDest(frame.getDestBusId(), frame.getDestCallId());

            if (_sink)
                _sink(outFrame);
        }
        else if (_codecType == CODECType::IAX2_CODEC_SLIN_48K) {
            // No support for interpolation
            if (frame.getType() == Message::Type::AUDIO) {
                // No conversion needed
                if (_sink)
                    _sink(frame);
            }
            else
                assert(false);
        }
    }
    else if (frame.getType() == Message::Type::AUDIO_INTERPOLATE) {
        // No support
        assert(false);
    } 
    // Non-audio messages are key passed right through, transcoding
    // not relevant here.
    else {
        if (_sink)
            _sink(frame);
    }
}

}
