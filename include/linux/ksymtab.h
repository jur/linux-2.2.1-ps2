/*
 * ksymtab.h - kernel symbol support 
 */


#ifndef _KSYMTAB_H
#define _KSYMTAB_H

#include <linux/miscdevice.h>
#define KSYMTAB_MINOR	MISC_DYNAMIC_MINOR

#ifndef KSYMTAB_DEVICE_NAME
#define  KSYMTAB_DEVICE_NAME "ksymtab"
#endif 

#ifdef __KERNEL__

#ifndef _LANGUAGE_ASSEMBLY

#include <linux/spinlock.h>

struct ksymtab_methods {
	volatile int valid;
	int (*get_entries_nr) (void);
	unsigned long (*find_symbol) (unsigned long addr, 
		char *symbol, int sz, struct module **mod);
	volatile int	once;	/* flag for init_lock_once_only() */
	spinlock_t lock;
	/* points to methods on the module */
	volatile int (*internal_get_entries_nr) (void);
	volatile unsigned long (*internal_find_symbol) (unsigned long addr, 
		char *symbol, int sz, struct module **mod);
};


static inline void init_lock_once_only
	(volatile int *once, spinlock_t *lock __attribute__((unused)))
{
	if (!tas(once)) {
		spin_lock_init(lock);
	}
}

#endif /*_LANGUAGE_ASSEMBLY*/

#endif  /* __KERNEL_ */

#endif /*_KSYMTAB_H*/


