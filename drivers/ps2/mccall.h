/*
 *  PlayStation 2 Memory Card driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: mccall.h,v 1.1.6.2 2001/09/19 10:08:22 takemura Exp $
 */

#include <asm/ps2/sifdefs.h>
#include <asm/ps2/sbcall.h>

static __inline__ int ps2mclib_Init(void)
{
	int res;

#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (sbios(SB_GETVER, NULL) < 0x0200)
		return -1;
#endif

	do {
		if (sbios_rpc(SBR_MC_INIT, NULL, &res) < 0 || res < 0)
			return -1;
	} while (res == 0);
	return 1;
}

static __inline__ int ps2mclib_Open(int port, int slot, const char *name, int mode, int *result)
{
	struct sbr_mc_open_arg arg;

	arg.port = port;
	arg.slot = slot;
	arg.name = name;
	arg.mode = mode;
	return sbios_rpc(SBR_MC_OPEN, &arg, result);
}

static __inline__ int ps2mclib_Mkdir(int port, int slot, const char *name, int *result)
{
	struct sbr_mc_mkdir_arg arg;

	arg.port = port;
	arg.slot = slot;
	arg.name = name;
	return  sbios_rpc(SBR_MC_MKDIR, &arg, result);
}

static __inline__ int ps2mclib_Close(int fd, int *result)
{
	struct sbr_mc_close_arg arg;

	arg.fd = fd;
	return sbios_rpc(SBR_MC_CLOSE, &arg, result);
}

static __inline__ int ps2mclib_Seek(int fd, int offset, int mode, int *result)
{
	struct sbr_mc_seek_arg arg;

	arg.fd = fd;
	arg.offset = offset;
	arg.mode = mode;
	return sbios_rpc(SBR_MC_SEEK, &arg, result);
}

static __inline__ int ps2mclib_Read(int fd, void *buff, int size, int *result)
{
	struct sbr_mc_read_arg arg;

	arg.fd = fd;
	arg.buff = buff;
	arg.size = size;
	return sbios_rpc(SBR_MC_READ, &arg, result);
}

static __inline__ int ps2mclib_Write(int fd, void *buff, int size, int *result)
{
	struct sbr_mc_write_arg arg;

	arg.fd = fd;
	arg.buff = buff;
	arg.size = size;
	return sbios_rpc(SBR_MC_WRITE, &arg, result);
}

static __inline__ int ps2mclib_GetInfo(int port, int slot, int *type, int *free, int *format, int *result)
{
	struct sbr_mc_getinfo_arg arg;

	arg.port = port;
	arg.slot = slot;
	arg.type = type;
	arg.free = free;
	arg.format = format;
	return sbios_rpc(SBR_MC_GETINFO, &arg, result);
}

static __inline__ int ps2mclib_GetDir(int port, int slot, const char *name,
				      unsigned int mode, int maxent,
				      McDirEntry *table, int *result)
{
	struct sbr_mc_getdir_arg arg;

	arg.port = port;
	arg.slot = slot;
	arg.name = name;
	arg.mode = mode;
	arg.maxent = maxent;
	arg.table = table;
	return sbios_rpc(SBR_MC_GETDIR, &arg, result);
}

static __inline__ int ps2mclib_Format(int port, int slot, int *result)
{
	struct sbr_mc_format_arg arg;

	arg.port = port;
	arg.slot = slot;
	return sbios_rpc(SBR_MC_FORMAT, &arg, result);
}

static __inline__ int ps2mclib_Delete(int port, int slot, const char *name, int *result)
{
	struct sbr_mc_delete_arg arg;

	arg.port = port;
	arg.slot = slot;
	arg.name = name;
	return sbios_rpc(SBR_MC_DELETE, &arg, result);
}

static __inline__ int ps2mclib_Flush(int fd, int *result)
{
	struct sbr_mc_flush_arg arg;

	arg.fd = fd;
	return sbios_rpc(SBR_MC_FLUSH, &arg, result);
}

static __inline__ int ps2mclib_SetFileInfo(int port, int slot,
					   const char *name, const char *info,
					   unsigned int valid, int *result)
{
	struct sbr_mc_setfileinfo_arg arg;

	arg.port = port;
	arg.slot = slot;
	arg.name = name;
	arg.info = info;
	arg.valid = valid;
	return sbios_rpc(SBR_MC_SETFILEINFO, &arg, result);
}

static __inline__ int ps2mclib_Rename(int port, int slot,
				      const char *org, const char *new,
				      int *result)
{
	struct sbr_mc_rename_arg arg;

	arg.port = port;
	arg.slot = slot;
	arg.org = org;
	arg.new = new;
	return sbios_rpc(SBR_MC_RENAME, &arg, result);
}

static __inline__ int ps2mclib_Unformat(int port, int slot, int *result)
{
	struct sbr_mc_unformat_arg arg;

	arg.port = port;
	arg.slot = slot;
	return sbios_rpc(SBR_MC_UNFORMAT, &arg, result);
}

static __inline__ int ps2mclib_GetEntSpace(int port, int slot, const char *path, int *result)
{
	struct sbr_mc_getentspace_arg arg;

	arg.port = port;
	arg.slot = slot;
	arg.path = path;
	return sbios_rpc(SBR_MC_GETENTSPACE, &arg, result);
}
