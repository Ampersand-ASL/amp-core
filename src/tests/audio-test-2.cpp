#include <iostream>
#include <cmath>

#include "kc1fsz-tools/fixed_math.h"

#include "amp/Resampler.h"
#include "dsp_util.h"

using namespace std;
using namespace kc1fsz;

// https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=32196

// Need to match the DFT library
const double PI64 = std::atan(1.0) * 4.0;
const unsigned BLOCK_SIZE_48K = 960;
const unsigned BLOCK_SIZE_8K = 160;
const unsigned BLOCKS_48K = 4;
const unsigned BLOCKS_8K = BLOCKS_48K * 6;
const unsigned FFT_N = 1024;
const double sampleHz = 48000;
const double resolutionHz = sampleHz / (double)FFT_N;

float analyze(int16_t* audioIn48) {

    // FFT analysis   
    cf64 fftIn[FFT_N];
    cf64 fftOut[FFT_N];
    for (unsigned i = 0; i < FFT_N; i++) 
        fftIn[i] = cf64((double)audioIn48[i] / 32767.0, 0);
    simpleDFT64(fftIn, fftOut, FFT_N);
    // Only look at the left-side of the spectrum for the max mag search
    unsigned fundBucket = maxMagIdx64(fftOut, 0, FFT_N / 2);

    // Compute distortion
    double fundMag = fftOut[fundBucket].mag();
    // Two-sided power!
    double fundPower = (fundMag * fundMag) * 2.0;
    double totalPower = 0;
    double otherPower = 0;

    for (unsigned i = 0; i < FFT_N; i++) {
        // IMPORTANT: This FFT implementation contains the /N internally!
        double power = std::pow(fftOut[i].mag(), 2.0);
        totalPower += power;
        // Exclude the fundamental on both sides of the spectrum
        if (i != fundBucket && i != FFT_N - fundBucket)
            otherPower += power;
    }

    /*
    cout << "Fund bin         " << fundBucket << endl;
    cout << "Total Power      " << totalPower << endl;
    cout << "Fund  Power      " << fundPower << endl;
    cout << "Fund/total  dB   " << 10.0 * log10(fundPower / totalPower) << endl;
    cout << "Other/total dB   " << 10.0 * log10(otherPower / totalPower) << endl;
    cout << "Other/fund dB    " << 10.0 * log10(otherPower / fundPower) << endl;
    */

    // The figure of merit is other vs fundamental
    return 10.0 * log10(otherPower / fundPower);
}

int main(int, const char**) {

    // Generate some audio 
    int16_t audioIn48[BLOCK_SIZE_48K * BLOCKS_48K];    

    double m = 2;
    double step = 2;
    double endHz = 4000;

    while (m * resolutionHz < endHz) {

        // Choosing a frequency that is coherent
        double amp = 0.5;
        double toneHz = m * resolutionHz;
        double omega = 2.0f * PI64 * toneHz / sampleHz;
        double phi = 0;   
        for (unsigned i = 0; i < BLOCK_SIZE_48K * BLOCKS_48K; i++) {
            audioIn48[i] = round(amp * std::cos(phi) * 32767.0);
            phi += omega;
        }
        //double vrms = amp / std::sqrt(2.0);
        //cout << "Signal freq  " << toneHz << endl;
        //cout << "Signal Vrms  " << vrms << endl;
        //cout << "Signal power " << vrms * vrms << endl;

        double preDb = analyze(audioIn48);

        // Now put the signal through the resampling
        amp::Resampler res;
        res.setRates(48000, 8000);

        // Down-sample
        int16_t audioIn8[BLOCK_SIZE_8K * BLOCKS_48K];
        unsigned p48 = 0;
        unsigned p8 = 0;

        for (unsigned i = 0; i < BLOCKS_48K; i++) {
            res.resample(audioIn48 + p48, BLOCK_SIZE_48K, audioIn8 + p8, BLOCK_SIZE_8K);
            p48 += BLOCK_SIZE_48K;
            p8 += BLOCK_SIZE_8K;
        }

        // Up-sample 
        res.setRates(8000, 48000);
        int16_t audioOut48[BLOCK_SIZE_48K * BLOCKS_8K];
        p48 = 0;
        p8 = 0;

        for (unsigned i = 0; i < BLOCKS_8K; i++) {
            res.resample(audioIn8 + p8, BLOCK_SIZE_8K, audioOut48 + p48, BLOCK_SIZE_48K);
            p48 += BLOCK_SIZE_48K;
            p8 += BLOCK_SIZE_8K;
        }

        // Start the analysis a few blocks in to avoid filter loading artifacts
        //cout << "After down sample/up sample" << endl;
        double postDb = analyze(audioOut48 + BLOCK_SIZE_48K * 2);

        cout << toneHz << "," << preDb << "," << postDb << endl;

        m += step;
    }
}
