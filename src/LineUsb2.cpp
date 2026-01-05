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
#include <kc1fsz-tools/raiiholder.h>

#include "LineUsb.h"

using namespace std;

namespace kc1fsz {

// NOTE: This function borrows heavily from the app_rpt implementation.
// The license from the relevant file is:
//
// Copyright (C) 2023, Naveen Albert
// Based upon previous code by:
// Jim Dixon, WB6NIL <jim@lambdatel.com>
// Steve Henke, W9SH  <w9sh@arrl.net>
//
// This program is free software, distributed under the terms of
// the GNU General Public License Version 2. See the LICENSE file
// at the top of the source tree.

int setMixer(const char* deviceName, const char *paramName, int v1, int v2) {

	snd_hctl_t *hctl;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_value_t *control;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *info;

	if (snd_hctl_open(&hctl, deviceName, 0)) {
        cout << "open failed" << endl;
		return -1;
    }
    // Make sure the handle gets cleaned up no matter how we go out of scope
    raiiholder<snd_hctl_t> holder(hctl, [](snd_hctl_t* o) { snd_hctl_close(o); });

	snd_hctl_load(hctl);
	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, paramName);
	elem = snd_hctl_find_elem(hctl, id);
	if (!elem) {
        cout << "Find element failed" << endl;
        return -1;
    }
	snd_ctl_elem_info_alloca(&info);
	snd_hctl_elem_info(elem, info);
    int dataType = snd_ctl_elem_info_get_type(info);
	snd_ctl_elem_value_alloca(&control);
	snd_ctl_elem_value_set_id(control, id);
	switch (dataType) {
        case SND_CTL_ELEM_TYPE_INTEGER:
            snd_ctl_elem_value_set_integer(control, 0, v1);
            if (v2 > 0) {
                snd_ctl_elem_value_set_integer(control, 1, v2);
            }
            break;
        case SND_CTL_ELEM_TYPE_BOOLEAN:
            snd_ctl_elem_value_set_integer(control, 0, (v1 != 0));
            break;
	}
	if (snd_hctl_elem_write(elem, control)) {
        cout << "Write element failed" << endl;
        return -1;
    }
	return 0;
}

int getMixerMax(const char* deviceName, const char* paramName) {

	snd_hctl_t *hctl;
	if (snd_hctl_open(&hctl, deviceName, 0))
		return -1;
    // Make sure the handle gets cleaned up no matter how we go out of scope
    raiiholder<snd_hctl_t> holder(hctl, [](snd_hctl_t* o) { snd_hctl_close(o); });

	snd_hctl_load(hctl);
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, paramName);
	snd_hctl_elem_t *elem = snd_hctl_find_elem(hctl, id);
	if (!elem)
		return -1;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_info_alloca(&info);
	snd_hctl_elem_info(elem, info);
	// At the moment we only support integer values
	int type = snd_ctl_elem_info_get_type(info);
	if (type == SND_CTL_ELEM_TYPE_INTEGER)
		return snd_ctl_elem_info_get_max(info);
	else 
		return -1;
}

}
