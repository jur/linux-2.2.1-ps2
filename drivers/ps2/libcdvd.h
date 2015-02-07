/*
 *  PlayStation 2 CD/DVD driver
 *
 *        Copyright (C) 1993, 1994, 2000, 2001
 *        Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * derived from ee/include/libcdvd.h (release 2.0.3)
 *
 * $Id: libcdvd.h,v 1.4.6.2 2001/09/19 10:08:22 takemura Exp $
 */

#ifndef PS2LIBCDVD_H
#define PS2LIBCDVD_H

/*
 * read data pattern
 */
#define SCECdSecS2048		0	/* sector size 2048 */
#define SCECdSecS2328		1	/* sector size 2328 */
#define SCECdSecS2340		2	/* sector size 2340 */
#define SCECdSecS2352		0	/* sector size 2352  CD-DA read */
#define SCECdSecS2368		1	/* sector size 2368  CD-DA read */
#define SCECdSecS2448		2	/* sector size 2448  CD-DA read */

/*
 * spindle control
 */
#define SCECdSpinMax		0	/* maximum speed	*/
#define SCECdSpinNom		1	/* optimized speed	*/
#define SCECdSpinX1             2	/* x1			*/
#define SCECdSpinX2             3	/* x2			*/
#define SCECdSpinX4             4	/* x4			*/
#define SCECdSpinX12            5	/* x12			*/
#define SCECdSpinNm2           10	/* optimized speed
					   (based on current speed) */
#define SCECdSpin1p6           11	/* DVD x1.6 CLV		*/
#define SCECdSpinMx            20	/* maximum speed	*/

/*
 * error code
 */
#define SCECdErFAIL		-1	/* can't get error code		*/
#define SCECdErNO		0x00	/* No Error			*/
#define SCECdErEOM		0x32	/* End of Media			*/
#define SCECdErTRMOPN		0x31	/* tray was opened while reading */
#define SCECdErREAD		0x30	/* read error			*/
#define SCECdErPRM		0x22	/* invalid parameter		*/
#define SCECdErILI		0x21	/* illegal length		*/
#define SCECdErIPI		0x20	/* illegal address		*/
#define SCECdErCUD		0x14	/* not appropreate for current disc */
#define SCECdErNORDY		0x13    /* not ready			*/
#define SCECdErNODISC		0x12	/* no disc			*/
#define SCECdErOPENS		0x11	/* tray is open			*/
#define SCECdErCMD		0x10	/* not supported command	*/
#define SCECdErABRT		0x01	/* aborted			*/

/*
 * disc type
 */
#define SCECdIllgalMedia 	0xff
#define SCECdDVDV		0xfe
#define SCECdCDDA		0xfd
#define SCECdPS2DVD		0x14
#define SCECdPS2CDDA		0x13
#define SCECdPS2CD		0x12
#define SCECdPSCDDA 		0x11
#define SCECdPSCD		0x10
#define SCECdUNKNOWN		0x05
#define SCECdDETCTDVDD		0x04
#define SCECdDETCTDVDS		0x03
#define SCECdDETCTCD		0x02
#define SCECdDETCT		0x01
#define SCECdNODISC 		0x00

/*
 * spinup result
 */
#define SCECdComplete	0x02	/* Command Complete 	  */
#define SCECdNotReady	0x06	/* Drive Not Ready	  */

/*
 * read mode
 */
typedef struct sceCdRMode {
	u_char trycount;	/* trycount   */
	u_char spindlctrl;	/* spindlctrl */
	u_char datapattern;	/* datapattern */
	u_char pad;		/* pad         */
} sceCdRMode;

/*
 * media mode
 */
#define SCECdCD         1
#define SCECdDVD        2

/*
 * tray request
 */
#define SCECdTrayOpen   0       /* Tray Open  */
#define SCECdTrayClose  1       /* Tray Close */
#define SCECdTrayCheck  2       /* Tray Check */

#endif /* ! PS2LIBCDVD_H */
