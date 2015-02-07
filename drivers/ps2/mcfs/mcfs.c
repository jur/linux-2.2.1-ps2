/*
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 * $Id: mcfs.c,v 1.11 2000/10/12 12:23:57 takemura Exp $
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/soundcard.h>
#include <linux/autoconf.h>
#include <asm/smplock.h>

#include "mcfs.h"
#include "mcfs_debug.h"

unsigned long ps2mcfs_debug = 0;
extern int (*ps2mc_blkrw_hook)(int, int, void*, int);

MODULE_PARM(ps2mcfs_debug, "i");

char *ps2mcfs_basedir = PS2MC_BASEDIR;

static struct file_system_type ps2mcfs_fs_type = {
	"ps2mcfs", 
	FS_REQUIRES_DEV /* FS_NO_DCACHE doesn't work correctly */,
	ps2mcfs_read_super, 
	NULL
};

/* wacthing thread stuff */
static struct semaphore *thread_sem = NULL;
static struct task_struct *thread_task = NULL;
static struct wait_queue *thread_wq = NULL;

static int ps2mcfs_init(void);
static void ps2mcfs_cleanup(void);
static int ps2mcfs_thread(void *);

static int
ps2mcfs_init()
{
	TRACE("ps2mcfs_init()\n");
	printk("PlayStation 2 Memory Card file system\n");

	if (ps2mcfs_init_filebuf() < 0 ||
	    ps2mcfs_init_pathcache() < 0 ||
	    ps2mcfs_init_fdcache() < 0 ||
	    ps2mcfs_init_dirent() < 0 ||
	    ps2mcfs_init_root() < 0)
		return -1;

	/*
	 * hook block device read/write routine
	 */
	if (ps2mc_blkrw_hook == NULL)
		ps2mc_blkrw_hook = ps2mcfs_blkrw;

	/*
	 * create and start thread
	 */
	{
            struct semaphore sem = MUTEX_LOCKED;
            
            thread_sem = &sem;
	    kernel_thread(ps2mcfs_thread, NULL, 0);

	    /* wait the thread ready */
            down(&sem);
            thread_sem = NULL;
	}
                
	return register_filesystem(&ps2mcfs_fs_type);
}

static void
ps2mcfs_cleanup()
{
	TRACE("ps2mcfs_cleanup()\n");

	/*
	 * un-hook block device read/write routine
	 */
	if (ps2mc_blkrw_hook == ps2mcfs_blkrw)
		ps2mc_blkrw_hook = NULL;

	/*
	 * stop the thread
	 */
	if (thread_task != NULL) {
            struct semaphore sem = MUTEX_LOCKED;
            
            thread_sem = &sem;
            send_sig(SIGKILL, thread_task, 1);

	    /* wait the thread exit */
            down(&sem);
            thread_sem = NULL;
	}

	unregister_filesystem(&ps2mcfs_fs_type);
	ps2mcfs_exit_root();
	ps2mcfs_exit_pathcache();
	ps2mcfs_exit_fdcache();
	ps2mcfs_exit_filebuf();
}

static int
ps2mcfs_thread(void *arg)
{

	DPRINT(DBG_INFO, "start thread\n");

	lock_kernel();

	/*
	 * If we were started as result of loading a module, close all of the
	 * user space pages.  We don't need them, and if we didn't close them
	 * they would be locked into memory.
	 */
	exit_mm(current);

	current->session = 1;
	current->pgrp = 1;

        /*
         * FIXME this is still a child process of the one that did the insmod.
         * This needs to be attached to task[0] instead.
         */
	siginitsetinv(&current->blocked,
		      sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGTERM));
        current->fs->umask = 0;

	/*
	 * Set the name of this process.
	 */
	sprintf(current->comm, "ps2mcfs");
        
	unlock_kernel();

	thread_task = current;
	if (thread_sem != NULL)
		up(thread_sem); /* notify that we are ready */

	/*
	 * loop
	 */
	while(1) {
		ps2mcfs_check_fd();

		interruptible_sleep_on_timeout(&thread_wq,
					       PS2MCFS_CHECK_INTERVAL);

		if (signal_pending(current) )
			break;
	}

	DPRINT(DBG_INFO, "exit thread\n");

	thread_task = NULL;
	if (thread_sem != NULL)
		up(thread_sem); /* notify that we've exited */

	return (0);
}

/*
 * module stuff
 */
#ifdef MODULE
int
init_module(void)
{
	int res;

	DPRINT(DBG_INFO, "load\n");
	if ((res = ps2mcfs_init()) < 0) {
		ps2mcfs_cleanup();
		return res;
	}

	return 0;
}

void
cleanup_module(void)
{
	DPRINT(DBG_INFO, "unload\n");
	ps2mcfs_cleanup();
}
#endif /* MODULE */
