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
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include <cmath>
#include <cassert>
#include <algorithm>

#include <alsa/asoundlib.h>

/*
This test program is used to get to the bottom of some USB capture timing
issues.

Notes on the behavior of this program:

What we see is several of the periods taking more than 20ms, and then suddenly
one capture taking much less that lets us "catch up." 

bruce@pi5:~/pico/microlink2/sw/ml2/build $ make tone-test-2; ./tone-test-2 
[100%] Built target tone-test-2
period time = 20000 us
period size = 960 frames
Read frames   960 delay        1 error count    11047
Read frames   960 delay    21108 error count    11047
Read frames   960 delay    21436 error count    11047
Read frames   960 delay    21117 error count    11047
Read frames   960 delay    21131 error count    11047
Read frames   960 delay    21438 error count    11047
Read frames   960 delay    21035 error count    11047
Read frames   960 delay    10638 error count    11047
Read frames   960 delay    21270 error count    11047
Read frames   960 delay    21313 error count    11047
Read frames   960 delay    21249 error count    11047
Read frames   960 delay    21645 error count    11047
Read frames   960 delay    21044 error count    11047
Read frames   960 delay    22838 error count    11047
Read frames   960 delay    23794 error count    11047
Read frames   960 delay     6393 error count    11047
Read frames   960 delay    21233 error count    11047
Read frames   960 delay    21470 error count    11047

When the period size is set down to 5ms we get more 
frequent small "frames" that make the overall jitter
less variable. See below, the 20ms frames are alternating
between -1.2ms late and +1.2ms early.

Read frames   128 delay     2495 error count      279
     Send jitter (+=fast,-=slow)  1180
Read frames   128 delay     2575 error count      279
Read frames   128 delay     2633 error count      279
Read frames   128 delay     2744 error count      279
Read frames   128 delay     2583 error count      279
Read frames   128 delay     2569 error count      279
Read frames   128 delay     2657 error count      279
Read frames   128 delay     2662 error count      279
Read frames   128 delay     2653 error count      279
     Send jitter (+=fast,-=slow) -1158
Read frames   128 delay     2644 error count      279
Read frames   128 delay     2662 error count      279
Read frames   128 delay     2711 error count      279
Read frames   128 delay     3936 error count      279
Read frames   128 delay     1419 error count      279
Read frames   128 delay     2657 error count      279
Read frames   128 delay     2876 error count      279
     Send jitter (+=fast,-=slow)  1146
Read frames   128 delay     2497 error count      279
Read frames   128 delay     2827 error count      279
Read frames   128 delay     2514 error count      279
Read frames   128 delay     4011 error count      279
Read frames   128 delay     1434 error count      279
Read frames   128 delay     2100 error count      279
Read frames   128 delay     2540 error count      279
Read frames   128 delay     2543 error count      279
     Send jitter (+=fast,-=slow) -1374
*/

// Interesting, this seems to make a difference in jitter behavior.
// The hw:0 has less jitter.
//static const char* alsaDeviceName = "default";
static const char* alsaDeviceName = "hw:0";

int main(int, const char**) {

    int err;
    snd_pcm_t* captureH = 0;
    unsigned int audioRate = 48000;
    unsigned int channels = 2;
    // With this setting we're getting around 128 audio samples per
    // period, which leads to a good range of jitters.
    //unsigned int periodTimeUs = 5000;
    unsigned int periodTimeUs = 20000;
    // Let the buffer store 8 20ms frames of sound
    unsigned int bufferTimeUs = 20000 * 8;
    const unsigned frameSize = 960;

    if ((err = snd_pcm_open(&captureH, alsaDeviceName, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
        printf("Cannot open capture device %d", err);
        return -1;
    }

    // No free needed, alloca() frees memory on function exit
    snd_pcm_hw_params_t* capture_hw_params;
    snd_pcm_hw_params_alloca(&capture_hw_params);
    snd_pcm_hw_params_any(captureH, capture_hw_params);
    snd_pcm_hw_params_set_access(captureH, capture_hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(captureH, capture_hw_params, SND_PCM_FORMAT_S16_LE);
    // The last paramter (sub unit direction) is for near calls. Use 1 to request a rate 
    // greater than the specified value, -1 for a rate less than the value, and 0 for a 
    // rate that is exactly the value. 
    snd_pcm_hw_params_set_rate_near(captureH, capture_hw_params, &audioRate, 0);
    snd_pcm_hw_params_set_channels_near(captureH, capture_hw_params, &channels);
    // Request an exact period
    //snd_pcm_hw_params_set_period_time_near(captureH, capture_hw_params, &periodTimeUs, 0);
    // Request a max period
    snd_pcm_hw_params_set_period_time_max(captureH, capture_hw_params, &periodTimeUs, 0);
    snd_pcm_hw_params_set_buffer_time(captureH, capture_hw_params, bufferTimeUs, 0);
    snd_pcm_hw_params_set_rate_resample(captureH, capture_hw_params, 0);
    
    if ((err = snd_pcm_hw_params(captureH, capture_hw_params)) < 0) {
        printf("Capture parameters %d", err);
        return -1;
    }

    // Display some parameters to make sure
    unsigned val;
    int dir;
    snd_pcm_uframes_t frames;
    snd_pcm_hw_params_get_period_time(capture_hw_params, &val, &dir);
    printf("period time = %d us\n", val);
    snd_pcm_hw_params_get_period_size(capture_hw_params, &frames, &dir);
    printf("period size = %d frames\n", (int)frames);
    snd_pcm_hw_params_get_buffer_time(capture_hw_params, &val, &dir);
    printf("buffer time = %d us\n", val);

    // Get the ball rolling with the capture
    if ((err = snd_pcm_prepare(captureH)) < 0) {
        printf("Cannot prepare audio interface for use (%s)\n",
	        snd_strerror (err));
        return -1;
    }
    if ((err = snd_pcm_start(captureH)) < 0) {
        printf("Cannot start audio interface for use (%s)\n",
	        snd_strerror (err));
        return -1;
    }

    // Here we use a feature in ALSA that can extra a set of pollfd's that
    // can be passed directly to poll() to implement an effcient wait 
    // on capture activity.
    const unsigned fdsCapacity = 16;
    pollfd fds[fdsCapacity];
    int fdsUsed = snd_pcm_poll_descriptors(captureH, fds, fdsCapacity);
    if (fdsUsed < 0) {
        printf("FD problem 2\n");
        return -1;
    }

    assert(fdsUsed == 1);

    struct timeval tv0;
    gettimeofday(&tv0, NULL); 

    unsigned errorCount = 0;
    unsigned blockSize = 0;
    long lastBlockTimeUs = (long)tv0.tv_sec * 1000000 + tv0.tv_usec;
    long lastGapUs = 0;

    // Simulation of main loop
    while (true) {

        gettimeofday(&tv0, NULL); 
        long us0 = (long)tv0.tv_sec * 1000000 + tv0.tv_usec;

        // Block waiting for activity or timeout
        int rc = poll(fds, fdsUsed, 1000);

        gettimeofday(&tv0, NULL); 
        long us1 =  (long)tv0.tv_sec * 1000000 + tv0.tv_usec;

        if (rc < 0) {
            printf("Poll error 2");
        } 
        else if (rc > 0) {

            // Do work based on what we were told happend
            const int usbBufferSize = frameSize * 2 * 2;
            uint8_t usbBuffer[usbBufferSize];
            // Read what is needed to form a complete frame (and no more)
            int samples = snd_pcm_readi(captureH, usbBuffer, frameSize);
            if (samples > 0) {
                blockSize += samples;
                printf("Read frames %5d delay %8ld error count %8u\n", samples, (us1 - us0),
                    errorCount);
                // Time to send?
                if (blockSize > frameSize) {                
                    blockSize -= frameSize;                        
                    lastGapUs = us0 - lastBlockTimeUs;
                    lastBlockTimeUs = us0;
                    printf("     Send jitter (+=fast,-=slow) %5ld\n", (20000 - lastGapUs));
                }
            } else if (samples == -11) {
                // NOTE: In this test there is some period during startup when 
                // snd_pcm_readi() consistently returns a -11 error. That clears
                // after a bit of time, possibly once the first period has been 
                // captured.
                errorCount++;
                snd_pcm_recover(captureH, samples, 0); 
            } else if (samples < 0) {
                errorCount++;
                printf("Error %d\n", samples);
                snd_pcm_recover(captureH, samples, 0); 
            }
        }
    }

    return 0;
}
