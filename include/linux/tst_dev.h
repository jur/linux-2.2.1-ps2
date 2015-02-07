/*
 * tst_dev.h - TEST and SET pseudo device interface
 */


#ifndef _TST_DEV_H
#define _TST_DEV_H

#define TST_DEVICE_NAME	"tst"

#ifndef _LANGUAGE_ASSEMBLY

#include <linux/types.h>

struct _tst_area_info {
	__u32 	magic;
	__u32 	pad1;
	void 	*map_addr;
#if _MIPS_SZPTR==32
	__u32 	pad2;
#endif
	};

#endif /*_LANGUAGE_ASSEMBLY*/

#define _TST_INFO_MAGIC		0x20000304

#ifdef __KERNEL__
#define _TST_ACCESS_MAGIC	0x00200000
#define _TST_START_MAGIC	0x00300000
#endif  /* __KERNEL_ */

#endif  /*_TST_DEV_H */



