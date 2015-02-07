/*
 * lowlevel.c
 *
 * PURPOSE
 *  Low Level Device Routines for the UDF filesystem
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1999-2000 Ben Fennema
 *
 * HISTORY
 *
 *  03/26/99 blf  Created.
 */

#include "udfdecl.h"

#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <asm/uaccess.h>
#include <scsi/scsi.h>

typedef struct scsi_device Scsi_Device;
typedef struct scsi_cmnd   Scsi_Cmnd;

#include <scsi/scsi_ioctl.h>

#include <linux/udf_fs.h>
#include "udf_sb.h"

unsigned int 
udf_get_last_session(struct super_block *sb)
{
	struct cdrom_multisession ms_info;
	unsigned int vol_desc_start;
	kdev_t dev = sb->s_dev;
	struct inode inode_fake;
	extern struct file_operations * get_blkfops(unsigned int);
	int i;

	vol_desc_start=0;
	if (get_blkfops(MAJOR(dev))->ioctl!=NULL)
	{
		/* Whoops.  We must save the old FS, since otherwise
		 * we would destroy the kernels idea about FS on root
		 * mount in read_super... [chexum]
		 */
		mm_segment_t old_fs=get_fs();
		inode_fake.i_rdev=dev;
		ms_info.addr_format=CDROM_LBA;
		set_fs(KERNEL_DS);
		i=get_blkfops(MAJOR(dev))->ioctl(&inode_fake,
						NULL,
						CDROMMULTISESSION,
						(unsigned long) &ms_info);
		set_fs(old_fs);

#define WE_OBEY_THE_WRITTEN_STANDARDS 1

		if (i == 0)
		{
			udf_debug("XA disk: %s, vol_desc_start=%d\n",
				(ms_info.xa_flag ? "yes" : "no"), ms_info.addr.lba);
#if WE_OBEY_THE_WRITTEN_STANDARDS
			if (ms_info.xa_flag) /* necessary for a valid ms_info.addr */
#endif
				vol_desc_start = ms_info.addr.lba;
		}
		else
		{
			udf_debug("CDROMMULTISESSION not supported: rc=%d\n", i);
		}
	}
	else
	{
		udf_debug("Device doesn't know how to ioctl?\n");
	}
	return vol_desc_start;
}

#ifdef CDROM_LAST_WRITTEN

static unsigned int
udf_get_last_written(kdev_t dev, struct inode *inode_fake)
{
	extern struct file_operations * get_blkfops(unsigned int);
	unsigned long lastsector;

	if (!(get_blkfops(MAJOR(dev))->ioctl(inode_fake,
		NULL,
		CDROM_LAST_WRITTEN,
		(unsigned long) &lastsector)))
	{
		return lastsector - 1;
	}
	else
		return 0;
}

#else

static int
do_scsi(kdev_t dev, struct inode *inode_fake, Uint8 *command, int cmd_len,
	Uint8 *buffer, Uint32 in_len, Uint32 out_len)
{
	extern struct file_operations * get_blkfops(unsigned int);
	Uint32 *ip;

	ip = (Uint32 *)buffer;
	ip[0] = in_len;
	ip[1] = out_len;
	memcpy(buffer + 8, command, cmd_len);
	return get_blkfops(MAJOR(dev))->ioctl(inode_fake,
		NULL, SCSI_IOCTL_SEND_COMMAND, (unsigned long)buffer);
}

static unsigned int
udf_get_last_rti(kdev_t dev, struct inode *inode_fake)
{
	char buffer[128];
	int result = 0;
	int *ip;
	int track_no;
	Uint32 trackstart, tracklength, freeblocks;
	Uint8 cdb[10];
	unsigned long lastsector = 0;
	int len;

	ip = (int *)(buffer + 8);
	memset(cdb, 0, 10);
	cdb[0] = 0x51;
	cdb[8] = 32;
	result = do_scsi(dev, inode_fake, cdb, 10, buffer, 0, 32);
	if (!result)
	{
		track_no = buffer[14];
		udf_debug("Generic Read Disc Info worked; last track is %d. status=0x%x\n",
			track_no, buffer[10] & 0x3);
		memset(buffer, 0, 128);
		cdb[0] = 0x52;
		cdb[1] = 1;
		cdb[4] = (track_no & 0xFF00) >> 8;
		cdb[5] = track_no & 0xFF;
		cdb[8] = 8;
		result = do_scsi(dev, inode_fake, cdb, 10, buffer, 0, 8);
		if (!result)
		{
			len = cdb[8] = ((buffer[8] << 8) | (buffer[9] & 0xFF)) + 2;
			result = do_scsi(dev, inode_fake, cdb, 10, buffer, 0, len);
			if (!result)
			{
				if (buffer[14] & 0x40)
				{
					cdb[4] = ((track_no - 1) & 0xFF00) >> 8;
					cdb[5] = (track_no - 1) & 0xFF;
					result = do_scsi(dev, inode_fake, cdb, 10, buffer, 0, len);
				}
				if (!result)
				{
					trackstart = be32_to_cpu(ip[2]);
					tracklength = be32_to_cpu(ip[6]);
					freeblocks = be32_to_cpu(ip[4]);
					udf_debug("Start %d, length %d, freeblocks %d.\n", trackstart, tracklength, freeblocks);
					if (buffer[14] & 0x20)
					{
						if (buffer[14] & 0x10)
						{
							udf_debug("Packet size is %d.\n", be32_to_cpu(ip[5]));
							lastsector = trackstart + tracklength - 1;
						}
						else
						{
							udf_debug("Variable packet written track.\n");
							lastsector = trackstart + tracklength - 1;
							if (freeblocks)
							{
								lastsector = lastsector - freeblocks - 7;
							}
						}
					}
				}
			}
		}
	}
	return lastsector;
}

static unsigned int
udf_get_toc_entry(kdev_t dev, struct inode *inode_fake)
{
	extern struct file_operations * get_blkfops(unsigned int);
	struct cdrom_tocentry toc;
	int res, lastsector = 0;

	toc.cdte_format = CDROM_LBA;
	toc.cdte_track = 0xAA;
	
	if (!(res = get_blkfops(MAJOR(dev))->ioctl(inode_fake,
			NULL,
			CDROMREADTOCENTRY,
			(unsigned long) &toc)))
	{
		lastsector = toc.cdte_addr.lba - 1;
	}

	return lastsector;
}

static unsigned int
udf_get_capacity(kdev_t dev, struct inode *inode_fake)
{
	char buffer[128];
	int result = 0;
	int *ip;
	Uint8 cdb[10];
	unsigned long lastsector = 0;

	ip = (int *)(buffer + 8);
	memset(cdb, 0, 10);

	cdb[0] = READ_CAPACITY;
	result = do_scsi(dev, inode_fake, cdb, 10, buffer, 0, 8);
	if (!result)
		lastsector = be32_to_cpu(ip[0]);

	return lastsector;
}

static int
is_mmc(kdev_t dev, struct inode *inode_fake)
{
	Uint8 buffer[142];
	int result = 0, n;
	Uint8 cdb[6];
	Uint8 *data = &buffer[8];
	int len = 4;

	cdb[0] = MODE_SENSE;
	cdb[2] = 0x2A;
	cdb[4] = len;
	cdb[1] = cdb[3] = cdb[5] = 0;

	memset(buffer, 0, 142);
	result = do_scsi(dev, inode_fake, cdb, 6, buffer, 0, len);
	if (!result)
	{
		len = cdb[4] = data[3] + 4 + 2;
		result = do_scsi(dev, inode_fake, cdb, 6, buffer, 0, len);
		if (!result)
		{
			n = data[3] + 4;
			len = cdb[4] = n + 2 + data[n+1];
			result = do_scsi(dev, inode_fake, cdb, 6, buffer, 0, len);
			if (!result && ((data[n] & 0x3F) == 0x2A))
			{
				udf_debug("Page Code=0x%02x  PS=0x%1x  Page Length=0x%02x\n",
					data[n] & 0x3F, (data[n] >> 7) & 0x01, data[n+1]);
				udf_debug("DVD-RAM R/W(%c/%c)  DVD-R R/W(%c/%c)  DVD-ROM R(%c)\n",
					data[n+2] & 0x20 ? 'Y' : 'N', data[n+3] & 0x20 ? 'Y' : 'N',
					data[n+2] & 0x10 ? 'Y' : 'N', data[n+3] & 0x10 ? 'Y' : 'N',
					data[n+2] & 0x08 ? 'Y' : 'N');
				udf_debug("CD-RW   R/W(%c/%c)  CD-R  R/W(%c/%c)  Fixed Packet (%c)\n",
					data[n+2] & 0x02 ? 'Y' : 'N', data[n+3] & 0x02 ? 'Y' : 'N',
					data[n+2] & 0x01 ? 'Y' : 'N', data[n+3] & 0x01 ? 'Y' : 'N',
					data[n+2] & 0x04 ? 'Y' : 'N');
				udf_debug("Multi Session (%c)  Mode 2 Form 2/1 (%c/%c) Digital Port (2)/(1) (%c/%c)\n",
					data[n+4] & 0x40 ? 'Y' : 'N', data[n+4] & 0x20 ? 'Y' : 'N',
					data[n+4] & 0x10 ? 'Y' : 'N', data[n+4] & 0x08 ? 'Y' : 'N',
					data[n+4] & 0x04 ? 'Y' : 'N');
				udf_debug("Composite (%c)  Audio Play (%c)  Read Bar Code (%c)  UPC (%c)  ISRC (%c)\n",
					data[n+4] & 0x02 ? 'Y' : 'N', data[n+4] & 0x01 ? 'Y' : 'N',
					data[n+5] & 0x80 ? 'Y' : 'N', data[n+5] & 0x40 ? 'Y' : 'N',
					data[n+5] & 0x20 ? 'Y' : 'N');
				udf_debug("C2 Pointers are supported (%c)  R-W De-interleved & corrected (%c)\n",
					data[n+5] & 0x10 ? 'Y' : 'N', data[n+5] & 0x80 ? 'Y' : 'N');
				udf_debug("R-W Supported (%c)  CD-DA Stream is Accurate (%c)  CD-DA Commands Supported (%c)\n",
					data[n+5] & 0x04 ? 'Y' : 'N', data[n+5] & 0x02 ? 'Y' : 'N',
					data[n+5] & 0x01 ? 'Y' : 'N');
				udf_debug("Loading Mechanism Type=0x%03x  Eject (%c)  Prevent Jumper (%c)\n",
					(data[n+6] >> 5) & 0x07, data[n+6] & 0x08 ? 'Y' : 'N',
					data[n+6] & 0x04 ? 'Y' : 'N');
				udf_debug("Lock State (%c)  Lock(%c)\n",
					data[n+6] & 0x02 ? 'Y' : 'N', data[n+6] & 0x01 ? 'Y' : 'N');
				udf_debug("P through W in Lead-In (%c)  Side Change Capable (%c)  S/W Slot Selection (%c)\n",
					data[n+7] & 0x20 ? 'Y' : 'N', data[n+7] & 0x10 ? 'Y' : 'N',
					data[n+7] & 0x08 ? 'Y' : 'N');
				udf_debug("Changer Supports Disc Present (%c)  Seperate Channel Mute (%c)  Seperate Volume Levels (%c)\n",
					data[n+7] & 0x04 ? 'Y' : 'N', data[n+7] & 0x02 ? 'Y' : 'N',
					data[n+7] & 0x01 ? 'Y' : 'N');
				udf_debug("Maximum Read Speed Supported (in kBps)=0x%04x (Obsolete)\n",
					(data[n+8] << 8) | (data[n+9] & 0xFF));
				udf_debug("Number of Volume Levels Support=0x%04x\n",
					(data[n+10] << 8) | (data[n+11] & 0xFF));
				udf_debug("Buffer Size supported by Drive (in KBytes)=0x%04x\n",
					(data[n+12] << 8) | (data[n+13] & 0xFF));
				udf_debug("Current Read Speed Selected (in kBps)=0x%04x (Obsolete)\n",
					(data[n+14] << 8) | (data[n+15] & 0xFF));
				udf_debug("Digital Out: Length=0x%01x  LSBF (%c)  RCK (%c)  BCKF (%c)\n",
					(data[n+17] >> 4) & 0x03, data[n+17] & 0x08 ? 'Y' : 'N',
					data[n+17] & 0x04 ? 'Y' : 'N', data[n+17] & 0x02 ? 'Y' : 'N');
				udf_debug("Maximum Write Speed Supported (in kBps)=0x%04x (Obsolete)\n",
					(data[n+18] << 8) | (data[n+19] & 0xFF));
				udf_debug("Current Write Speed Selected (in kBps)=0x%04x (Obsolete)\n",
					(data[n+20] << 8) | (data[n+21] & 0xFF));
				udf_debug("Copy Management Revision Supported=%04x\n",
					(data[n+22] << 8) | (data[n+23] & 0xFF));
			}
			else 
				return 0;
		}
	}
	return !result;
}

#endif

unsigned int
udf_get_last_block(struct super_block *sb)
{
	kdev_t dev = sb->s_dev;
	struct inode inode_fake;
	extern struct file_operations * get_blkfops(unsigned int);
	int ret;
	unsigned long lblock;
	int accurate = 0;

	if (get_blkfops(MAJOR(dev))->ioctl!=NULL)
	{
      /* Whoops.  We must save the old FS, since otherwise
       * we would destroy the kernels idea about FS on root
       * mount in read_super... [chexum]
       */
		mm_segment_t old_fs=get_fs();
		inode_fake.i_rdev=dev;
		set_fs(KERNEL_DS);

		lblock = 0;
		ret = get_blkfops(MAJOR(dev))->ioctl(&inode_fake,
				NULL,
				BLKGETSIZE,
				(unsigned long) &lblock);

		if (!ret && lblock != 0x7FFFFFFF) /* Hard Disk */
		{
			udf_debug("BLKGETSIZE lblock=%ld\n", lblock);
			lblock = ((512 * lblock) / sb->s_blocksize) - 1;
			accurate = 1;
		}
		else /* CDROM */
		{
#ifdef CDROM_LAST_WRITTEN
			if ((lblock = udf_get_last_written(dev, &inode_fake)))
			{
				udf_debug("last_written lblock=%ld\n", lblock);
				accurate = 1;
			}
#else
			if (is_mmc(dev, &inode_fake) &&
				(lblock = udf_get_last_rti(dev, &inode_fake)))
			{
				udf_debug("LAST_RTI lblock=%ld\n", lblock);
			}
			else if ((lblock = udf_get_toc_entry(dev, &inode_fake)))
			{
				udf_debug("TOC_ENTRY lblock=%ld\n", lblock);
			}
			else if ((lblock = udf_get_capacity(dev, &inode_fake)))
			{
				udf_debug("READ_CAPACITY lblock=%ld\n", lblock);
			}
#endif
		}
		set_fs(old_fs);
		return lblock;
	}
	else
	{
		udf_debug("Device doesn't know how to ioctl?\n");
	}
	return 0;
}
