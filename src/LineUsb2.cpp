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

int setMixer(const char* deviceName, const char *paramName, unsigned count, int v1, int v2) {

	snd_hctl_t *hctl;
	snd_hctl_elem_t *elem;

	if (snd_hctl_open(&hctl, deviceName, 0)) {
        cout << "open failed" << endl;
		return -1;
    }
    // Make sure the handle gets cleaned up no matter how we go out of scope
    raiiholder<snd_hctl_t> holder(hctl, [](snd_hctl_t* o) { snd_hctl_close(o); });

	snd_hctl_load(hctl);
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_id_malloc(&id);
	// Make sure this handle isn't leaked
    raiiholder<snd_ctl_elem_id_t> id_holder(id, 
		[](snd_ctl_elem_id_t* o) { snd_ctl_elem_id_free(o); });

	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, paramName);
	elem = snd_hctl_find_elem(hctl, id);
	if (!elem)
        return -1;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_info_malloc(&info);
	// Make sure this handle isn't leaked
    raiiholder<snd_ctl_elem_info_t> info_holder(info, 
		[](snd_ctl_elem_info_t* o) { snd_ctl_elem_info_free(o); });

	snd_hctl_elem_info(elem, info);
    int dataType = snd_ctl_elem_info_get_type(info);
	snd_ctl_elem_value_t *control;
	snd_ctl_elem_value_malloc(&control);
	// Make sure this handle isn't leaked
    raiiholder<snd_ctl_elem_value_t> control_holder(control, 
		[](snd_ctl_elem_value_t* o) { snd_ctl_elem_value_free(o); });
	snd_ctl_elem_value_set_id(control, id);
	switch (dataType) {
        case SND_CTL_ELEM_TYPE_INTEGER:
            snd_ctl_elem_value_set_integer(control, 0, v1);
			if (count == 2) {
                snd_ctl_elem_value_set_integer(control, 1, v2);
            }
            break;
        case SND_CTL_ELEM_TYPE_BOOLEAN:
            snd_ctl_elem_value_set_integer(control, 0, (v1 != 0));
			if (count == 2) {
	            snd_ctl_elem_value_set_integer(control, 1, (v2 != 0));
			}
            break;
	}
	if (snd_hctl_elem_write(elem, control))
        return -2;
	return 0;
}

int setMixer1(const char* deviceName, const char *paramName, int v) {
	return setMixer(deviceName, paramName, 1, v, 0);
}

int setMixer2(const char* deviceName, const char *paramName, int v1, int v2) {
	return setMixer(deviceName, paramName, 2, v1, v2);
}

/**
 * Common sub-function that gets us to the element info point.
 */
int withMixerElementInfo(const char* deviceName, const char* paramName, 
	std::function<int(snd_hctl_elem_t*, snd_ctl_elem_info_t*)> cb) {

		snd_hctl_t *hctl;
	if (snd_hctl_open(&hctl, deviceName, 0))
		return -1;
    // Make sure the handle gets cleaned up no matter how we go out of scope
    raiiholder<snd_hctl_t> holder(hctl, [](snd_hctl_t* o) { snd_hctl_close(o); });

	snd_hctl_load(hctl);
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_id_malloc(&id);
	// Make sure this handle isn't leaked
    raiiholder<snd_ctl_elem_id_t> id_holder(id, 
		[](snd_ctl_elem_id_t* o) { snd_ctl_elem_id_free(o); });

	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, paramName);
	snd_hctl_elem_t* elem = snd_hctl_find_elem(hctl, id);
	if (!elem)
		return -2;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_info_malloc(&info);
	// Make sure this handle isn't leaked
    raiiholder<snd_ctl_elem_info_t> info_holder(info, 
		[](snd_ctl_elem_info_t* o) { snd_ctl_elem_info_free(o); });
	snd_hctl_elem_info(elem, info);

	return cb(elem, info);
}

int getMixerRange(const char* deviceName, const char* paramName, int* minV, int* maxV) {
	return withMixerElementInfo(deviceName, paramName, 
		[minV, maxV](snd_hctl_elem_t*, snd_ctl_elem_info_t* info) {
			// At the moment we only support integer values
			int type = snd_ctl_elem_info_get_type(info);
			if (type == SND_CTL_ELEM_TYPE_INTEGER) {
				*minV = snd_ctl_elem_info_get_min(info);
				*maxV = snd_ctl_elem_info_get_max(info);
				return 0;
			} else {
				return -3;
			}
		}
	);
}

int convertMixerValueToDb(const char* deviceName, const char* paramName, int value, int* db) {
    return withMixerElementInfo(deviceName, paramName, 
		[value, db](snd_hctl_elem_t* elem, snd_ctl_elem_info_t* info) {
		// At the moment we only support integer values
		int type = snd_ctl_elem_info_get_type(info);
		if (type == SND_CTL_ELEM_TYPE_INTEGER) {
			const unsigned int tlvSize = 32;
			unsigned int tlv[tlvSize];
			int rc2 = snd_hctl_elem_tlv_read(elem, tlv, tlvSize);
			if (rc2 != 0)
				return -2;
			unsigned int* dbTlv = 0;
			// NOTE: It's possible that this call will return a negative 
			// value to indicate a problem. 
			//
			// There is no discussion in the documentation about releasing
			// the pointer that we get back in dbTlv.
			int tlvInfoSize = snd_tlv_parse_dB_info(tlv, 32, &dbTlv);
			if (tlvInfoSize > 0) {                
				long dbGain = 0;
				int rc3 = snd_tlv_convert_to_dB(dbTlv, 
					snd_ctl_elem_info_get_min(info),
					snd_ctl_elem_info_get_max(info), 
					value, &dbGain);
				if (rc3 == 0) {
					*db = dbGain / 100;
					return 0;
				} else {
					return -3;
				}
			} else {
				return -4;
			}
		} else {
			return -5;
		}
	});
}

int convertMixerDbToValue(const char* deviceName, const char* paramName, int db, int* value) {
    return withMixerElementInfo(deviceName, paramName, 
		[value, db](snd_hctl_elem_t* elem, snd_ctl_elem_info_t* info) {
		// At the moment we only support integer values
		int type = snd_ctl_elem_info_get_type(info);
		if (type == SND_CTL_ELEM_TYPE_INTEGER) {
			const unsigned int tlvSize = 32;
			unsigned int tlv[tlvSize];
			int rc2 = snd_hctl_elem_tlv_read(elem, tlv, tlvSize);
			if (rc2 != 0)
				return -2;
			unsigned int* dbTlv = 0;
			// NOTE: It's possible that this call will return a negative 
			// value to indicate a problem. 
			//
			// There is no discussion in the documentation about releasing
			// the pointer that we get back in dbTlv.
			int tlvInfoSize = snd_tlv_parse_dB_info(tlv, 32, &dbTlv);
			if (tlvInfoSize > 0) {                
				long value2 = 0;
				// Keep in mind that these functions operate in terms of 0.01dB
				int rc3 = snd_tlv_convert_from_dB(dbTlv, 
					snd_ctl_elem_info_get_min(info),
					snd_ctl_elem_info_get_max(info), 
					db * 100, &value2, 0);
				if (rc3 == 0) {
					*value = value2;
					return 0;
				} else {
					return -3;
				}
			} else {
				return -4;
			}
		} else {
			return -5;
		}
	});
}


}
