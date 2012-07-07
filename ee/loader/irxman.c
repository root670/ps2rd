/*
 * IRX file manager
 *
 * Copyright (C) 2009-2010 Mathias Lafeldt <misfire@debugon.org>
 *
 * This file is part of PS2rd, the PS2 remote debugger.
 *
 * PS2rd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PS2rd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PS2rd.  If not, see <http://www.gnu.org/licenses/>.
 *
 * $Id$
 */

#include <tamtypes.h>
#include <kernel.h>
#include <string.h>
#include <loadfile.h>
#include "dbgprintf.h"
#include "configman.h"
#include "irxman.h"

/* Default IOP modules to load */
static const char *_modules[] = {
	"rom0:SIO2MAN",
	"rom0:MCMAN",
	"rom0:MCSERV",
	"rom0:PADMAN",
	NULL
};

/* Statically linked IRX files */
//extern u8  _ps2dev9_irx_start[];
//extern u8  _ps2dev9_irx_end[];
//extern int _ps2dev9_irx_size;
//extern u8  _ps2ip_irx_start[];
//extern u8  _ps2ip_irx_end[];
//extern int _ps2ip_irx_size;
extern u8  _ps2smap_irx_start[];
extern u8  _ps2smap_irx_end[];
extern int _ps2smap_irx_size;
#ifdef _SMS_MODULES
//extern u8  _ps2ip_sms_irx_start[];
//extern u8  _ps2ip_sms_irx_end[];
//extern int _ps2ip_sms_irx_size;
extern u8  _ps2smap_sms_irx_start[];
extern u8  _ps2smap_sms_irx_end[];
extern int _ps2smap_sms_irx_size;
#endif
extern u8  _debugger_irx_start[];
extern u8  _debugger_irx_end[];
extern int _debugger_irx_size;
extern u8  _memdisk_irx_start[];
extern u8  _memdisk_irx_end[];
extern int _memdisk_irx_size;
extern u8  _eesync_irx_start[];
extern u8  _eesync_irx_end[];
extern int _eesync_irx_size;

extern u8  _usbd_irx_start[];
extern u8  _usbd_irx_end[];
extern int _usbd_irx_size;
extern u8  _usb_mass_irx_start[];
extern u8  _usb_mass_irx_end[];
extern int _usb_mass_irx_size;

/* TODO: make it configurable */
#define IRX_ADDR	0x80030000

#define IRX_NUM		6

/* RAM file table entry */
typedef struct {
	u32	hash;
	u8	*addr;
	u32	size;
} ramfile_t;

/**
 * strhash - String hashing function as specified by the ELF ABI.
 * @name: string to calculate hash from
 * @return: 32-bit hash value
 */
static u32 strhash(const char *name)
{
	const u8 *p = (u8*)name;
	u32 h = 0, g;

	while (*p) {
		h = (h << 4) + *p++;
		if ((g = (h & 0xf0000000)) != 0)
			h ^= (g >> 24);
		h &= ~g;
	}

	return h;
}

/*
 * Helper to populate an RAM file table entry.
 */
static void ramfile_set(ramfile_t *file, const char *name, u8 *addr, u32 size)
{
	file->hash = name ? strhash(name) : 0;
	file->addr = addr;
	file->size = size;

	D_PRINTF("%s: name=%s hash=%08x addr=%08x size=%i\n", __FUNCTION__,
		name, file->hash, (u32)file->addr, file->size);
}

/*
 * Copy statically linked IRX files to kernel RAM.
 * They will be loaded by the debugger later...
 */
void install_modules(const config_t *config)
{
	ramfile_t file_tab[IRX_NUM + 1];
	ramfile_t *file_ptr = file_tab;
	ramfile_t *ktab = NULL;
	u32 addr = IRX_ADDR;

	D_PRINTF("%s: addr=%08x\n", __FUNCTION__, addr);

	/*
	 * build RAM file table
	 */
#ifdef _SMS_MODULES
	if (config_get_bool(config, SET_DEBUGGER_SMS_MODULES)) {
//		ramfile_set(file_ptr++, "ps2ip", _ps2ip_sms_irx_start, _ps2ip_sms_irx_size);
		ramfile_set(file_ptr++, "ps2smap", _ps2smap_sms_irx_start, _ps2smap_sms_irx_size);
	} else {
#endif
//		ramfile_set(file_ptr++, "ps2ip", _ps2ip_irx_start, _ps2ip_irx_size);
		ramfile_set(file_ptr++, "ps2smap", _ps2smap_irx_start, _ps2smap_irx_size);
#ifdef _SMS_MODULES
	}
#endif
	//ramfile_set(file_ptr++, "ps2dev9", _ps2dev9_irx_start, _ps2dev9_irx_size);
	ramfile_set(file_ptr++, "debugger", _debugger_irx_start, _debugger_irx_size);
	ramfile_set(file_ptr++, "memdisk", _memdisk_irx_start, _memdisk_irx_size);
	ramfile_set(file_ptr++, "eesync", _eesync_irx_start, _eesync_irx_size);
	ramfile_set(file_ptr, NULL, NULL, 0); /* terminator */

	/*
	 * copy modules to kernel RAM
	 *
	 * memory structure at @addr:
	 * |RAM file table|IRX module #1|IRX module #2|etc.
	 */
	DI();
	ee_kmode_enter();

	ktab = (ramfile_t*)addr;
	addr += sizeof(file_tab);
	file_ptr = file_tab;

	while (file_ptr->hash) {
		memcpy((u8*)addr, file_ptr->addr, file_ptr->size);
		file_ptr->addr = (u8*)addr;
		addr += file_ptr->size;
		file_ptr++;
	}

	memcpy(ktab, file_tab, sizeof(file_tab));

	ee_kmode_exit();
	EI();

	FlushCache(0);
}

/*
 * Load IRX modules into IOP RAM.
 */
int load_modules(const config_t *config)
{
	const char **modv = _modules;
	int i = 0, ret;

	while (modv[i] != NULL) {
		ret = SifLoadModule(modv[i], 0, NULL);
		if (ret < 0) {
			fprintf(stderr, "%s: failed to load module: %s (%i)\n",
				__FUNCTION__, modv[i], ret);
			return -1;
		}
		i++;
	}

	if (config_get_bool(config, SET_USB_SUPPORT)) {
		SifExecModuleBuffer(_usbd_irx_start, _usbd_irx_size, 0, NULL, &ret);
		if (ret < 0) {
			fprintf(stderr, "%s: failed to load module usbd.irx (%i)\n",
				__FUNCTION__, ret);
		}

		SifExecModuleBuffer(_usb_mass_irx_start, _usb_mass_irx_size, 0, NULL, &ret);
		if (ret < 0) {
			fprintf(stderr, "%s: failed to load module usb_mass.irx (%i)\n",
				__FUNCTION__, ret);
		}
	}

	return 0;
}
