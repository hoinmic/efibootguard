/*
 * EFI Boot Guard
 *
 * Copyright (c) Siemens AG, 2020
 *
 * Authors:
 *  Dr. Johann Pfefferl <johann.pfefferl@siemens.com>
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier:	GPL-2.0
 */

#include <efi.h>
#include <efilib.h>
#include <pci/header.h>
#include <sys/io.h>
#include <mmio.h>
#include "utils.h"

#define PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_LPC	0xa150

#define SMBIOS_TYPE_OEM_129			129

#define SIMATIC_OEM_ENTRY_TYPE_BINARY		0xff

#define SIMATIC_IPC427E				0xa01
#define SIMATIC_IPC477E				0xa02

typedef struct {
	UINT8	type;
	UINT8	length;
	UINT8	reserved[3];
	UINT32	station_id;
} __attribute__((packed)) SIMATIC_OEM_ENTRY;

#define SIMATIC_WD_ENABLE_REG			0x62
#define  SIMATIC_WD_ENABLE			(1 << 0)
#define  SIMATIC_WD_MACRO_MOD			(1 << 1)
#define  SIMATIC_WD_SCALER_SHIFT		3
#define  SIMATIC_WD_TRIGGERED			(1 << 7)
#define SIMATIC_WD_TRIGGER_REG			0x66

#define SUNRISEPOINT_H_MMCFG_BASE		0xf0000000

#define P2SB_PCIID				0x00
#define P2SB_SBREG_BAR				0x10
#define P2SB_SBREG_BARH				0x14
#define P2SB_CTRL				0xe0
#define  P2SB_CFG_HIDE				(1 << 8)

#define GPIO_COMMUNITY0_PORT_ID			0xaf

/* drives SAFE_EN_N */
#define PAD_CFG_DW0_GPP_A_23			0x4b8
#define  PAD_CFG_GPIOTXSTATE			(1 << 0)

static UINT32 get_station_id(SMBIOS_STRUCTURE_POINTER oem_strct)
{
	SIMATIC_OEM_ENTRY *entry;
	UINTN n;

	entry = (SIMATIC_OEM_ENTRY *)(oem_strct.Raw + sizeof(*oem_strct.Hdr));

	/* Find 4th entry in OEM data. */
	for (n = 0; n < 3; n++) {
		if (entry->type != SIMATIC_OEM_ENTRY_TYPE_BINARY) {
			return 0;
		}
		entry = (SIMATIC_OEM_ENTRY *)((UINT8 *)entry + entry->length);
	}

	if (entry->type == SIMATIC_OEM_ENTRY_TYPE_BINARY &&
	    entry->length == sizeof(SIMATIC_OEM_ENTRY)) {
		return entry->station_id;
	}

	return 0;
}

static SMBIOS_STRUCTURE_POINTER
smbios_find_struct(SMBIOS_STRUCTURE_TABLE *table, UINT16 type)
{
	SMBIOS_STRUCTURE_POINTER strct;
	UINT8 *str;
	UINTN n;

	strct.Raw = (UINT8 *)(uintptr_t)table->TableAddress;

	for (n = 0; n < table->NumberOfSmbiosStructures; n++) {
		if (strct.Hdr->Type == type) {
			return strct;
		}
		/* Read over any appended strings. */
		str = strct.Raw + strct.Hdr->Length;
		while (str[0] != 0 || str[1] != 0) {
			str++;
		}
		strct.Raw = str + 2;
	}

	strct.Raw = NULL;
	return strct;
}

static UINTN mmcfg_address(UINTN bus, UINTN device, UINTN function,
			   UINTN offset)
{
	return SUNRISEPOINT_H_MMCFG_BASE + (bus << 20) + (device << 15) +
		(function << 12) + offset;
}

static UINT64 get_sbreg_rba(VOID)
{
	BOOLEAN p2sb_hidden =
		readw(mmcfg_address(0, 0x1f, 1, P2SB_PCIID)) == 0xffff;
	UINT32 lo, hi;
	UINT64 sbreg;

	/* Unhide the P2SB device if it's hidden. */
	if (p2sb_hidden) {
		writel(0, mmcfg_address(0, 0x1f, 1, P2SB_CTRL));
	}

	lo = readl(mmcfg_address(0, 0x1f, 1, P2SB_SBREG_BAR));
	hi = readl(mmcfg_address(0, 0x1f, 1, P2SB_SBREG_BARH));
	sbreg = (lo & 0xff000000) | ((UINT64)hi << 32);

	if (p2sb_hidden) {
		writel(P2SB_CFG_HIDE, mmcfg_address(0, 0x1f, 1, P2SB_CTRL));
	}

	return sbreg;
}

static EFI_STATUS __attribute__((constructor))
init(EFI_PCI_IO *pci_io, UINT16 pci_vendor_id, UINT16 pci_device_id,
     UINTN timeout)
{
	SMBIOS_STRUCTURE_TABLE *smbios_table;
	SMBIOS_STRUCTURE_POINTER smbios_struct;
	EFI_STATUS status;
	UINTN pad_cfg;
	UINT8 val;

	if (!pci_io || pci_vendor_id != PCI_VENDOR_ID_INTEL ||
	    pci_device_id != PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_LPC) {
		return EFI_UNSUPPORTED;
	}

	status = LibGetSystemConfigurationTable(&SMBIOSTableGuid,
						(VOID **)&smbios_table);
	if (status != EFI_SUCCESS) {
		return EFI_UNSUPPORTED;
	}

	smbios_struct = smbios_find_struct(smbios_table, SMBIOS_TYPE_OEM_129);
	if (smbios_struct.Raw == NULL) {
		return EFI_UNSUPPORTED;
	}

	switch (get_station_id(smbios_struct)) {
	case SIMATIC_IPC427E:
	case SIMATIC_IPC477E:
		INFO(L"Detected SIMATIC IPC4x7E watchdog\n");

		/*
		 * Drive SAFE_EN_N low to allow that watchdog can trigger a
		 * hard reset.
		 */
		pad_cfg = get_sbreg_rba() + (GPIO_COMMUNITY0_PORT_ID << 16) +
			PAD_CFG_DW0_GPP_A_23;
		writel(readl(pad_cfg) & ~PAD_CFG_GPIOTXSTATE, pad_cfg);

		if (timeout <= 2) {
			val = 0 << SIMATIC_WD_SCALER_SHIFT;
		} else if (timeout <= 4) {
			val = 1 << SIMATIC_WD_SCALER_SHIFT;
		} else if (timeout <= 6) {
			val = 2 << SIMATIC_WD_SCALER_SHIFT;
		} else if (timeout <= 8) {
			val = 3 << SIMATIC_WD_SCALER_SHIFT;
		} else if (timeout <= 16) {
			val = 4 << SIMATIC_WD_SCALER_SHIFT;
		} else if (timeout <= 32) {
			val = 5 << SIMATIC_WD_SCALER_SHIFT;
		} else if (timeout <= 48) {
			val = 6 << SIMATIC_WD_SCALER_SHIFT;
		} else {
			val = 7 << SIMATIC_WD_SCALER_SHIFT;
		}
		val |= SIMATIC_WD_MACRO_MOD;
		if (inb(SIMATIC_WD_ENABLE_REG) & SIMATIC_WD_TRIGGERED) {
			WARNING(L"Detected watchdog triggered reboot\n");
			/* acknowledge, turning off the LED */
			val |= SIMATIC_WD_TRIGGERED;
		}
		outb(val, SIMATIC_WD_ENABLE_REG);

		/* Enable the watchdog after programming it, just to be safe. */
		val |= SIMATIC_WD_ENABLE;
		outb(val, SIMATIC_WD_ENABLE_REG);

		/* Trigger the watchdog once. Now the clock ticks. */
		inb(SIMATIC_WD_TRIGGER_REG);

		return EFI_SUCCESS;

	default:
		return EFI_UNSUPPORTED;
	}
}
