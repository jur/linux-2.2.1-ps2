/*
 *  PlayStation 2 Memory Card driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: mc.h,v 1.4.6.1 2001/09/19 10:08:22 takemura Exp $
 */

#ifndef PS2MC_H
#define PS2MC_H

typedef struct {
	struct {
		unsigned char Resv2,Sec,Min,Hour;
		unsigned char Day,Month;
		unsigned short Year;
	} _Create;
	struct {
		unsigned char Resv2,Sec,Min,Hour;
		unsigned char Day,Month;
		unsigned short Year;
	} _Modify;
	unsigned FileSizeByte;
	unsigned short AttrFile;
	unsigned short Reserve1;
	unsigned Reserve2[2];
	unsigned char EntryName[32];
} McDirEntry __attribute__((aligned (64)));


#define McMaxFileDiscr		3
#define McMaxPathLen		1023

#define McRDONLY		0x0001
#define McWRONLY		0x0002
#define McRDWR			0x0003
#define McCREAT			0x0200

#define McFileInfoCreate	0x01
#define McFileInfoModify	0x02
#define McFileInfoAttr		0x04

#define McFileAttrReadable	0x0001
#define McFileAttrWriteable	0x0002
#define McFileAttrExecutable	0x0004
#define McFileAttrSubdir	0x0020

#define McTZONE			(9 * 60 * 60)

#endif /* PS2MC_H */
