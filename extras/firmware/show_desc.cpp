#include <usb/usb_host.h>
#include "Arduino.h"
#include "show_desc.hpp"

void show_dev_desc(const usb_device_desc_t *dev_desc) {
  (void)dev_desc;
}

void show_config_desc(const void *p) {
  (void)p;
}

uint8_t show_interface_desc(const void *p) {
  const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;
  return intf ? intf->bInterfaceClass : 0;
}

void show_endpoint_desc(const void *p) {
  (void)p;
}

void show_hid_desc(const void *p) {
  (void)p;
}

void show_interface_assoc(const void *p) {
  (void)p;
}
