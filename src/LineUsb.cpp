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

#include <cmath>
#include <cassert>
#include <algorithm>
#include <cstring>

// NOTE: This may be the real ARM library or a mock, depending on the
// platfom that we are building for.
#include <arm_math.h>

#include <kc1fsz-tools/Log.h>
#include <kc1fsz-tools/raiiholder.h>

#include "IAX2Util.h"
#include "MessageConsumer.h"
#include "Transcoder_SLIN_48K.h"
#include "LineUsb.h"

using namespace std;

namespace kc1fsz {

// "A close look at ALSA paper": https://www.volkerschatz.com/noise/alsa.html
// IMPORTANT: https://equalarea.com/paul/alsa-audio.html
// IMPORTANT: https://www.linuxjournal.com/article/6735?page=0,1
// IMPORTANT: https://0pointer.de/blog/projects/all-about-periods.html
//
// A good article on volume: https://www.dr-lex.be/info-stuff/volumecontrols.html
/*
Useful: https://unix.stackexchange.com/questions/561725/disable-volume-controls-on-external-speaker-connected-through-usb-sound-card

Edited /usr/share/pulseaudio/alsa-mixer/paths/analog-output.conf.common
[PCM] section, volume=ignore
*/

/*
Notes on OSS Compatibility
==========================

Current app_rpt access to sound card: /dev/dsp. This is the OSS identification. "/dev/dsp is the 
default audio device in the system. It's connected to the main speakers and the primary 
recording source (such as microphone). The system administrator can set /dev/dsp to be a 
symbolic link to the desired default device. The ossinfo utility can be used to list the 
available audio devices in the system."

app_rpt can also access /dev/dsp1, /dev/dsp2, according to chan_simpleusb_pvt->devicenum.

ALSA wrapper: alsa-oss (translates calls), emulation layer: osspd (emulates /dev/dsp) using ASLA/PA.

cat /sys/module/snd_pcm_oss/parameters/dsp_map indicates which ALSA PCM device is assigned to /dev/dsp.
For instance, dsp_map=0 means /dev/dsp is mapped to PCM device #0 on card #0. If you have multiple 
cards or devices, the value might be 0,1 for card 0, device 1, or similar.

/etc/modprobe.d/snd-pcm-oss # May have some information about which ALSA sound card is mapped
to /dev/dsp.

/usr/share/alsa.conf

Example of interacting with mixer: https://radutomuleasa.dev/2020-04-04-alsalib/
How simple_usbradio does it: https://github.com/AllStarLink/app_rpt/blob/fa8830dec5f899d9080e1385515c636af88a80e6/res/res_usbradio.c#L160
ALSA summary docs: https://www.volkerschatz.com/noise/alsa.html

# The directory ov vendor product IDs:
cat /var/lib/usbutils/usb.ids 

Important ALSA Commands
=======================
lsusb -t
aplay -l
ls /proc/asound
ls /dev/snd
dmesg - Shows messages of connects/disconnects
# Display the names of all controls:
amixer -c <card> controls 
# Display the vaues of all controls
amixer -c <card> contents

Permission Rules to allow normal users to access the HID device:
sudo touch /etc/udev/rules.d/99-mydevice.rules
# Put in this line:
# SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0d8c", ATTRS{idProduct}=="0012", MODE="0666", TAG+="uaccess"
sudo udevadmin control --reload-rules
sudo udevadmin trigger

*/
LineUsb::LineUsb(Log& log, Clock& clock, MessageConsumer& captureConsumer, 
    unsigned busId, unsigned callId,
    unsigned destBusId, unsigned destCallId) 
:   LineRadio(log, clock, captureConsumer, busId, callId, destBusId, destCallId) {
}

int LineUsb::open(int cardNumber, int playLevelL, int playLevelR, int captureLevel) {

    close();

    char alsaDeviceName[16];
    snprintf(alsaDeviceName, 16, "plughw:%d,0", cardNumber);

    snd_pcm_t* playH = 0;
    snd_pcm_t* captureH = 0;
    int err;

    if ((err = snd_pcm_open(&playH, alsaDeviceName, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
        _log.error("Cannot open playback device %s %d", alsaDeviceName, err);
        return -10;
    }
    // Make sure this handle gets closed if we fail during the setup process
    raiiholder<snd_pcm_t> playHolder(playH, _sndCloser);

    if ((err = snd_pcm_open(&captureH, alsaDeviceName, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
        _log.error("Cannot open capture device %s %d", alsaDeviceName, err);
        return -10;
    }
    // Make sure this handle gets closed if we fail during the setup process
    raiiholder<snd_pcm_t> captureHolder(captureH, _sndCloser);

    unsigned int audioRate = AUDIO_RATE;
    unsigned int channels = 2;

    // No free needed, alloca() frees memory one function exit
    snd_pcm_hw_params_t* play_hw_params;
    snd_pcm_hw_params_alloca(&play_hw_params);
    snd_pcm_hw_params_any(playH, play_hw_params);
    snd_pcm_hw_params_set_access(playH, play_hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playH, play_hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(playH, play_hw_params, &audioRate, 0);
    snd_pcm_hw_params_set_channels_near(playH, play_hw_params, &channels);
    // With this setting we're getting around 480 audio samples per
    // period, which leads to a good range of jitters.
    unsigned int periodTimeUs = 20000;
    // Request a max period
    snd_pcm_hw_params_set_period_time(playH, play_hw_params, periodTimeUs, 0);
    // Let the buffer store 8x 20ms frames of sound
    unsigned int bufferTimeUs = 20000 * 8;
    snd_pcm_hw_params_set_buffer_time(playH, play_hw_params, bufferTimeUs, 0);
    if ((err = snd_pcm_hw_params(playH, play_hw_params)) < 0) {
        _log.error("Play parameters %d", err);
        return -1;
    }

    // No free needed, alloca() frees memory one function exit
    snd_pcm_hw_params_t* capture_hw_params;
    snd_pcm_hw_params_alloca(&capture_hw_params);
    snd_pcm_hw_params_any(captureH, capture_hw_params);
    snd_pcm_hw_params_set_access(captureH, capture_hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(captureH, capture_hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_subformat(captureH, capture_hw_params, SND_PCM_SUBFORMAT_STD);
    // The last paramter (sub unit direction) is for near calls. Use 1 to request a rate 
    // greater than the specified value, -1 for a rate less than the value, and 0 for a 
    // rate that is exactly the value. 
    audioRate = AUDIO_RATE;
    snd_pcm_hw_params_set_rate_near(captureH, capture_hw_params, &audioRate, 0);
    //channels = 2;
    snd_pcm_hw_params_set_channels(captureH, capture_hw_params, 2);
    // With this setting we're getting around 480 audio samples per
    // period, which leads to a good range of jitters.
    periodTimeUs = 5000;
    // Request a max period
    snd_pcm_hw_params_set_period_time_max(captureH, capture_hw_params, &periodTimeUs, 0);
    // Let the buffer store 8x 20ms frames of sound
    bufferTimeUs = 20000 * 8;
    snd_pcm_hw_params_set_buffer_time(captureH, capture_hw_params, bufferTimeUs, 0);

    if ((err = snd_pcm_hw_params(captureH, capture_hw_params)) < 0) {
        _log.error("Capture parameters %d", err);
        return -1;
    }

    // Added to get the capture ball rolling
    if ((err = snd_pcm_prepare(captureH)) < 0) {
        _log.error("Cannot prepare audio interface for use (%s)", snd_strerror(err));
        return -1;
    }

    if ((err = snd_pcm_start(captureH)) < 0) {
        _log.error("Cannot start audio interface for use (%s)", snd_strerror(err));
        return -1;
    }

    _captureStartMs = _clock.time();
    _captureCount = 0;

    // Set the mixer levevels
    char alsaDeviceName2[16];
    snprintf(alsaDeviceName2, 16, "hw:%d", cardNumber);
    const char* playMixerName = "Speaker Playback Volume";
    const char* captureMixerName = "Mic Capture Volume";
    const int LEVEL_SCALE = 1000;

    // The device is queried to see what the range of volume values are.
    // Typically this will be something like "37" which represent the maximum
    // of a range of vaules from 0->37, which maps to an actual range of 
    // -37dB to 0dB. 
    //
    // The maximum value from the control is scaled by the 0->1000 level provided
    // by the caller.

    int maxPlayVolume = getMixerMax(alsaDeviceName2, playMixerName);
    int valueL = maxPlayVolume * playLevelL / LEVEL_SCALE;
    int valueR = maxPlayVolume * playLevelR / LEVEL_SCALE;
    int rc1 = setMixer(alsaDeviceName2, playMixerName, valueL, valueR);
    _log.info("Setting playback mixer level to %d/%d (max is %d)", valueL, valueR, 
        maxPlayVolume);
    if (rc1 != 0) {
        _log.error("Failed to set playback mixer level");
        return -5;
    }

    int maxCaptureVolume = getMixerMax(alsaDeviceName2, captureMixerName);
    int valueM = maxCaptureVolume * captureLevel / LEVEL_SCALE;
    int rc2 = setMixer(alsaDeviceName2, captureMixerName, valueM, valueM);
    _log.info("Setting capture mixer level to %d (max is %d)", valueM, maxCaptureVolume);
    if (rc2 != 0) {
        _log.error("Failed to set capture mixer level");
        return -5;
    }

    // At this point we are good to go
    playHolder.release();
    _playH = playH;
    captureHolder.release();
    _captureH = captureH;

    // Call up to the base for signaling
    _open();

    return 0;
}

void LineUsb::close() {
    if (_playH || _captureH) {
        _close();
        if (_playH)
            snd_pcm_close(_playH);
        _playH = 0;
        if (_captureH)
            snd_pcm_close(_captureH);
        _captureH = 0;
    }
}

int LineUsb::getPolls(pollfd* fds, unsigned fdsCapacity) {

    int used = 0, rc;    

    if (_captureH) {
        // We always want to be alerted about capture
        rc = snd_pcm_poll_descriptors(_captureH, fds + used, fdsCapacity);
        if (rc < 0) {
            _log.error("FD problem 2");
        } else {
            used += rc;
            fdsCapacity -= rc;
        }
    }

    // Alerts about playing are only needed when there is something 
    // in the accumulator that needs to be swept out.
    if (_playH && _playAccumulatorSize > 0) {
        rc = snd_pcm_poll_descriptors(_playH, fds + used, fdsCapacity);
        if (rc < 0) {
            _log.error("FD problem 3");
        } else {
            used += rc;
            fdsCapacity -= rc;
        }
    }
   
    return used;
}

bool LineUsb::run2() { 
    _captureIfPossible();
    _playIfPossible();
    return false;
}

void LineUsb::consume(const Message& frame) {
    
    if (frame.isSignal(Message::SignalType::COS_ON)) {
        _setCosStatus(true);
    } else if (frame.isSignal(Message::SignalType::COS_OFF)) {
        _setCosStatus(false);
    } else if (frame.getType() == Message::Type::AUDIO) {
        // Detect transitions from silence to playing
        if (!_playing) {
            // When re-starting after a period of silence we "stuff a frame"
            // of extra silence into the USB ALSA buffer to reduce the chances of 
            // getting behind later due to subtle timing differences.
            if (PLAY_ACCUMULATOR_CAPACITY - _playAccumulatorSize >= BLOCK_SIZE_48K) {
                // Move new audio block into the play accumulator 
                memset(&(_playAccumulator[_playAccumulatorSize]), 0,
                    BLOCK_SIZE_48K * sizeof(int16_t));
                _playAccumulatorSize += BLOCK_SIZE_48K;
            }
        }
    }

    // Then go through the normal consume process
    LineRadio::consume(frame);
}

// ===== Capture Related =========================================================

// On startup the capture card is in STATE_PREPARED
// 
void LineUsb::_captureIfPossible() {  
   
    if (!_captureH)
        return;

    bool audioCaptureEnabled = (_cosActive && _ctcssActive) || _toneActive;

    // Attempt to read inbound (captured) audio data. Whenever a full
    // block is accumulated it can be returned to the outside.
    //
    // NOTE: We never read more than one block of data. Anything we 
    // don't need will be accumulated for the next time.
    //
    const int usbBufferSize = BLOCK_SIZE_48K * 2 * 2;
    uint8_t usbBuffer[usbBufferSize];
    // TODO: rename to framesRead
    int samplesRead = snd_pcm_readi(_captureH, usbBuffer, BLOCK_SIZE_48K);
    if (samplesRead > 0) {

        assert((unsigned)samplesRead <= BLOCK_SIZE_48K);

        // Do we have a full audio block available yet?
        if (_captureAccumulatorSize + samplesRead >= BLOCK_SIZE_48K) {

            uint32_t nowMs = _clock.time();
            uint32_t idealNowMs = _captureStartMs + (_captureCount * BLOCK_PERIOD_MS);
            _captureCount++;
           
            // Form a complete block of mono 16-bit PCM by joining what
            // we had already accumulated previously with the new samples 
            // we just received.
            int16_t pcm48k_1[BLOCK_SIZE_48K];

            // The first part of the complete block comes from the accumulator. 
            // The accumulator will be emptied by this process.
            assert(_captureAccumulatorSize <= BLOCK_SIZE_48K);
            unsigned usedFromAccumulator = _captureAccumulatorSize;
            for (unsigned i = 0; i < _captureAccumulatorSize; i++)
                pcm48k_1[i] = _captureAccumulator[i];
            _captureAccumulatorSize = 0;

            // The second part of the complete block comes from what we just captured.
            const uint8_t* srcPtr = usbBuffer;
            for (unsigned i = usedFromAccumulator; i < BLOCK_SIZE_48K; i++, srcPtr += 4)
                // Left side audio only
                pcm48k_1[i] = unpack_int16_le(srcPtr);

            // Finally, put anything extra that we just captured back
            // into the accumulator for use next time.
            //
            // For example (sanity check):
            // BLOCK_SIZE_48K                   = 960
            // usedFromAccumulator (previous)   = 900
            // samplesRead (this time)          = 200, and will never be >960
            // extra                            = 200 - (960 - 900) = 140
            //
            // NOTE: We will never get here if the amount captured was not enough
            // to form a complete block, see enclosing if statement.
            assert((unsigned)samplesRead >= (BLOCK_SIZE_48K - usedFromAccumulator));
            unsigned extra = samplesRead - (BLOCK_SIZE_48K - usedFromAccumulator);
            for (unsigned i = 0; i < extra; i++, srcPtr += 4)
                // Left side audio only
                _captureAccumulator[_captureAccumulatorSize++] = unpack_int16_le(srcPtr);

            // The processing steps are only done if capture is enabled, otherwise
            // the new audio data is just dropped.
            if (audioCaptureEnabled) {

                // Transition detect, the beginning of a capture "run"
                if (!_capturing) {
                    _capturing = true;
                    // Force a synchronization of the actual system clock and 
                    // the timestamps that will be put on the generated frames.
                    _captureStartMs = nowMs;
                    _captureCount = 0;
                    idealNowMs = nowMs;
                    _captureStart();
                }
                _lastCapturedFrameMs = _clock.time();
                
                // Here is where statistics and possibly recording happens
                _analyzeCapturedAudio(pcm48k_1, BLOCK_SIZE_48K);

                // Here is where the actual processing of the new block happens
                _processCapturedAudio(pcm48k_1, BLOCK_SIZE_48K, nowMs, idealNowMs);
            }
        }
        // If we don't have a complete block yet then just keep storing
        // the captured audio in the accumulator.
        else {
            const uint8_t* srcPtr = usbBuffer;
            assert(samplesRead + _captureAccumulatorSize <= BLOCK_SIZE_48K);
            for (unsigned i = 0; i < (unsigned)samplesRead; i++, srcPtr += 4)
                _captureAccumulator[_captureAccumulatorSize++] = unpack_int16_le(srcPtr);
        }
    }   
    // This case is known to happen during startup
    // "Resource temporarily unavailable"
    else if (samplesRead == -11) {
        snd_pcm_recover(_captureH, samplesRead, 0); 
        _captureErrorCount++;
    } 
    else if (samplesRead < 0) {
        _log.error("Audio capture error %s", snd_strerror(samplesRead));
        snd_pcm_recover(_captureH, samplesRead, 0); 
        _captureErrorCount++;
    }
}

// ===== Play Related =========================================================

/**
 * This will be called by the base class after all decoding has happened.
 */
void LineUsb::_playPCM48k(int16_t* pcm48k_2, unsigned blockSize) {

    // Check to make sure we actually have room in the accumulator
    if (_playAccumulatorSize + BLOCK_SIZE_48K > PLAY_ACCUMULATOR_CAPACITY) {
        _log.error("Play accumulator is full, lost a frame");
    }
    else {
        // Move new audio block into the play accumulator 
        memcpy(&(_playAccumulator[_playAccumulatorSize]), pcm48k_2,
            BLOCK_SIZE_48K * sizeof(int16_t));
        _playAccumulatorSize += BLOCK_SIZE_48K;
    }

    _playFrameCount++;

    // Try to clear the buffer
    _playIfPossible();
}

void LineUsb::_playIfPossible() {

    if (!_playH)
        return;

    if (_playAccumulatorSize == 0)
        return;

    // Look at the status of the PCM, might be useful for underrun?
    snd_pcm_status_t *status;
    snd_pcm_status_alloca(&status);
    snd_pcm_status(_playH, status);

    // Add the other stereo channel (interleaved) and convert to S16_LE.
    const int usbBufferSize = PLAY_ACCUMULATOR_CAPACITY * 2 * 2;
    uint8_t usbBuffer[usbBufferSize];
    uint8_t* p2 = usbBuffer;
    for (unsigned i = 0; i < _playAccumulatorSize; i++, p2 += 4) {
        // Left
        pack_int16_le(_playAccumulator[i], p2);
        // Right
        pack_int16_le(_playAccumulator[i], p2 + 2);
    }

    // This is a loop to allow an attempt to recover after an underrun
    for (unsigned i = 0; i < 2; i++) {
        // Here is where we send the audio to the hardware
        int rc = snd_pcm_writei(_playH, usbBuffer, _playAccumulatorSize);
        if (rc < 0) {
            if (rc == -EPIPE) {
                // This isn't really an error, it's just the sound card telling
                // us that it was underrun prior to us starting to stream again.
                // We recover the card and then wait for the polling loop to 
                // come back to get things rolling with a re-write.
                _underrunCount++;
                // We expect an underrun at the very beginning of a talkspurt
                // so there is a flag to supress the message
                snd_pcm_recover(_playH, rc, 1); 
            } else if (rc == -11) {
                _log.info("Write full");
                // This is the case that the card can't accept anything
                break;
            } else {
                _log.error("Write failed %d", rc);
                snd_pcm_recover(_playH, rc, 1); 
                break;
            }
        } else if (rc > 0) {
            if ((unsigned)rc == _playAccumulatorSize) {
                _playAccumulatorSize = 0;
            } else {
                // Shift left to get rid of the frames that were written 
                memmove(_playAccumulator, &(_playAccumulator[rc]),
                    (_playAccumulatorSize - rc) * sizeof(int16_t));
                _playAccumulatorSize -= rc;
            }
            break;
        }
    }
}

}
