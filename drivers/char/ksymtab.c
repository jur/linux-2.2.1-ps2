/*
 * ksymtab.c - kernel symbol support 
 *
 *        Copyright (C) 2000  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 *	 Usage: isnmod ksymtab.o [sysmap=SYSTEM_MAP_FILE_NAME]
 */

#include <linux/autoconf.h>

#ifndef MODULE
#error MUST BE MODULE
#endif

/*
 * 	Setup/Clean up Driver Module
 */


#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#define MODVERSIONS
#endif

#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif

#include <linux/init.h>

#include <linux/errno.h>	/* error codes */
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* file op. */
#include <linux/proc_fs.h>	/* proc fs file op. */
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <asm/io.h>
#include <asm/uaccess.h>	/* copy to/from user space */
#include <asm/page.h>		/* page size */
#include <asm/pgtable.h>	/* PAGE_READONLY */

#include <linux/ksymtab.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>

#include <linux/miscdevice.h>

#ifndef KSYMTAB_MINOR
#define KSYMTAB_MINOR	MISC_DYNAMIC_MINOR
#endif

static int minor=KSYMTAB_MINOR;
static char * sysmap=NULL;

MODULE_PARM(minor,"i");		/* as parameter on loaing */
MODULE_PARM(sysmap,"s");	/* as parameter on loaing */


#define MOD_STRLEN 60 /* see module's symbol struct <linux/module.h> */


/* imported from kernel */

extern struct module *module_list;
extern struct ksymtab_methods  ksymtab_methods;


/* exported methods */

static int get_entries_nr (void);	/* forward */
static unsigned long find_symbol (unsigned long addr, 
		char *symbol, int sz, struct module **mod); /* forward */


/*
 * File Operations table
 *	please refer <linux/fs.h> for other methods.
 */

static struct file_operations  fops; 


#ifndef KSYMTAB_DEVICE_NAME
#define  KSYMTAB_DEVICE_NAME "ksymtab"
#endif 


static 
int get_ksymtab_info(char *buf, char **start, off_t pos, int count, int wr);

static
struct proc_dir_entry proc_mod = {
	low_ino:0,		/* inode # will be dynamic assgined */
	namelen:sizeof(KSYMTAB_DEVICE_NAME)-1, 
	name:KSYMTAB_DEVICE_NAME,
	mode:(S_IFREG | S_IRUGO),	/* in <linux/stat.h> */
	nlink:1,		/* 1 for FILE, 2 for DIR */
	uid:0, gid:0,		/* owner and group */
	size:0, 		/* inode size */
	ops:NULL,		/* use default procs for leaf */
	get_info:get_ksymtab_info,
};

static struct ksymbols ksymbols; /* forward */
static int ksyms_is_valid(struct ksymbols *syms); /* forward */

/*
 * Caller of (*get_info)() is  proc_file_read() in fs/proc/generic.c
 */
static 
int
get_ksymtab_info(char *buf, 	/*  allocated area for info */
	       char **start, 	/*  return youown area if you allocate */
	       off_t pos,	/*  pos arg of vfs read */
	       int count,	/*  readable bytes */
	       int wr)		/*  1, for O_RDWR  */
{

/* SPRINTF does not exist in the kernel */
#define MY_BUFSIZE 256
#define MARGIN 16
	char mybuf[MY_BUFSIZE+MARGIN];

	int len;

	len = sprintf(mybuf, 	"System.map:    [%s]\n"
				"kernel symbols:[%s]\n"
				, sysmap
				, ksyms_is_valid(&ksymbols)
					? "OK" : "NA"
		      );
	if (len >= MY_BUFSIZE) mybuf[MY_BUFSIZE] = '\0';

	if ( pos+count >= len ) {
		count = len-pos;
	}
	memcpy (buf, mybuf+pos, count);
	return count;
}

static struct miscdevice ksymtab_dev = {
        KSYMTAB_MINOR,
        KSYMTAB_DEVICE_NAME,
        &fops
};

static int init_ksymtab(char *sysmap_f);	/* forward */
static void cleanup_ksymtab(void);		/* forward */
static void cleanup_methods(void);		/* forward */
static void init_methods(void);			/* forward */


int
init_module (void)
{
	volatile static int busy = 0;
	int result;

	if (tas(&busy)) {
		return  -EBUSY;
	}

	ksymtab_dev.minor = minor;
	result = misc_register(&ksymtab_dev);
	if (result < 0) {
		printk(KERN_WARNING 
		       KSYMTAB_DEVICE_NAME ": can't register device.\n");
		busy = 0;
		return result;
	}

#ifdef CONFIG_PROC_FS
	/*
	 * register /proc entry, if you want.
	 */
	result=proc_register(&proc_root, &proc_mod);
	if (result < 0)  {
		printk(KERN_WARNING 
		       KSYMTAB_DEVICE_NAME ": can't register proc. entry.\n");
		misc_deregister(&ksymtab_dev);
		busy = 0;
		return result;
	}
#endif

	if (sysmap) {
		result = init_ksymtab(sysmap);
		if (result < 0) {
			misc_deregister(&ksymtab_dev);
#ifdef CONFIG_PROC_FS
			(void) proc_unregister(&proc_root, proc_mod.low_ino);
#endif
			busy = 0;
			return result;
		}
	}

	init_methods();
	if (sysmap) {
		printk(KERN_INFO KSYMTAB_DEVICE_NAME 
			":loaded (sysmap=\"%s\").\n", sysmap);
	} else {
		printk(KERN_INFO KSYMTAB_DEVICE_NAME ":loaded.\n");
	}
	busy = 0;
	return 0;
}


void
cleanup_module (void)
{

	unsigned long save_flags;

	init_lock_once_only(&ksymtab_methods.once,
		&ksymtab_methods.lock);
	spin_lock_irqsave (&ksymtab_methods.lock, save_flags);

#ifdef CONFIG_PROC_FS
	/* unregister /proc entry */
	(void) proc_unregister(&proc_root, proc_mod.low_ino);
#endif
	cleanup_ksymtab();
	cleanup_methods();
	misc_deregister(&ksymtab_dev);
	printk(KERN_INFO KSYMTAB_DEVICE_NAME ":removed.\n");

	spin_unlock_irqrestore (&ksymtab_methods.lock, save_flags);
}

//========================================================================

/*
 * struct ksymbols and methods
 */

struct ksym_ent {
	unsigned long val;
	char *str;
};

struct ksymbols {
	int max_nr;
	int nr;
	char *strings_tbl ;
	struct ksym_ent (*tbl)[];
};

static struct ksymbols ksymbols = {0,};

static struct ksym_ent ksym_ent[8192];
static char ksym_strings[MOD_STRLEN*sizeof(ksym_ent)/sizeof(ksym_ent[0])];
static struct ksym_ent ksym_ent_mod[2048];

/* constructors and destructor */
static void
ksyms_destroy(struct ksymbols *syms)
{
	syms->max_nr = 0;
	syms->nr = 0;
	syms->tbl= 0;
}

static void
ksyms_init(struct ksymbols *syms)
{
	syms->max_nr = sizeof(ksym_ent)/sizeof(ksym_ent[0]);
	syms->nr=0;
	syms->strings_tbl=ksym_strings;
	syms->tbl=&ksym_ent;
}

static void
ksyms_init_mod(struct ksymbols *syms)
{
	syms->max_nr = sizeof(ksym_ent_mod)/sizeof(ksym_ent_mod[0]);
	syms->nr=0;
	syms->strings_tbl=0;
	syms->tbl=&ksym_ent_mod;
}

/* access methods */

inline static unsigned long
ksyms_get_val(struct ksymbols *syms, int i)
{
	return ((*syms->tbl)[i].val);
}

inline static char *
ksyms_get_str(struct ksymbols *syms, int i)
{
	return ((*syms->tbl)[i].str);
}

static int
ksyms_is_valid(struct ksymbols *syms)
{
	return (syms->max_nr);
}

inline static int
ksyms_get_nr(struct ksymbols *syms)
{
	return (syms->nr);
}

/* other methods */

/* swap entry */
inline static void
ksyms_swap(struct ksymbols *syms, int i, int j)
{
	struct ksym_ent t;

	t = (*syms->tbl)[i];
	(*syms->tbl)[i] = (*syms->tbl)[j];
	(*syms->tbl)[j] = t;
}


/* add a entry */
static int
ksyms_addent(struct ksymbols *syms, char *p, unsigned long val)
{
	if (syms->max_nr == syms->nr)
		return -1;
	(*syms->tbl)[syms->nr].val= val;
	(*syms->tbl)[syms->nr].str= p;
	if (syms->strings_tbl) {
		int i;
		(*syms->tbl)[syms->nr].str= syms->strings_tbl;
		strncpy(syms->strings_tbl, p, MOD_STRLEN);
		for (i=0; i<MOD_STRLEN; i++) {
			if (!*(syms->strings_tbl)) break;
			(syms->strings_tbl) ++;
		}
		*(syms->strings_tbl) = '\0';
		(syms->strings_tbl) ++;
	}
	(syms->nr) ++;
	return 0;
}

/* get the nearest index points to value which is LE key */
static int
ksyms_find(struct ksymbols *syms, unsigned long key)
{
        int mid;
	int l,u;

	l = 0;
	u = syms->nr-1;
	if (l>=u)
		return -1;

	while (1) {
		if ((u-l)<=1) {
			if (key < ksyms_get_val(syms, l)) {
				return l-1;
			} else if (key >= ksyms_get_val(syms, u)) {
				return u;
			}
			return l;
		}
		mid = (l + u) / 2;
		if (key < ksyms_get_val(syms, mid)) {
			u = mid;
		} else if (key > ksyms_get_val(syms, mid)) {
			l = mid + 1;
		} else {
			return mid;
		}
	}
}

/* get value of give symbol name */
static unsigned long
ksyms_get_symval(struct ksymbols *syms, char *name)
{
	int i;
	for (i=0; i<syms->nr; i++) {
		if (strncmp (name, ksyms_get_str(syms,i), MOD_STRLEN) == 0) 
			return ksyms_get_val(syms,i);
	}

	return 0;
}

/* check consistency with exported symbols */
static int
ksyms_check(struct ksymbols *syms)
{
	struct module *mod;
	struct module_symbol *msym;
	int i, j, k;

	for (mod = module_list; mod; mod = mod->next) {
		/* find kernel */
		if (!*mod->name)
			break;
	}
	if (!*mod->name) {
		unsigned long limit;
		limit = ksyms_get_symval(syms,"_etext");
		if (limit==0)  {
			return -EINVAL;
		}

		j = -1;
		msym = mod->syms;
		for (i = 0 ; i < mod->nsyms; msym++,i++) {
			int ok;
			/* ignore out of range */
			if (msym->value > limit)
				continue;

			/* find symbol */
			j = ksyms_find(syms, msym->value);

			/* ignore out of range */
			if (j<0) {
				printk(KERN_DEBUG KSYMTAB_DEVICE_NAME
					":ksyms_check: ignore symbol:"
					"%s:%lx\n", 
					msym->name, msym->value);
				continue;
			}

			/* check name and value*/
			ok = 0;
			k = j-1;
			/* check forward */
			while ( j < ksyms_get_nr(syms) && 
				ksyms_get_val(syms, j) == msym->value) {

				if (strncmp(ksyms_get_str(syms,j),
						msym->name, MOD_STRLEN) == 0) {
						ok = 1;
						break;
				}
				j ++;
			}
			if (!ok) {
				/* check backward */
				while ( k>=0 && 
				        ksyms_get_val(syms, k) == msym->value) {

					if (strncmp(ksyms_get_str(syms,k),
						msym->name, MOD_STRLEN) == 0) {
							ok = 1;
							break;
					}
					k --;
				}
				j = k;
			}
			if (!ok) 
				break;
		}
		if (i<mod->nsyms) {
			printk(KERN_ERR KSYMTAB_DEVICE_NAME
				":ksyms_check: found mismatch symbol:"
				"<exported>%s:%lx  <system.map>%s:%lx\n",
				msym->name, msym->value,
				j>=0 ? ksyms_get_str(syms, j) : "", 
				j>=0 ? ksyms_get_val(syms, j) : 0);
			return -EINVAL;
		}
		return 0;
	}
	printk(KERN_ERR KSYMTAB_DEVICE_NAME
		":ksyms_check: no exported symbol found.\n");
	return -EINVAL;
}


/* sort ksymbols (quick sort) */
static
int
ksyms_sort(struct ksymbols *syms)
{
	int start, stop;
	int i,j;
	unsigned long criterion ;
	int mid;

#define NSTACK 20
	static  struct {int start; 
			int stop;
	} stack[NSTACK];

	int sp = 0;
	int maxsp = 0;

	start=0;
	stop=syms->nr-1;


	while (1) {
		if (start >= stop) {
		    ret:
			if (!sp) { 
				return 0;
			}
			sp --;
			start = stack[sp].start;
			stop = stack[sp].stop;
			continue;
		}
		if (start +1 == stop ) {
			if (ksyms_get_val(syms, start) 
				> ksyms_get_val(syms, stop))
					ksyms_swap(syms, start, stop);
			goto ret;
		}

		mid = (start + stop)/2;
		criterion= ksyms_get_val(syms, mid);
		i = start;
		j = stop;
		ksyms_swap(syms, mid,stop);

		while (1) {
			for (; i<j; i++)
				if (ksyms_get_val(syms, i) >= criterion) break;
			for (; i<j; j--)
				if (ksyms_get_val(syms, j) < criterion) break;
			if (i>=j)
				break;
			ksyms_swap(syms, i, j);
		}

		if (i<stop)
			ksyms_swap(syms, stop, i);

		if (NSTACK <= sp ) {
			printk(KERN_ERR KSYMTAB_DEVICE_NAME
			":ksym_sort:stack over flow.\n");
			return -1; 
		}
		/* push next task to the stack */
		/* sort [i+1 .. stop] : .ge. criterion  */
		stack[sp].start = i+1;
		stack[sp].stop = stop;

		sp ++; if (maxsp<sp) maxsp = sp;
		/* sort [start .. i-1] : .le. criterion  */
		stop = i - 1;
	}

}

//========================================================================

/*
 * helpers to parse system.map file
 */

/* hex char to int, return -1 if failed */
inline static int x2i(char c)
{
	switch (c) {
		case '0'...'9':
			return c - '0';
			break;
		case 'a'...'f':
			return c - 'a'+10;
			break;
		case 'A'...'F':
			return c - 'A'+10;
			break;
	}
	return -1;
}

/* parse hex string  return 0 if failed, 
	otherwise pointer to a non-hex char. */
inline static char * parse_hex(char *p, unsigned long *val)
{
	char *q;
	int i;
	unsigned long v;
	
	q = p;
	v = 0;
	i = x2i(*p);
	while (i != -1) {
		v<<=4;
		v += i;
		p ++;
		i = x2i(*p);
	}
	if (p == q)
		return 0;
	*val = v;
	return p;
}

/* skip space, return 0, if EOL reached */
inline static char * skip_space(char *p)
{
	while (*p && ((*p==' ')|| (*p=='\t')))
		p ++;
	if (!*p)
		return 0;
	return p;
}

/* get char, return minus value if EOF reached */
int
get_char(struct file *fp)
{
	static unsigned char buf[4096];
	static unsigned char *buf_p;
	static int remains=0;
	if (!remains) {
		set_fs(KERNEL_DS);
		remains = fp->f_op->read(fp, (char *) &buf, sizeof(buf),
				&fp->f_pos);
		set_fs(USER_DS);
		if (remains<0)
			return remains;
		buf_p = buf;
	}
	remains --;
	return *(buf_p++);
}


/* get line, return 0 if EOF reached */
static
int
read_ln(struct file *fp, char *p, int sz)
{
	char *q;
	int c;

	q = p;
	c = get_char(fp);
	while ( c>0 && c != '\n') {
		if (sz>0) {
			*p = c;
			p++;
			sz --;
		}
		c = get_char(fp);
	}

	*p='\0';
	return p-q;
}


/* build the ksymbols */
static
int
read_and_build(struct file *fp, struct ksymbols *syms)
{
	static char lnbuf[512];
	int err = 0;
	char *p;
	unsigned long val;

	ksyms_init(syms);
	while (err = read_ln(fp, lnbuf, sizeof(lnbuf)), err>0) {
		lnbuf[sizeof(lnbuf)-1] = '\0';

		/* parse Symbol Value */
		p = parse_hex(lnbuf, &val);
		if (!p) continue;

		p = skip_space(p);
		if (!p) continue;

		/* parse Symbol Type */
		if ( (*p!='T') 
			&& (*p!='t') 
			&& (*p!='A') )
			continue;

		p = skip_space(p+1);
		if (!p) continue;

		err = ksyms_addent(syms, p, val);
		if (err) {
			printk(KERN_ERR KSYMTAB_DEVICE_NAME
				":ksymbols table over flow.\n");
			break;
		}
	}
	return err;
}


/* open file */
static
int
open_f(char *fname, struct file **fp)
{
	struct file *file;
	int fd;
	int error;

	lock_kernel();
	*fp = 0;
	fd = get_unused_fd();
	if (fd >= 0) {
		file = filp_open(fname, O_RDONLY, 0);
		error = PTR_ERR(file);
		if (IS_ERR(file)) {
			fd = error;
			goto out;
		}
		fd_install(fd, file);
		*fp = file;
	}
out:
	unlock_kernel();
	return fd;
}

/* close file */
static
int close_f(int fd, struct file *fp)
{
	int error;
	struct files_struct * files = current->files;

	lock_kernel();
	files->fd[fd] = NULL;
	put_unused_fd(fd);
	FD_CLR(fd, &files->close_on_exec);
	error = close_fp(fp, files);
	unlock_kernel();

	return error;
}

//========================================================================

/*
 * setup and cleanup ksymbols 
 */

static
int
init_ksymtab(char *sysmap_f)
{
	int fd=0;
	int error=0;
	struct file  *filp;


	fd = open_f(sysmap_f, &filp);
	if (fd<0) {
		printk(KERN_ERR KSYMTAB_DEVICE_NAME
			":Can't open system.map file \"%s\".\n", sysmap_f);
		error = fd;
		goto out;
	}

	error = read_and_build(filp, &ksymbols);
	if (error <0) {
		printk(KERN_ERR KSYMTAB_DEVICE_NAME
			":Fail on reading system.map file \"%s\".\n", sysmap_f);
		ksyms_destroy(&ksymbols);
		close_f(fd, filp);
		goto out;
	}
	
	close_f(fd, filp);

	/* check ksymbols */
	error = ksyms_check(&ksymbols);
	if (error) {
		printk(KERN_ERR KSYMTAB_DEVICE_NAME
			":Invalid system.map file \"%s\".\n", sysmap_f);
		ksyms_destroy(&ksymbols);
		goto out;
	}
out:
	return error;
}

static
void
cleanup_ksymtab(void)
{
	ksyms_destroy(&ksymbols);
}

//========================================================================

/*
 * setup and cleanup methods 
 */

static
void
init_methods(void)
{
	ksymtab_methods.internal_get_entries_nr = 
		(volatile int (*) (void)) get_entries_nr;
	ksymtab_methods.internal_find_symbol = 
		(volatile unsigned long (*) (unsigned long addr, 
			char *symbol, int sz, struct module **mod))
		find_symbol;
	ksymtab_methods.valid = 1;
}

static
void
cleanup_methods(void)
{
	ksymtab_methods.valid = 0;
	ksymtab_methods.internal_get_entries_nr = 0;
	ksymtab_methods.internal_find_symbol = 0;
}

//========================================================================

/*
 * exported methods
 */

/*
 *
 * unsigned long
 * find_symbol (unsigned long addr, 	   :address
 *		char *symbol, 		   :spcae for symbol name
 *		int sz, 		   :size of symbol name
 *		struct module **modp	   :module contains the symbol
 *		)
 *
 * Find the symbol corresponding to given address and return 
 * the symbol name, module and offset.
 * If no symbol is found, offset(i.e. return-value) is same as addr.
 */
static
unsigned long
find_symbol (unsigned long addr, 
		char *symbol, int sz, struct module **modp)
{
	struct ksymbols mod_syms;
	int str_sz = MOD_STRLEN>sz?sz:MOD_STRLEN;
	struct module *mod;
	unsigned long offset  = 0;
	int i;



	ksyms_init_mod(&mod_syms);
	for (mod = module_list; mod; mod = mod->next) {
		void *mod_begin; 
		void *mod_end;
		struct module_symbol *msym;
		int err;
		int n;

		mod_begin = (void *) mod;
		mod_end= (void *)mod_begin + mod->size;

		/* check out of range */
		if ( *mod->name && /* ! kernel */
			((mod_begin > (void*)addr) 
				|| ((void *)addr > mod_end )))
			continue;

		/* try to examin kernel symbol if available */
		if (ksyms_is_valid(&ksymbols) && !*mod->name)  {

			i = ksyms_find(&ksymbols, addr);
			if (i<0 ) continue;

			strncpy(symbol, ksyms_get_str(&ksymbols,i), str_sz);
			offset = (unsigned long) addr 
					- ksyms_get_val(&ksymbols,i);
			if (modp) {
				*modp= mod;
			}
			goto out;
		}

		/* build module symbol table */
		n =  mod->nsyms;
		msym = mod->syms;
		for (i = 0 ; i < n; i++) {
			err = ksyms_addent(&mod_syms, 
						(char *)msym->name, 
						msym->value);
			if (err) {
				printk(KERN_ERR KSYMTAB_DEVICE_NAME
					":module symbol table over flow.\n");
				break;
			}
			msym ++;
		}

		/* sort symbols by addr */
		err = ksyms_sort(&mod_syms);
		if (err) {
			break;
		}

#if 0 /* debug only */
		/* sanity check */
		for (i = 1 ; i < n; i++) {
			if (ksyms_get_val(&mod_syms, i-1) > 
					ksyms_get_val(&mod_syms, i)) {
				printk(KERN_DEBUG KSYMTAB_DEVICE_NAME
				       "Funny entry:%3d:%8.8lx:%3d:%8.8lx\n", 
					i-1, ksyms_get_val(&mod_syms, i-1),
					i, ksyms_get_val(&mod_syms, i));

				break;
			}
		}
#endif
		/* search */
		i = ksyms_find(&mod_syms, (unsigned long)addr);
		if (i<0) {
			if (modp)
				*modp = mod;
			strncpy(symbol, mod->name, str_sz);
			offset = (unsigned long) addr 
					- (unsigned long)mod_begin;
			goto out;
		}
		if (modp)
			*modp = mod;
		strncpy(symbol, ksyms_get_str(&mod_syms, i), str_sz);
		offset = (unsigned long) addr - ksyms_get_val(&mod_syms, i);
		goto out;
	}
	if (modp)
		*modp=0;
	strncpy(symbol, "(unknown)", str_sz);
	offset = addr;
out:
	ksyms_destroy(&mod_syms);
	return offset;
}

/*
 * int get_entries_nr(void)
 *
 * get the number of kernel's ksymbols entry.
 *
 */

static
int get_entries_nr(void)
{

	if (ksyms_is_valid(&ksymbols)){
		return ksyms_get_nr(&ksymbols);
	}
	return 0;

}
//========================================================================

/*
 * 	Device File Operations
 */


/*
 * Open and Close
 */

static int open_dev (struct inode *p_inode, struct file *p_file)
{
	
        if ( p_file->f_mode & FMODE_WRITE ) {
                return -EPERM;
        }
	
	/* 
	 * if you want store something for later processing, do it on
	 * p_file->private_data .
	 */
        MOD_INC_USE_COUNT;
        return 0;          /* success */
}

static int release_dev (struct inode *p_inode, struct file *p_file)
{
	MOD_DEC_USE_COUNT;
	return 0;
}




static
struct file_operations  fops = {
	/* ssize_t (*read) (struct file *, char *, size_t, loff_t *); */
	//read:read,
	/* int (*open) (struct inode *, struct file *); */
	open:open_dev,
	/* int (*release) (struct inode *, struct file *);*/
	release:release_dev, 
};
