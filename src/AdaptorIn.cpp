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

#include "Message.h"
#include "AdaptorIn.h"

namespace kc1fsz {

void AdaptorIn::setCodec(CODECType codecType) {
    _codecType = codecType;
    unsigned rate = codecSampleRate(_codecType);
    if (rate == 0)
        assert(false);
    _resampler.setRates(rate, 48000);
}

void AdaptorIn::consume(const Message& frame) {
    
    if (frame.getType() == Message::Type::AUDIO ||
        frame.getType() == Message::Type::AUDIO_INTERPOLATE) {
    
        if (_codecType == CODECType::IAX2_CODEC_G711_ULAW ||
            _codecType == CODECType::IAX2_CODEC_SLIN ||
            _codecType == CODECType::IAX2_CODEC_SLIN_16K) { 
            
            Transcoder* t0 = 0;
            if (_codecType == CODECType::IAX2_CODEC_G711_ULAW)
                t0 = &_transcoder0a;
            else if (_codecType == CODECType::IAX2_CODEC_SLIN) 
                t0 = &_transcoder0b;
            else if (_codecType == CODECType::IAX2_CODEC_SLIN_16K) 
                t0 = &_transcoder0c;

            // Make PCM data
            // MAKE SURE THIS IS LARGE ENOUGH!
            int16_t pcm[BLOCK_SIZE_8K * 2];
            if (frame.getType() == Message::Type::AUDIO) {
                t0->decode(frame.raw(), frame.size(), pcm, codecBlockSize(_codecType));
            } else {
                t0->decodeGap(pcm, codecBlockSize(_codecType));
            }

            // Resample PCM data up to 48K
            int16_t pcm48k[BLOCK_SIZE_48K];
            _resampler.resample(pcm, codecBlockSize(_codecType), pcm48k, BLOCK_SIZE_48K);
            
            // Transcode to SLIN_48K
            uint8_t slin48k[BLOCK_SIZE_48K * 2];
            _transcoder1.encode(pcm48k, BLOCK_SIZE_48K, slin48k, BLOCK_SIZE_48K * 2);
            Message outFrame(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K,
                BLOCK_SIZE_48K * 2, slin48k, frame.getOriginUs());
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
            } else
                assert(false);
        }
        else {
            assert(false);
        }
    }
    else {
        if (_sink)
            _sink(frame);
    }
}

}
