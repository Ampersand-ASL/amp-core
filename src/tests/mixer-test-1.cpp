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

// Interesting, this seems to make a difference in jitter behavior.
// The hw:0 has less jitter.
//static const char* alsaDeviceName = "default";
static const char* alsaDeviceName = "hw:0";
//static const char* selem_name = "Master";
static const char* selemName = "Speaker";

int main(int, const char**) {

    long volume = 10;
    long min, max;
    int rc =0;

    snd_mixer_t* _mixerH = 0;
    rc = snd_mixer_open(&_mixerH, 0);
    if (rc < 0) {
        printf("Unable to open mixer\n");
        return -1;
    }
    snd_mixer_attach(_mixerH, alsaDeviceName);
    snd_mixer_selem_register(_mixerH, NULL, NULL);
    rc = snd_mixer_load(_mixerH);
    if (rc < 0) {
        printf("Unable to load mixer\n");
        return -1;
    }

    // No free needed
    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, selemName);
    snd_mixer_elem_t* elem = snd_mixer_find_selem(_mixerH, sid);
    if (elem == 0) {
        printf("Unable to get mixer element\n");
        return -1;
    }

    while (true) {
        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        snd_mixer_selem_set_playback_volume_all(elem, volume * max / 100);
        sleep(1);
    }

    snd_mixer_close(_mixerH);

    return 0;
}
