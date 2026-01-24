#include <iostream>
#include <random>
#include <fstream>
#include <cmath>

#include <kc1fsz-tools/Common.h>

using namespace std;
using namespace kc1fsz;

// 1. Seed the random number engine.
// std::random_device provides a non-deterministic source of randomness (hardware entropy) 
// to seed the PRNG differently each time the program runs.
// These things are used in global space because of large stack consumption
static std::random_device rd;
static std::mt19937 gen(rd());



void test_1() {

    // Generates float values in the range [-1.0, 1.0).
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    fstream os("/home/bruce/white.pcm", ios::binary | ios::out);

    float omega = 2.0 * 3.1415926f * 440.0f / 8000.0f;
    float phi = 0;
    // This will show up as 0dB peak amplitude and -3dB RMS for a pure tone
    float a = 1.0;

    // 3. Generate white noise samples.
    int num_samples = 8000 * 4;
    for (int i = 0; i < num_samples; ++i) {
        int16_t s = (a * dist(gen) * 32767.0f);
        //int16_t s = a * std::cos(phi) * 32766.0f;
        uint8_t buf[2];
        pack_int16_le(s, buf);
        os.write((const char*)buf, 2);
        phi += omega;
    }
        
    os.close();
}

int main(int,const char**) {
    test_1();
}