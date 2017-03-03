/*
 * This file is part of linux-driver-management.
 *
 * Copyright © 2016-2017 Ikey Doherty
 *
 * linux-driver-management is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <assert.h>
#include <pci/pci.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "gpu.h"
#include "scanner.h"

/**
 * Determine the driver for a PCI device
 */
static char *ldm_pci_device_driver(struct pci_dev *dev)
{
        char *p = NULL;
        if (asprintf(&p,
                     "/sys/devices/pci%04x:%02x/%04x:%02x:%02x.%d/driver",
                     dev->domain,
                     dev->bus,
                     dev->domain,
                     dev->bus,
                     dev->dev,
                     dev->func) < 0) {
                abort();
        }
        char *r = realpath(p, NULL);
        free(p);
        if (!r) {
                return strdup("unknown");
        }
        char *r2 = basename(r);
        char *ret = strdup(r2);
        free(r);
        return ret;
}

/**
 * Convert the PCI class into a usable LDM type, i.e. GPU
 */
static LdmDeviceType ldm_pci_to_device_type(struct pci_dev *dev)
{
        if (dev->device_class >= PCI_CLASS_DISPLAY_VGA &&
            dev->device_class <= PCI_CLASS_DISPLAY_3D) {
                return LDM_DEVICE_GPU;
        }
        return LDM_DEVICE_UNKNOWN;
}

/**
 * Construct a new LdmDevice for a PCI device
 */
static LdmDevice *ldm_pci_device_new(LdmDeviceType type, struct pci_dev *dev, char *name)
{
        LdmDevice *ret = NULL;
        LdmPCIAddress addr = {
                .domain = dev->domain, .bus = dev->bus, .dev = dev->dev, .func = dev->func,
        };

        switch (type) {
        case LDM_DEVICE_GPU: {
                /* Handle GPU specific device construction */
                LdmGPU *gpu = NULL;

                gpu = calloc(1, sizeof(LdmGPU));
                if (!gpu) {
                        goto oom_fail;
                }

                *gpu = (LdmGPU){
                        .address = addr, .vendor_id = dev->vendor_id, .device_id = dev->device_id,
                };
                ret = (LdmDevice *)gpu;
        } break;
        case LDM_DEVICE_UNKNOWN: {
                ret = calloc(1, sizeof(LdmDevice));
                if (!ret) {
                        goto oom_fail;
                }
        } break;
        default:
                fputs("Condition should not be reached", stderr);
                abort();
                break;
        }

        assert(ret != NULL);

        /* Finish off the structure */
        ret->type = type;
        ret->driver = ldm_pci_device_driver(dev);
        if (name) {
                ret->device_name = strdup(name);
        }

        return ret;

oom_fail:
        fputs("Out of memory", stderr);
        abort();
        return NULL;
}

static LdmDevice *ldm_scan_pci_devices(void)
{
        struct pci_access *ac = NULL;
        char buf[1024];
        char *nom = NULL;
        LdmDevice *root = NULL;
        LdmDevice *last = NULL;

        /* Init PCI lookup */
        ac = pci_alloc();
        if (!ac) {
                abort();
        }
        pci_init(ac);
        pci_scan_bus(ac);

        /* Iterate devices looking for something interesting. */
        for (struct pci_dev *dev = ac->devices; dev != NULL; dev = dev->next) {
                pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_CLASS);
                LdmDeviceType type = ldm_pci_to_device_type(dev);
                LdmDevice *device = NULL;

                /* Skip unknown for now */
                if (type == LDM_DEVICE_UNKNOWN) {
                        continue;
                }

                /* Skip dodgy devices */
                if (dev->vendor_id == 0 && dev->device_id == 0) {
                        continue;
                }

                nom = pci_lookup_name(ac,
                                      buf,
                                      sizeof(buf),
                                      PCI_LOOKUP_DEVICE,
                                      dev->vendor_id,
                                      dev->device_id);

                device = ldm_pci_device_new(type, dev, nom);
                if (!device) {
                        goto cleanup;
                }
                if (!root) {
                        root = device;
                }
                if (last) {
                        last->next = device;
                }
                last = device;
        }

cleanup:
        pci_cleanup(ac);
        return root;
}

LdmDevice *ldm_scan_devices()
{
        LdmDevice *ret = NULL;

        /* Scan PCI right now, in future we may add USB, etc. */
        ret = ldm_scan_pci_devices();

        return ret;
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */