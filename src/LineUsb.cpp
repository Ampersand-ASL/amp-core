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

An article on USB timing:

https://audiophilestyle.com/ca/bits-and-bytes/Asynchronicity-USB-Audio-Primer/

There are two options: Adapative and Asynchronous. The CM108 uses Adaptive, which is the most
common mode.

"In Adaptive mode the computer controls the audio transfer rate, and the USB device has to follow 
along updating the Master Clock (MCLK) every one millisecond. The USB bus runs at 12MHz, which is 
unrelated to the audio sample rate of any digital audio format (i.e. 44.1K requires a MCLK = 11.2896MHz). 
Therefore Adaptive Mode USB DACs must derive the critical master audio clock by use of a complex 
Frequency Synthesizer. Since the computer is handling many tasks at once, the timing of the USB audio 
transfers has variations. This leads to jitter in the derived clock." Says Wavelength Audio's Gordon 
Rankin.

vs:

"Asynchronous USB essentially turns the computer into a slave device as opposed to adaptive USB which 
does the opposite. Thus, an asynchronous USB DAC has total control over the timing of the audio"


An article on timing for full-duplex:

https://nyanpasu64.gitlab.io/blog/low-latency-audio-output-duplex-alsa/

A reference: https://www.volkerschatz.com/noise/alsa.html
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

References
==========
* Example of interacting with mixer: https://radutomuleasa.dev/2020-04-04-alsalib/
* How simple_usbradio does it: https://github.com/AllStarLink/app_rpt/blob/fa8830dec5f899d9080e1385515c636af88a80e6/res/res_usbradio.c#L160
* ALSA summary docs: https://www.volkerschatz.com/noise/alsa.html
* A good discussion on setting play/capture levels (dB vs 0-1000): https://github.com/AllStarLink/app_rpt/pull/411

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

Location of libsound.so.2.0.0, used to check version of binary:

    /usr/lib/aarch64-linux-gnu/

Command to get ALSA driver version:

    cat /proc/asound/version

Command to get details about the USB Audio driver:

    sudo modinfo snd-usb-audio
*/
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <cmath>
#include <cassert>
#include <algorithm>
#include <cstring>

#include <kc1fsz-tools/Log.h>
#include <kc1fsz-tools/raiiholder.h>

#include "MessageConsumer.h"
#include "LineUsb.h"

// Per David NR9V, this is a good setting (one 20ms frame)
#define USB_PLAY_PERIOD_SIZE_FRAMES (960)
// Per David NR9V, 4 is a good setting.
// 17-May-2026, Bruce changed to much larger to see if it reduces the overrun problem.
#define USB_PLAY_BUFFER_SIZE_FRAMES (960 * 16)
// This sets the initial delay between the arrival of audio frames and the playout of 
// audio frames. A smaller number means less latency, but a greater possibility of 
// underruns.
#define USB_PLAY_START_THRESHOLD_FRAMES (960 * 2)
// How many play/capture errors can happen before essentially rebooting the line
#define ERROR_COUNT_THRESHOLD (5)

using namespace std;

namespace kc1fsz {

LineUsb::LineUsb(Log& log, Clock& clock, MessageConsumer& captureConsumer, 
    unsigned busId, unsigned callId,
    unsigned destBusId, unsigned destCallId, unsigned signalDestLineId, 
    unsigned networkDestLineId) 
:   LineRadio(log, clock, captureConsumer, busId, callId, destBusId, destCallId,
        signalDestLineId, networkDestLineId) {
}

int LineUsb::open(int cardNumber, int playLevelL, int playLevelR, int captureLevel, 
    bool echo, float echoGainDb) {

    // Capture all of the parameters of the open so that we can open and re-open 
    // if necessary.
    _openCardNumber = cardNumber;
    _openPlayLevelL = playLevelL;
    _openPlayLevelR = playLevelR;
    _openCaptureLevel = captureLevel;
    _openEcho = echo;
    _openEchoGainDb = echoGainDb;
    _openRequested = true;

    return _open();
}

void LineUsb::close() {
    _openRequested = false;
    _close();
}

int LineUsb::_open() {

    // Always clear up existing state first
    if (_isOpen)
        _close();

    // In ALSA (Advanced Linux Sound Architecture), plughw is a virtual plugin device 
    // that acts as an abstraction layer over raw hardware (hw). It automatically handles 
    // audio format conversions—such as sample rate (e.g., 44.1kHz to 48kHz, channel 
    // mapping (e.g., mono to stereo), and bit depth (e.g., 16-bit to 32-bit) if the 
    // hardware does not support the format directly.
    char alsaDeviceName[16];
    // Using hardware directly
    snprintf(alsaDeviceName, 16, "hw:%d,0", _openCardNumber);
    char alsaDeviceNameC[16];
    // #### TODO: For reasons that I don't fully understand, the capture device needs 
    // to go through the plug plugin. Possibly due to some format/rate translation?
    // If I use the hardware directly the audio is distorted.
    snprintf(alsaDeviceNameC, 16, "plughw:%d,0", _openCardNumber);
    char alsaDeviceName2[16];
    // Using hardware directly for mixer setting
    snprintf(alsaDeviceName2, 16, "hw:%d", _openCardNumber);

    snd_pcm_t* playH = 0;
    snd_pcm_t* captureH = 0;
    int err;

    if ((err = snd_pcm_open(&playH, alsaDeviceName, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
        if (err == -16) {
            _log.error("Can't open sound device %s, busy", alsaDeviceName);
            return -12;
        } else {
            _log.error("Cannot open playback device %s %d", alsaDeviceName, err);
            return -10;
        }
    }
    // Make sure this handle gets closed if we fail during the setup process
    raiiholder<snd_pcm_t> playHolder(playH, _sndCloser);

    if ((err = snd_pcm_open(&captureH, alsaDeviceNameC, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
        _log.error("Cannot open capture device %s %d", alsaDeviceName, err);
        return -10;
    }
    // Make sure this handle gets closed if we fail during the setup process
    raiiholder<snd_pcm_t> captureHolder(captureH, _sndCloser);

    const unsigned int audioRate = AUDIO_RATE;
    const unsigned int channels = 2;

    // No free needed, alloca() frees memory one function exit
    snd_pcm_hw_params_t* play_hw_params;
    snd_pcm_hw_params_alloca(&play_hw_params);
    snd_pcm_hw_params_any(playH, play_hw_params);
    snd_pcm_hw_params_set_access(playH, play_hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playH, play_hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(playH, play_hw_params, audioRate, 0);
    snd_pcm_hw_params_set_channels(playH, play_hw_params, channels);
    snd_pcm_uframes_t periodFrames = USB_PLAY_PERIOD_SIZE_FRAMES;
    snd_pcm_hw_params_set_period_size_near(playH, play_hw_params, &periodFrames, 0);
    // Let the buffer store 16x 20ms frames of sound. 
    // At 48K, there are 960 samples in a 20ms frame.
    snd_pcm_uframes_t bufferFrames = USB_PLAY_BUFFER_SIZE_FRAMES;
    snd_pcm_hw_params_set_buffer_size_near(playH, play_hw_params, &bufferFrames);

    if ((err = snd_pcm_hw_params(playH, play_hw_params)) < 0) {
        _log.error("Play parameters %d", err);
        return -1;
    }

    // ALSA software parameters
    snd_pcm_sw_params_t* play_sw_params;
    snd_pcm_sw_params_alloca(&play_sw_params);
    snd_pcm_sw_params_current(playH, play_sw_params);
    const snd_pcm_uframes_t startThreshold = USB_PLAY_START_THRESHOLD_FRAMES;
    snd_pcm_sw_params_set_start_threshold(playH, play_sw_params, startThreshold);
    //const snd_pcm_uframes_t stopThreshold = USB_PLAY_BUFFER_SIZE_FRAMES - USB_PLAY_PERIOD_SIZE_FRAMES;
    //snd_pcm_sw_params_set_stop_threshold(playH, play_sw_params, stopThreshold);


    if ((err = snd_pcm_sw_params(playH, play_sw_params)) < 0) {
        _log.error("Unable to configure play SW parameters %d", err);
        return -1;
    }

    snd_pcm_uframes_t t0 = 0;
    snd_pcm_sw_params_get_start_threshold(play_sw_params, &t0);
    _log.info("Start threshold %d (frames)", t0);

    snd_pcm_uframes_t t1 = 0;
    snd_pcm_sw_params_get_stop_threshold(play_sw_params, &t1);
    _log.info("Stop threshold %d (frames)", t1);

    // No free needed, alloca() frees memory one function exit
    snd_pcm_hw_params_t* capture_hw_params;
    snd_pcm_hw_params_alloca(&capture_hw_params);
    snd_pcm_hw_params_any(captureH, capture_hw_params);
    snd_pcm_hw_params_set_access(captureH, capture_hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(captureH, capture_hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_subformat(captureH, capture_hw_params, SND_PCM_SUBFORMAT_STD);
    // The last parameter (sub unit direction) is for near calls. Use 1 to request a rate 
    // greater than the specified value, -1 for a rate less than the value, and 0 for a 
    // rate that is exactly the value. 
    snd_pcm_hw_params_set_rate(captureH, capture_hw_params, audioRate, 0);
    //channels = 2;
    snd_pcm_hw_params_set_channels(captureH, capture_hw_params, 2);
    // With this setting we're getting around 480 audio samples per
    // period, which leads to a good range of jitters.
    unsigned periodTimeUs = 5000;
    // Request a max period
    snd_pcm_hw_params_set_period_time_max(captureH, capture_hw_params, &periodTimeUs, 0);
    // Let the buffer store 8x 20ms frames of sound
    unsigned bufferTimeUs = 20000 * 8;
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

    // Set the mixer levevels
    const char* playMixerName = "Speaker Playback Volume";

    int valueL, valueR;
    int rc6 = convertMixerDbToValue(alsaDeviceName2, playMixerName, _openPlayLevelL, &valueL);
    if (rc6 != 0) {
        _log.error("Failed to convert mixer play value from %d dB", _openPlayLevelL);
        return -6;
    }
    int rc7 = convertMixerDbToValue(alsaDeviceName2, playMixerName, _openPlayLevelR, &valueR);
    if (rc7 != 0) {
        _log.error("Failed to convert mixer play value from %d dB", _openPlayLevelR);
        return -7;
    }
    _log.info("Setting playback mixer level to %d/%d dB (%d/%d)", 
        _openPlayLevelL, _openPlayLevelR, valueL, valueR);
    int rc1 = setMixer2(alsaDeviceName2, playMixerName, valueL, valueR);
    if (rc1 != 0) {
        _log.error("Failed to set playback mixer level");
        return -8;
    }

    const char* captureMixerName = "Mic Capture Volume";

    int valueM;
    int rc8 = convertMixerDbToValue(alsaDeviceName2, captureMixerName, _openCaptureLevel, &valueM);
    if (rc8 != 0) {
        _log.error("Failed to convert mixer capture value from %d dB", _openCaptureLevel);
        return -9;
    }
    _log.info("Setting capture mixer level to %d dB (%d)", _openCaptureLevel, valueM);
    int rc2 = setMixer1(alsaDeviceName2, captureMixerName, valueM);
    if (rc2 != 0) {
        _log.error("Failed to set capture mixer level");
        return -10;
    }

    // Since we're doing non-blocking I/O, pull out the pollfds that will 
    // be monitored to see when hardware activity is pending.
    int rc;
    rc = snd_pcm_poll_descriptors(playH, _playFds, MAX_POLL_FDS);
    if (rc < 0) {
        _log.error("FD problem 2");
        return -11;
    } 
    _playFdCount = rc;

    rc = snd_pcm_poll_descriptors(captureH, _captureFds, MAX_POLL_FDS);
    if (rc < 0) {
        _log.error("FD problem 3");
        return -12;
    } 
    _captureFdCount = rc;

    // At this point we are good to go
    playHolder.release();
    _playH = playH;
    captureHolder.release();
    _captureH = captureH;
    _captureStartMs = _clock.time();
    _captureCount = 0;
    _captureErrorCount = 0;
    _playErrorCount = 0; 
    _playAccumulatorSize = 0;
    _underrunCount = 0;
   
    // Call up to the base for signaling
    _signalOpen(_openEcho, _openEchoGainDb);

    _isOpen = true;
    _fatalError = false;
    _fastRecoveryAttempted = false;
    _lastWriteMs = 0;
    _lastWriteOverrunMs = 0;

    return 0;
}

void LineUsb::_close() {

    _signalClose();

    _isOpen = false;
    _playFdCount = 0;
    _captureFdCount = 0;

    if (_playH)
        snd_pcm_close(_playH);
    _playH = 0;
    if (_captureH)
        snd_pcm_close(_captureH);
    _captureH = 0;
}

int LineUsb::getPolls(pollfd* fds, unsigned fdsCapacity) {

    int used = 0;    

    if (_isOpen) {
        // We always want to be alerted about capture activity
        for (unsigned i = 0; i < _captureFdCount && fdsCapacity > 0; i++) {
            fds[used++] = _captureFds[i];
            fdsCapacity--;
        }

        // Alerts about playing are only needed when there is something 
        // in the accumulator that needs to be swept out.
        if (_playAccumulatorSize > 0) {
            for (unsigned i = 0; i < _playFdCount && fdsCapacity > 0; i++) {
                fds[used++] = _playFds[i];
                fdsCapacity--;
            }
        }
    } 
    
    return used;
}

bool LineUsb::run2() { 

    _captureIfPossible();

    // Only attempt to write more audio every 10ms, unless this is the first
    // pass after an underrun
    if (_clock.isPastWindow(_lastWriteMs, 10) || _underrunCount == 1)
        _playIfPossible();

    // Per suggestion from David NR9V, if the errors start to accumulate then trigger 
    // a preemptive close/re-open of the system. 
    if (_captureErrorCount > ERROR_COUNT_THRESHOLD || 
        _playErrorCount > ERROR_COUNT_THRESHOLD) 
        _fatalError = true;

    return false;
}

void LineUsb::oneSecTick() {
    
    LineRadio::oneSecTick();

    // Check to see if an automatic reset of the line is required
    if (_fatalError) {
        _log.info("LineUsb error reported, attempting to re-open interface");
        _open();
    }
}

void LineUsb::audioRateTick(uint32_t tickMs) {
    
    // Let the base class do its thing
    LineRadio::audioRateTick(tickMs);

    // Check to see if an automatic reset of the line is required
    if (_fatalError && !_fastRecoveryAttempted) {
        _log.info("LineUsb error reported, attempting to re-open interface");
        _open();
        _fastRecoveryAttempted = true;
    }
}

// ===== Capture Related =========================================================

void LineUsb::_captureIfPossible() {  
   
    if (!_isOpen || _fatalError)
        return;

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

            _processCapturedAudio(pcm48k_1, BLOCK_SIZE_48K);
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
    } 
    else if (samplesRead == -ENODEV) {
        _log.error("Capture device not found");
        _captureErrorCount++;
    }
    else if (samplesRead < 0) {
        _log.error("Audio capture error %s", snd_strerror(samplesRead));
        snd_pcm_recover(_captureH, samplesRead, 0); 
        _captureErrorCount++;
    }
}

// ===== Play Related =========================================================

// This will be called by the base class to indicate that a talkspurt has 
// reached its end.
void LineUsb::_playSpurtEnd() {
    _tsRunning = false;
    // Call up to let the base class do its thing as well
    LineRadio::_playSpurtEnd();
}

// This will be called by the base class after all decoding has happened.
LineRadio::PlayStatus LineUsb::_playPCM48k(int16_t* pcm48k_2, unsigned blockSize) {

    assert(blockSize == BLOCK_SIZE_48K);

    if (!_isOpen || _fatalError)
        return PlayStatus::STATUS_ERROR;

    // Check to make sure we actually have room in the accumulator
    if (_playAccumulatorSize + BLOCK_SIZE_48K > PLAY_ACCUMULATOR_CAPACITY)
        return PlayStatus::STATUS_FULL;

    // Move new audio block into the play accumulator. This accumulator is necessary
    // because there is no certainty that the audio hardware will accept data at 
    // the same pace as it is being delivered in this function. We accumulate play
    // audio and then pass it to the hardware in what might turn out to be multiple chunks.
    memcpy(&(_playAccumulator[_playAccumulatorSize]), pcm48k_2,
        BLOCK_SIZE_48K * sizeof(int16_t));
    _playAccumulatorSize += BLOCK_SIZE_48K;

    // Immediately try to clear the _playAccumulator into the hardware.
    _playIfPossible();

    // At this point at least the frame has been accepted into the _playAccumulator.
    // As far as the base class is concerned we are done.
    return PlayStatus::STATUS_OK;
}

// This is called from within the event loop to try to make progress on any 
// frames that are hanging in the play accumulator.
void LineUsb::_playIfPossible() {

    if (!_isOpen || _fatalError || _playAccumulatorSize == 0)
        return;

    // After an overrun create a delay to try to recover
    if (!_clock.isPastWindow(_lastWriteOverrunMs, 500)) {
        _log.info("In overrun recovery delay");
        return;
    }

    // Look at the status of the PCM
    //snd_pcm_status_t *status;
    //snd_pcm_status_alloca(&status);
    //snd_pcm_status(_playH, status);
    // Delay is distance between current application frame position and sound frame position. 
    // It's positive and less than buffer size in normal situation, negative on playback underrun 
    // and greater than buffer size on capture overrun.
    //unsigned delayFrames = snd_pcm_status_get_delay(status);

    //if (delayFrames != _lastDelayFrames) {
        //_log.info("Delay %u frames", delayFrames);
    //    _lastDelayFrames = delayFrames;
    //}

    // Build a buffer that is at most one period in length.
    // There is no guarantee how much of this buffer will actually be accepted by 
    // the USB driver.
    uint8_t usbBuffer[USB_PLAY_PERIOD_SIZE_FRAMES * 2 * sizeof(int16_t)];
    uint8_t* p2 = usbBuffer;
    unsigned writeFrames = std::min((unsigned)USB_PLAY_PERIOD_SIZE_FRAMES, 
        _playAccumulatorSize);

    // Add the other stereo channel (interleaved) and convert to S16_LE.
    for (unsigned i = 0; i < writeFrames; i++, p2 += 4) {
        // Left
        pack_int16_le(_playAccumulator[i], p2);
        // Right
        pack_int16_le(_playAccumulator[i], p2 + 2);
    }

    // Here is where we send the audio to the hardware. We attempt to write 
    // everything in the accumulator, knowing that THE HARDWARE MIGHT NOT ACCEPT
    // ALL OF IT.
    _lastWriteMs = _clock.timeMs();
    int rc = snd_pcm_writei(_playH, usbBuffer, writeFrames);
    if (rc < 0) {
        if (rc == -EPIPE) {
            // We will likely encounter the PCM in an underrun state after the previous talkspurt
            // has ended and there is nothing left to play. It is possible that could also result 
            // from the data falling behind the audio frame rate. Either way, we re-prepare the PCM 
            // for a new stream of audio.
            //
            // This isn't really an error, it's just the sound card telling
            // us that it was underrun prior to us starting to stream again.
            // We recover the card and then wait for the polling loop to 
            // come back to get things rolling with a re-write.
            // We expect an underrun at the very beginning of a talkspurt
            // so there is a flag to supress the message in that case.
            // This is only concerning if it happens in the midst of a talkspurt
            //if (_tsRunning)
            _log.info("snd_pcm_writei underrun");
            int rc2 = snd_pcm_recover(_playH, rc, 1);
            if (rc2 < 0) {
                _log.info("Underrun recovery failed %d", rc2);
            } else {
                snd_pcm_start(_playH);
            }
            _underrunCount++;
        } else if (rc == -11) {
            // This is the case that the card can't accept anything more. This
            // really shouldn't happen given how much space is available in the 
            // play buffer. However, David NR9V demonstrated an Arduino Uno Q
            // that would randomly stop consuming audio, causing the play buffers
            // to fill up to 100%.
            _log.error("snd_pcm_writei overrun");
            int rc2 = snd_pcm_recover(_playH, rc, 1); 
            if (rc2 < 0) {
                _log.info("Overrun recovery failed %d", rc2);
                _playErrorCount++;
            } else {
                snd_pcm_start(_playH);
            }
            _underrunCount = 0;
            _lastWriteOverrunMs = _clock.timeMs();
        } else {
            // All other errors are unknown/serious. For example, the USB plug 
            // being pulled out.
            snd_pcm_recover(_playH, rc, 1); 
            _playErrorCount++;
            _log.error("snd_pcm_writei failed %d", rc);
            _underrunCount = 0;
        }
    } 
    else if (rc > 0) {     
        // This flag gets set as soon as a write was successful (i.e. streaming
        // is underway)
        _tsRunning = true;
        // Shift left to get rid of the frames that were accepted into the driver
        if ((unsigned)rc < _playAccumulatorSize)
            memmove(_playAccumulator, &(_playAccumulator[rc]),
                (_playAccumulatorSize - rc) * sizeof(int16_t));
        _playAccumulatorSize -= rc;
        _underrunCount = 0;
    }
    else {
        _log.info("snd_pcm_writei 0");
        _underrunCount = 0;
    }
}

}
