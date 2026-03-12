#include <iostream>
#include <cmath> 
#include <ctime>
#include <fstream>
#include <cassert>
#include <ctime>
#include <cstring>
#include <random>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/StdPollTimer.h"
#include "kc1fsz-tools/StdPollTimer.h"
#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/NetUtils.h"

using namespace std;
using namespace kc1fsz;

// 1. Seed the random number engine.
// std::random_device provides a non-deterministic source of randomness (hardware entropy) 
// to seed the PRNG differently each time the program runs.
// These things are used in global space because of large stack consumption
static std::random_device rd;
static std::mt19937 gen(rd());
// Generates float values in the range [0.0, 1.0).
static std::uniform_real_distribution<float> dist(0.0f, 1.0f);

static int realign(const char* infn, const char* outfn, unsigned sampleCount,
    float gain = 1.0, float noiseAmp = 0.02) {
    ifstream inFile(infn, std::ios::binary);
    ofstream outFile(outfn, std::ios::binary);
    if (!inFile.is_open()) 
        return -1;
    if (!outFile.is_open()) 
        return -2;
    short s = 0;

    // Pad the output file with silence
    for (unsigned i = 0; i < sampleCount; i++)
        outFile.write(reinterpret_cast<char*>(&s), sizeof(short));

    while (!inFile.eof()) {
        inFile.read(reinterpret_cast<char*>(&s), sizeof(short));
        float sf = s * gain;
        s = sf;

        // Additive white noise
        float noise = noiseAmp * dist(gen) * 32767.0f;
        s += noise;

        outFile.write(reinterpret_cast<const char*>(&s), sizeof(short));
    }
    return 0;
}

static int vote0(const char* infn0, const char* infn1, const char* outfn,
    unsigned logBlock) {

    ifstream inFile0(infn0, std::ios::binary);
    ifstream inFile1(infn1, std::ios::binary);
    ofstream outFile(outfn, std::ios::binary);
    if (!inFile0.is_open()) 
        return -1;
    if (!inFile1.is_open()) 
        return -1;
    if (!outFile.is_open()) 
        return -2;       
    
    const unsigned BLOCK_SIZE_8K = 160;
    short s0[BLOCK_SIZE_8K];
    short s1[BLOCK_SIZE_8K];

    unsigned block = 0;
    unsigned blockCount = 0;
    unsigned vote = 0;

    float fade0 = 1;
    float fadeStep0 = 0;
    float fadeTarget0 = 1;

    float fade1 = 0;
    float fadeStep1 = 0;
    float fadeTarget1 = 0;

    short hist0[BLOCK_SIZE_8K * 3] = { 0 };
    short hist1[BLOCK_SIZE_8K * 3] = { 0 };

    unsigned bestOffset = 0;

    unsigned searchMax = 20;
    unsigned searchStep = 1;

    unsigned hist[160] = { 0 };

    while (!inFile0.eof() && !inFile1.eof()) {

        inFile0.read(reinterpret_cast<char*>(s0), sizeof(short) * BLOCK_SIZE_8K);
        inFile1.read(reinterpret_cast<char*>(s1), sizeof(short) * BLOCK_SIZE_8K);

        // Keep history
        memmove(hist0, hist0 + BLOCK_SIZE_8K, sizeof(short) * BLOCK_SIZE_8K * 2);
        memcpy(hist0 + BLOCK_SIZE_8K * 2, s0, sizeof(short) * BLOCK_SIZE_8K);
        memmove(hist1, hist1 + BLOCK_SIZE_8K, sizeof(short) * BLOCK_SIZE_8K * 2);
        memcpy(hist1 + BLOCK_SIZE_8K * 2, s1, sizeof(short) * BLOCK_SIZE_8K);

        float rms = 0;
        for (unsigned i = 0; i < BLOCK_SIZE_8K; i++)
            rms += hist0[i] * hist0[i];
        rms = std::sqrt(rms / (float)BLOCK_SIZE_8K);

        float bestCorr = 0;

        for (unsigned offset = 0; offset < searchMax; offset += searchStep) {

            float corr = 0;
            for (unsigned i = 0; i < BLOCK_SIZE_8K; i++)
                corr += hist0[i] * hist1[i + offset];
            if (corr > 0)
                corr = corr / (float)BLOCK_SIZE_8K;

            if (corr > 0 && corr >= bestCorr) {
                bestCorr = corr;
                bestOffset = offset;
            }

            if (block == logBlock) {
                cout << offset << " " << corr << endl;
            }
        }

        cout << "Block " << block << " rms " << rms << " best corr/offset " << 
            bestCorr << "/" << bestOffset << endl;

        if (block < 250) {
            // Track histogram
            hist[bestOffset]++;
        }

        // Do the blending
        short outBlock[BLOCK_SIZE_8K];

        for (unsigned i = 0; i < BLOCK_SIZE_8K; i++) {
            float sf0 = s0[i];
            float sf1 = s1[i];
            outBlock[i] = sf0 * fade0 + sf1 * fade1;
        }

        outFile.write(reinterpret_cast<const char*>(outBlock), sizeof(short) * BLOCK_SIZE_8K);
        
        fade0 += fadeStep0;
        if (fade0 < 0) {
            fade0 = 0;
            fadeStep0 = 0;
        } else if (fade0 > 1) {
            fade0 = 1;
            fadeStep0 = 0;
        }
        
        fade1 += fadeStep1;
        if (fade1 < 0) {
            fade1 = 0;
            fadeStep1 = 0;
        } else if (fade1 > 1) {
            fade1 = 1;
            fadeStep1 = 0;
        }

        blockCount++;

        if (blockCount == 50) {
            if (vote == 0) {
                vote = 1;
                fadeStep0 = -1.0 / (float)(BLOCK_SIZE_8K * 3);
                fadeTarget0 = 0.0;
                fadeStep1 = 1.0 / (float)(BLOCK_SIZE_8K * 3);
                fadeTarget1 = 1.0;
            } else if (vote == 1) {
                vote = 0;
                fadeStep1 = -1.0 / (float)(BLOCK_SIZE_8K * 3);
                fadeTarget1 = 0.0;
                fadeStep0 = 1.0 / (float)(BLOCK_SIZE_8K * 3);
                fadeTarget0 = 1.0;
            }
            blockCount = 0;
        }

        block++;
    }

    // Display histogram
    for (unsigned i = 0; i < searchMax; i += searchStep) 
        cout << (float)i * 125.0 / 1000.0f << "," << (float)hist[i] / 250.0f << endl;

    return 0;
}

int main(int, const char**) {

    int rc = realign("/mnt/c/tmp/ASL-QSO-1.raw", "/mnt/c/tmp/ASL-QSO-1-a.raw", 0,
        1.0, 0.4);
    if (rc != 0) {
        cout << "Realigned failed" << endl;
        return -1;
    }

    rc = realign("/mnt/c/tmp/ASL-QSO-1.raw", "/mnt/c/tmp/ASL-QSO-1-b.raw", 16,
        1.1, 0.4);
    if (rc != 0) {
        cout << "Realigned failed" << endl;
        return -1;
    }

    rc = vote0("/mnt/c/tmp/ASL-QSO-1-a.raw", "/mnt/c/tmp/ASL-QSO-1-b.raw", 
        "/mnt/c/tmp/ASL-QSO-2.raw", 257);
    if (rc != 0) {
        cout << "vote0 failed" << endl;
        return -1;
    }
}

