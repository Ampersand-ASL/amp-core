/**
 * Software Defined Repeater Controller
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
 *
 * NOT FOR COMMERCIAL USE WITHOUT PERMISSION.
 */

 /*
 Extensive unit testing for the soft DTMF decoder.
 */
#include <iostream>
#include <cmath>
#include <cassert>
#include <random>

//#include <arm_math.h>
#define PI (3.1415926f)

#include "TestClock.h"
#include "kc1fsz-tools/DTMFDetector2.h"

using namespace std;
using namespace kc1fsz;

static void addWhiteNoise(float* out, unsigned n, float std) {
    // Obtain a random number from hardware
    std::random_device rd;
    // Seed the generator
    std::default_random_engine gen(rd()); 
    // Create a normal distribution object with a mean of 0 and the desired standard deviation
    std::normal_distribution<double> distrib(0.0, std);

    for (unsigned int i = 0; i < n; i++)
        out[i] += distrib(gen);
}

int main(int,const char**) {

    {
            // Doubling voltage results in a +6dB power change
            float vp0 = 1.5;
            float vp1 = 1.0;
            float vrms0 = vp0 * 0.707;
            float vrms1 = vp1 * 0.707;
            float vms0 = vrms0 * vrms0;
            float vms1 = vrms1 * vrms1;
            float db = 10.0 * std::log10(vms0 / vms1);
            printf("vp0=%f, vp1=%f, db=%f\n", vp0, vp1, db);
    }

    const unsigned FS = 8000;
    const unsigned N = 64;
    const float noise = 0.01;
    const float threshold = -55;
    const float thresholdLinear = pow(10.0, (threshold / 20.0));

    printf("Noise STD %f\n", noise);
    printf("Vrms detection threshold %f\n", thresholdLinear);

    TestClock clock;
    DTMFDetector2 detector(clock);
    detector.setSignalThreshold(threshold);

    float silence[N];
    for (unsigned int i = 0; i < N; i++) 
        silence[i] = 0;
    addWhiteNoise(silence, N, noise);
    // Compute the RMS of the silence
    float rt = 0;
    for (unsigned int i = 0; i < N; i++) 
        rt += silence[i] * silence[i];
    rt = sqrt(rt);
    printf("Silence Vrms %f\n", rt);
    printf("Silence Vrms**s %f\n", rt * rt);

    float vrms_target = pow(10.0, (-20.0/20.0));
    printf("Vrms target %f\n", vrms_target);
    float vp_target = vrms_target * (1.0/0.707);
    printf("Vp target %f\n", vp_target);

    {
        cout << "----- Test 1 ------" << endl;
        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + b;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        // 40ms silence then 40ms tone (valid DSC)
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(detector.isDetectionPending());
        assert(detector.popDetection() == '4');
        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 1a (Unacceptable twist col > row) ------" << endl;
        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            // COLUMN 0. x2.0 linear is +6dB, which should be unacceptable.
            float a = vp_target * 2 * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            // ROW 1
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + b;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 1b (Acceptable twist, col > row) ------" << endl;

        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            // COLUMN 0. x1.5 linear is +3.5dB, which should be acceptable.
            float a = vp_target * 1.5 * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            // ROW 1
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + b;
            // Scale up to a int32 fixed 
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        // 40ms tone w/ 40ms silence (valid DSC)
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(detector.isDetectionPending());
        assert(detector.popDetection() == '4');
        assert(!detector.isDetectionPending());
    }

    cout << "----- Test 1c (Two Symbols) ------" << endl;
    {
        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + b;
            // Scale up to a int32 fixed 
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(detector.isDetectionPending());
        assert(detector.popDetection() == '4');
        assert(!detector.isDetectionPending());

        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 941.0 / (float)FS);
            float t = a + b;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);

        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(detector.isDetectionPending());
        assert(detector.popDetection() == '*');
        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 1d (Acceptable reverse twist col < row) ------" << endl;
        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            // COLUMN 0
            float a = vp_target * 1 * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            // ROW 1. x2.0 linear is +6dB, which should be acceptable.
            float b = vp_target * 2 * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + b;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(detector.isDetectionPending());
        detector.popDetection();
    }

    {
        cout << "----- Test 1e (Unacceptable reverse twist col < row) ------" << endl;
        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            // COLUMN 0
            float a = vp_target * 1 * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            // ROW 1. x4.0 linear is +12dB, which should be unacceptable.
            float b = vp_target * 4 * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + b;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 2 (Bad freq) ------" << endl;
        // Make a bogus signal (48ms)
        float testBad[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two tones
            float a = 0.45 * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            // Invalid freq by 30 hz, which should be enough to break the 58Hz
            // resolution requirement.
            float b = 0.5 * cos((float)i * 2.0 * PI * (770 - 30) / (float)FS);
            float t = a + b;
            testBad[i] = t;
        }
        addWhiteNoise(testBad, N * 6, noise);

        // 40ms bad tone w/ 40ms silence (not valid DSC)
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);

        detector.processBlock(testBad);
        detector.processBlock(testBad + N);
        detector.processBlock(testBad + N * 2);
        detector.processBlock(testBad + N * 3);
        detector.processBlock(testBad + N * 4);
        detector.processBlock(testBad + N * 5);

        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 3 (Too short) ------" << endl;
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + b;
            // Scale up to a int32 fixed 
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        // 20ms tone w/ 40ms silence (not valid DSC)
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + 2 * N);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);

        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 4 (Tone with break) ------" << endl;
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + b;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        // Long tone with a break in the middle
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);

        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + 2 * N);
        detector.processBlock(test1 + 3 * N);
        detector.processBlock(silence);
        detector.processBlock(test1 + 4 * N);
        detector.processBlock(test1 + 5 * N);
        detector.processBlock(silence);
        detector.processBlock(silence);

        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 5 (Valid tone with break) ------" << endl;
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + b;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        // Long tone with a break in the middle
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);

        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + 2 * N);
        detector.processBlock(test1 + 3 * N);
        detector.processBlock(test1 + 4 * N);
        detector.processBlock(test1 + 5 * N);
        // Pick up the detection
        assert(detector.isDetectionPending());
        assert(detector.popDetection() == '4');

        // Short gap
        detector.processBlock(silence);

        // Keep going
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + 2 * N);
        detector.processBlock(test1 + 3 * N);
        detector.processBlock(test1 + 4 * N);
        detector.processBlock(test1 + 5 * N);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);

        // No new detection
        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 6a (Excessive column harmonic) ------" << endl;
        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            // Two valid tones
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float a2 = vp_target * cos((float)i * 2.0 * PI * 1209.0 * 2.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + a2 + b;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        // 40ms silence then 40ms tone (valid DSC)
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(!detector.isDetectionPending());
    }
    
    {
        cout << "----- Test 6b (Acceptable column harmonic) ------" << endl;
        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float a2 = vp_target * 0.07 * cos((float)i * 2.0 * PI * 1209.0 * 2.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float t = a + a2 + b;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        // 40ms silence then 40ms tone (valid DSC)
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(detector.isDetectionPending());
        assert(detector.popDetection() == '4');
        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 6c (Excessive row harmonic) ------" << endl;
        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float b2 = vp_target * cos((float)i * 2.0 * PI * 770.0 * 2.0 / (float)FS);
            float t = a + b + b2;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(!detector.isDetectionPending());
        //assert(detector.popDetection() == '4');
        //assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 6d (Acceptable row harmonic) ------" << endl;
        // Make a test signal (48ms)
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            float a = vp_target * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float b = vp_target * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            float b2 = vp_target * 0.7 * cos((float)i * 2.0 * PI * 770.0 * 2.0 / (float)FS);
            float t = a + b + b2;
            test1[i] = t;
        }
        addWhiteNoise(test1, N * 6, noise);

        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(!detector.isDetectionPending());
    }

    {
        cout << "----- Test 7 (Signal Too Low) ------" << endl;
        float test1[N * 6];
        for (unsigned int i = 0; i < N * 6; i++) {
            float a = vp_target * 0.05 * cos((float)i * 2.0 * PI * 1209.0 / (float)FS);
            float b = vp_target * 0.05 * cos((float)i * 2.0 * PI * 770.0 / (float)FS);
            test1[i] = a + b;
        }
        addWhiteNoise(test1, N * 6, noise);

        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(silence);
        detector.processBlock(test1);
        detector.processBlock(test1 + N);
        detector.processBlock(test1 + N * 2);
        detector.processBlock(test1 + N * 3);
        detector.processBlock(test1 + N * 4);
        detector.processBlock(test1 + N * 5);

        assert(!detector.isDetectionPending());
    }

    return 0;
}
