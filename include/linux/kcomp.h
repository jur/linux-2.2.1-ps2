/*
 * Kernel compatibility glue to allow USB compile on 2.2.x kernels
 */

#include <linux/list.h>

#define LIST_HEAD_INIT(name) { &(name), &(name) }
static __inline__ void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

typedef struct wait_queue wait_queue_t;
typedef struct wait_queue *wait_queue_head_t;
#define DECLARE_WAITQUEUE(wait, current)	struct wait_queue wait = { current, NULL }
#define DECLARE_WAIT_QUEUE_HEAD(wait)		wait_queue_head_t wait
#define init_waitqueue_head(x)			*x=NULL

#define init_MUTEX(x)				*(x)=MUTEX
#ifdef CONFIG_PS2
#define init_MUTEX_LOCKED(x)			*(x)=MUTEX_LOCKED
#endif
#define DECLARE_MUTEX(name)			struct semaphore name=MUTEX
#define DECLARE_MUTEX_LOCKED(name)		struct semaphore name=MUTEX_LOCKED

#define __set_current_state(state_value)	do { current->state = state_value; } while (0)
#ifdef __SMP__
#define set_current_state(state_value)		do { mb(); __set_current_state(state_value); } while (0)
#else
#define set_current_state(state_value)		__set_current_state(state_value)
#endif

#define __exit

#ifdef __alpha
extern long __kernel_thread (unsigned long, int (*)(void *), void *);
static inline long kernel_thread (int (*fn) (void *), void *arg, unsigned long flags)
{
	return __kernel_thread (flags | CLONE_VM, fn, arg);
}
#undef CONFIG_APM
#endif

#define proc_mkdir(buf, usbdir)			create_proc_entry(buf, S_IFDIR, usbdir)

#define pci_enable_device(x)

#define VID_HARDWARE_CPIA			24
#define page_address(x)				(x | PAGE_OFFSET)

#ifdef MODULE
#define module_init(x)                          int init_module(void) { return x(); }
#define module_exit(x)                          void cleanup_module(void) { x();}
#define THIS_MODULE                             (&__this_module)
#else
#define module_init(x)                          int x##_module(void) { return x(); }
#define module_exit(x)                          void x##_module(void) { x(); }
#define THIS_MODULE                             NULL
#endif

#ifdef CONFIG_PS2
#define NET_XMIT_SUCCESS        0
#define NET_XMIT_DROP           1       /* skb dropped                  */
#define NET_XMIT_CN             2       /* congestion notification      */
#endif
