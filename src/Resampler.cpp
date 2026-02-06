/**
 * Copyright (C) 2025, Bruce MacKinnon KC1FSZ, All Rights Reserved
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
#include <cstring>
#include <cassert>

#include "amp/Resampler.h"

namespace kc1fsz {
    namespace amp {

// REMEMBER: These are in reverse order but since they are symmetrical
// and an odd number this doesn't matter.
/*
const int16_t Resampler::F1_COEFFS[] = { 103, 136, 148, 74, -113, -395, -694,
    -881, -801, -331, 573, 1836, 3265, 4589, 5525, 5864, 5525,
    4589, 3265, 1836, 573, -331, -801, -881, -694, -395, -113,
    74, 148, 136, 103 };
*/
const int16_t Resampler::F1_COEFFS[] = {
    -151, -85, 8, 105, 181, 215, 194, 121, 9, -113, -214, -266, -252, -169, -35, 119, 255, 334, 331, 239, 75, -125, -311, -433, -451, -350, -142, 129, 400, 598, 660, 550, 270, -133, -574, -945, -1131, -1039, -615, 135, 1146, 2300, 3442, 4408, 5053, 5280, 5053, 4408, 3442, 2300, 1146, 135, -615, -1039, -1131, -945, -574, -133, 270, 550, 660, 598, 400, 129, -142, -350, -451, -433, -311, -125, 75, 239, 331, 334, 255, 119, -35, -169, -252, -266, -214, -113, 9, 121, 194, 215, 181, 105, 8, -85, -151
};

// REMEMBER: These are in reverse order but since they are symmetrical
// and an odd number this doesn't matter.
/*
const int16_t Resampler::F2_COEFFS[] = { 103, 136, 148, 74, -113, -395, -694,
    -881, -801, -331, 573, 1836, 3265, 4589, 5525, 5864, 5525,
    4589, 3265, 1836, 573, -331, -801, -881, -694, -395, -113,
    74, 148, 136, 103 };
*/
const int16_t Resampler::F2_COEFFS[] = {
    -151, -85, 8, 105, 181, 215, 194, 121, 9, -113, -214, -266, -252, -169, -35, 119, 255, 334, 331, 239, 75, -125, -311, -433, -451, -350, -142, 129, 400, 598, 660, 550, 270, -133, -574, -945, -1131, -1039, -615, 135, 1146, 2300, 3442, 4408, 5053, 5280, 5053, 4408, 3442, 2300, 1146, 135, -615, -1039, -1131, -945, -574, -133, 270, 550, 660, 598, 400, 129, -142, -350, -451, -433, -311, -125, 75, 239, 331, 334, 255, 119, -35, -169, -252, -266, -214, -113, 9, 121, 194, 215, 181, 105, 8, -85, -151
};

const int16_t Resampler::F16_COEFFS[] = {
// Computed using Parks-Macclenan with N=45, f0=7000, f1=8000
//    -114, 1338, 357, -75, -420, -412, 0, 489, 596, 138, -552, -851, -370, 605, 1242, 799, -648, -2008, -1822, 673, 4803, 8665, 10239, 8665, 4803, 673, -1822, -2008, -648, 799, 1242, 605, -370, -851, -552, 138, 596, 489, 0, -412, -420, -75, 357, 1338, -114

// Kaiser Window, N1 = 71. beta1 = 1, cutoff_hz1 = 7700
    -154, 69, 246, 198, -47, -269, -249, 17, 292, 309, 24, -314, -380, -79, 334, 465, 151, -353, -573, -252, 369, 715, 396, -382, -918, -620, 393, 1254, 1025, -401, -1956, -2010, 406, 4678, 8771, 10456, 8771, 4678, 406, -2010, -1956, -401, 1025, 1254, 393, -620, -918, -382, 396, 715, 369, -252, -573, -353, 151, 465, 334, -79, -380, -314, 24, 309, 292, 17, -249, -269, -47, 198, 246, 69, -154
};

void Resampler::setRates(unsigned inRate, unsigned outRate) {

    reset();

    _inRate = inRate;
    _outRate = outRate;

    if (_inRate == _outRate) {
        // No filter needed
    } else if (_inRate == 8000 && _outRate == 48000) {
        arm_fir_init_q15(&_lpfFilter, F1_TAPS, F1_COEFFS, _lpfState, BLOCK_SIZE_48K);
    } else if (_inRate == 48000 && _outRate == 8000) {
        //arm_fir_init_q15(&_lpfFilter, F2_TAPS, F2_COEFFS, _lpfState, BLOCK_SIZE_48K);
        arm_fir_decimate_init_q15(&_lpfDecimationFilter, F2_TAPS, 6, F2_COEFFS, 
            _lpfState, BLOCK_SIZE_48K);
    } else if (_inRate == 16000 && _outRate == 48000) {
        arm_fir_init_q15(&_lpfFilter, F16_TAPS, F16_COEFFS, _lpfState, BLOCK_SIZE_48K);
    } else if (_inRate == 48000 && _outRate == 16000) {
        //arm_fir_init_q15(&_lpfFilter, F16_TAPS, F16_COEFFS, _lpfState, BLOCK_SIZE_48K);
        arm_fir_decimate_init_q15(&_lpfDecimationFilter, F16_TAPS, 3, F16_COEFFS, 
            _lpfState, BLOCK_SIZE_48K);
    } else {
        assert(false);
    }
}

void Resampler::reset() {
    memset(_lpfState, 0, sizeof(_lpfState));
}

unsigned Resampler::getInBlockSize() const {
    return _getBlockSize(_inRate);
}

unsigned Resampler::getOutBlockSize() const {
    return _getBlockSize(_outRate);
}

unsigned Resampler::_getBlockSize(unsigned rate) const {
    if (rate == 8000)
        return BLOCK_SIZE_8K;
    if (rate == 16000)
        return BLOCK_SIZE_16K;
    else if (rate == 48000)
        return BLOCK_SIZE_48K;
    else 
        assert(false);
}

void Resampler::resample(const int16_t* inBlock, unsigned inSize, 
    int16_t* outBlock, unsigned outSize) {
    assert(_inRate != 0 && _outRate != 0);
    if (_inRate == _outRate) {
        assert(inSize == outSize);
        memcpy(outBlock, inBlock, sizeof(int16_t) * getInBlockSize());
    }
    else if (_inRate == 8000 && _outRate == 48000) {
        assert(inSize == BLOCK_SIZE_8K);
        assert(outSize == BLOCK_SIZE_48K);
        // Perform the upsampling to 48k.
        int16_t pcm48k_1[BLOCK_SIZE_48K];
        int16_t* p1 = pcm48k_1;
        const int16_t* p0 = inBlock;
        for (unsigned i = 0; i < BLOCK_SIZE_8K; i++, p0++)
            for (unsigned j = 0; j < 6; j++)
                *(p1++) = *p0;
        // Apply the LPF anti-aliasing filter
        arm_fir_q15(&_lpfFilter, pcm48k_1, outBlock, BLOCK_SIZE_48K);
    }
    // NOTE: This is a particularly performance-critical area given the 
    // scenario with a lot of 8K callers attached to a conference bridge
    // which natively runs at 48K.
    else if (_inRate == 48000 && _outRate == 8000) {
        assert(inSize == BLOCK_SIZE_48K);
        assert(outSize == BLOCK_SIZE_8K);
        // Decimate from 48k to 8k
        // Apply a LPF to the block because we are decimating.
        arm_fir_decimate_q15(&_lpfDecimationFilter, inBlock, outBlock, BLOCK_SIZE_48K);
    }
    else if (_inRate == 16000 && _outRate == 48000) {
        assert(inSize == BLOCK_SIZE_16K);
        assert(outSize == BLOCK_SIZE_48K);
        // Perform the upsampling to 48k.
        int16_t pcm48k_1[BLOCK_SIZE_48K];
        int16_t* p1 = pcm48k_1;
        const int16_t* p0 = inBlock;
        for (unsigned i = 0; i < BLOCK_SIZE_16K; i++, p0++)
            for (unsigned j = 0; j < 3; j++)
                *(p1++) = *p0;
        // Apply the LPF anti-aliasing filter
        arm_fir_q15(&_lpfFilter, pcm48k_1, outBlock, BLOCK_SIZE_48K);
    }
    else if (_inRate == 48000 && _outRate == 16000) {
        assert(inSize == BLOCK_SIZE_48K);
        assert(outSize == BLOCK_SIZE_16K);
        // Decimate from 48k 
        // Apply a LPF to the block because we are decimating.
        arm_fir_decimate_q15(&_lpfDecimationFilter, inBlock, outBlock, BLOCK_SIZE_48K);
    }
    else {
        assert(false);
    }
}
    }
}
