#pragma once

#include <functional>
#include <cinttypes>
#include <cmath>

#include "kc1fsz-tools/fixed_math.h"

typedef float f32;

float pi();

/**
 * Floating-point complex number
*/
struct cf32 {

    float r = 0;
    float i = 0;

    cf32() : r(0), i(0) {}
    cf32(float ar, float ai) : r(ar), i(ai) {}
    cf32(const cf32& other) : r(other.r), i(other.i) { }

    float mag() const {
        return std::sqrt(magSquared());
    }
    float magSquared() const {
        return r * r + i * i;
    }
    /**
     * @returns Phase angle in radians.
     */
    float phase() const {
        return std::atan2(i, r);
    }
    /**
     * @returns this number plus b.
     */
    cf32 add(cf32 b) const {
        return cf32(r + b.r, i + b.i);
    }
    /**
     * @returns this number times b.
     */
    cf32 mult(cf32 b) const {
        return cf32(r * b.r - i * b.i, r * b.i + i * b.r);
    }
};

/**
 * An extremely simplistic DFT.  Not usable for performance-critical 
 * applications!
 */
void simpleDFT(cf32* in, cf32* out, uint16_t fftN);

/**
 * Searches across an array of complex numbers and finds the 
 * index with the largets magnitude.
 */
uint16_t maxMagIdx(const cf32* data, uint16_t start, uint16_t dataLen);

