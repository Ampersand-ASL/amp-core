#include <libusb-1.0/libusb.h> // Include the libusb header
#include <stdio.h>

int main() {
    libusb_context *ctx = NULL; // a libusb session
    libusb_device **list; // pointer to pointer of device, used to retrieve a list of devices
    ssize_t count; // number of devices in the list

    // 1. Initialize libusb
    if (libusb_init(&ctx) < 0) {
        // Handle error
        return 1;
    }

    // 2. Get Device List
    count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        // Handle error
        libusb_exit(ctx);
        return 1;
    }

    // 3. Iterate and Process Devices
    for (ssize_t i = 0; i < count; i++) {
        libusb_device *dev = list[i];
        struct libusb_device_descriptor desc;

        if (libusb_get_device_descriptor(dev, &desc) == 0) {
            printf("Bus %d Port %d Device %d: ID %04x:%04x\n",
                   libusb_get_bus_number(dev),
                   libusb_get_port_number(dev),
                   libusb_get_device_address(dev),
                   desc.idVendor, desc.idProduct
                );

            // Further processing of device descriptor or opening the device
        }
    }

    // 4. Free Device List
    libusb_free_device_list(list, 1); // 1 to unref all devices

    // 5. Deinitialize libusb
    libusb_exit(ctx);

    return 0;
}