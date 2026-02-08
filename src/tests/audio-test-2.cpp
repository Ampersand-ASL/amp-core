#include <iostream>
#include <cmath>

#include "kc1fsz-tools/fixed_math.h"

#include "dsp_util.h"

using namespace std;

int main(int, const char**) {

    const float PI = 3.1415926f;
    const unsigned BLOCK_SIZE_48K = 960;
    const unsigned BLOCKS = 4;

    // Generate some audio 
    int16_t audioIn[BLOCK_SIZE_48K * BLOCKS];    

    float sampleHz = 48000;
    float toneHz = 400;
    float omega = 2.0f * PI * toneHz / sampleHz;
    float phi = 0;
    
    for (unsigned i = 0; i < BLOCK_SIZE_48K * BLOCKS; i++) {
        int16_t sample = round(0.5 * std::cos(phi) * 32767.0f);
        audioIn[i] = sample;
        phi += omega;
    }

    // FFT analysis
    const unsigned FFT_N = 2048;
    float w[FFT_N];
    for (unsigned i = 0; i < FFT_N; i++) 
        w[i] = 0.5 * (1 - std::cos(2.0 * PI * (float)i / (float)FFT_N));
    
    cf32 fftIn[FFT_N];
    cf32 fftOut[FFT_N];
    for (unsigned i = 0; i < FFT_N; i++) 
        fftIn[i] = cf32((float)audioIn[i]  / 32767.0f, 0);
    simpleDFT(fftIn, fftOut, FFT_N);
    int gotBucket = maxMagIdx(fftOut, 0, FFT_N / 2);

    cout << "Input freq/mag " << sampleHz * gotBucket / FFT_N  << " " 
        << fftOut[gotBucket].mag() << endl;
    // Compute distortion
    float fundamentalRms = sqrt(fftOut[gotBucket].magSquared() / FFT_N);

    float otherRms = 0;
    for (unsigned i = 1; i < FFT_N / 2; i++) {
        if (i != gotBucket)
            otherRms += fftOut[i].magSquared();
    }
    otherRms /= FFT_N;
    otherRms = std::sqrt(otherRms);

    cout << otherRms / fundamentalRms << endl;
    cout << 10.0 * log10(otherRms / fundamentalRms) << endl;

    //for (unsigned i = 0; i < FFT_N / 2; i++) 
    //    cout << i << " " << fftOut[i].mag() << endl;
}
