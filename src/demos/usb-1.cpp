/*
A demonstration of getting the db range of the mixers assoicated with
a USB sound device.

Relevant Docs: https://www.alsa-project.org/alsa-doc/alsa-lib/control.html
 */
#include <iostream>

#include <alsa/asoundlib.h>

#include "sound-map.h"

using namespace std;
using namespace kc1fsz;

int main(int, const char**) {
    
    const char* targetVendorName = "C-Media Electronics, Inc.";
    char query[64];
    snprintf(query, 64, "vendorname:\"%s\"", targetVendorName);
    int alsaDev;
    string ossDev;
    int rc = querySoundMap(query, alsaDev, ossDev);
    if (rc < 0) {
        cout << "querySoundMap() ERROR: " << rc << endl;
        return -1;
    }

    cout << "Found the device:" << endl;
    cout << " ALSA  : " << alsaDev << endl;
    cout << " OSS   : " << ossDev << endl;

    // Open the device 
    char name[32];
    snprintf(name, 32, "hw:%d", alsaDev);
    snd_ctl_t* ctl = 0;
    int rc2 = snd_ctl_open(&ctl, name, 0);
    cout << "open rc " << rc2 << endl;

    // Initialize element list for this device so we can iterate across it
    snd_ctl_elem_list_t* list;
    snd_ctl_elem_list_alloca(&list);
    // Get number of elements
    snd_ctl_elem_list(ctl, list);
    const int listCount = snd_ctl_elem_list_get_count(list);
    // Allocate space for identifiers.  This will need to be released using 
    // snd_ctl_elem_list_free_space() later
    snd_ctl_elem_list_alloc_space(list, listCount);
    // It appears that this needs to be called a second time before iterating
    // across the list.
    snd_ctl_elem_list(ctl, list); 

    for (int i = 0; i < listCount; i++) {
        
        // Basic element information from the list
        cout << "numid " << snd_ctl_elem_list_get_numid(list, i) << endl;
        cout << "name " << snd_ctl_elem_list_get_name(list, i) << endl;
        snd_ctl_elem_iface_t ift = snd_ctl_elem_list_get_interface(list, i);
        if (ift == SND_CTL_ELEM_IFACE_MIXER) {
            cout << "type mixer" << endl;
        }

        // Get the element id for the list item
        snd_ctl_elem_id_t* elem_id;
        snd_ctl_elem_id_alloca(&elem_id);
        snd_ctl_elem_list_get_id(list, i, elem_id);

        // Get the element info for the list item. Here the method is to allocate 
        // an info structure, fill in the numid, and then ask to have the rest of 
        // the data populated.
        snd_ctl_elem_info_t *info;
        snd_ctl_elem_info_alloca(&info);
        snd_ctl_elem_info_set_numid(info, snd_ctl_elem_list_get_numid(list, i));
        int rc4 = snd_ctl_elem_info(ctl, info);
        if (rc4 != 0)
            return -1;
        snd_ctl_elem_type_t elt = snd_ctl_elem_info_get_type(info);
        if (elt == SND_CTL_ELEM_TYPE_INTEGER) {
            cout << "  integer type element" << endl;
            cout << "  min  " << snd_ctl_elem_info_get_min(info) << endl;
            cout << "  max  " << snd_ctl_elem_info_get_max(info) << endl;

            // Check to see if TVL information is available. The TLV feature is designed to 
            // transfer data in a shape of Type/Length/Value, between a driver and any userspace 
            // applications. The main purpose is to attach supplement information for elements 
            // to an element set; e.g. dB range.
            if (snd_ctl_elem_info_is_tlv_readable(info)) {
                const unsigned int tlvSize = 32;
                unsigned int tlv[tlvSize];
                int rc6 = snd_ctl_elem_tlv_read(ctl, elem_id, tlv, tlvSize);
                if (rc6 != 0)
                    return -1;
                unsigned int* dbTlv = 0;
                // NOTE: It's possible that this call will return a negative 
                // value to indicate a problem. 
                //
                // There is no discussion in the documentation about releasing
                // the pointer that we get back in dbTlv.
                int tlvInfoSize = snd_tlv_parse_dB_info(tlv, 32, &dbTlv);
                if (tlvInfoSize > 0) {                

                    // NOTE: Gains are in in 0.01dB units                    
                    long dbMin, dbMax;
                    int rc7 = snd_tlv_get_dB_range(dbTlv, 
                        snd_ctl_elem_info_get_min(info),
                        snd_ctl_elem_info_get_max(info),
                        &dbMin, &dbMax);
                    if (rc7 != 0)
                        return -1;
                    cout << "    dbMin x100 " << dbMin << endl;
                    cout << "    dbMax x100 " << dbMax << endl;

                    long dbGain = 0;
                    snd_tlv_convert_to_dB(dbTlv, 
                        snd_ctl_elem_info_get_min(info),
                        snd_ctl_elem_info_get_max(info),
                        2, &dbGain);
                    cout << "    gain x100 " << dbGain << endl;
                }
            }
        }

        cout << endl;
    }
    
    // Cleanup
    snd_ctl_elem_list_free_space(list);
    snd_ctl_close(ctl);

    return 0;
}
