#include "dsp_util.h"

static const float PI = std::atan(1.0) * 4.0;

/**
 * This is an out-of-the-book implementation to use for sanity checking.
*/
void simpleDFT(cf32* in, cf32* out, uint16_t n) {
    for (uint16_t k = 0; k < n; k++) { 
        float sumreal = 0;
        float sumimag = 0;
        for (uint16_t t = 0; t < n; t++) {  // For each input element
            float angle = 2.0 * PI * (float)t * (float)k / (float)n;
            sumreal +=  in[t].r * std::cos(angle) + in[t].i * std::sin(angle);
            sumimag += -in[t].r * std::sin(angle) + in[t].i * std::cos(angle);
        }
        out[k].r = sumreal / (float)n;
        out[k].i = sumimag / (float)n;
    }
}

uint16_t maxMagIdx(const cf32* data, uint16_t start, uint16_t dataLen) {
    float maxMag = 0;
    uint16_t maxIdx = 0;
    for (uint16_t i = start; i < dataLen; i++) {
        float mag = data[i].mag();
        if (mag > maxMag) {
            maxMag = mag;
            maxIdx = i;
        }
    }
    return maxIdx;
}

