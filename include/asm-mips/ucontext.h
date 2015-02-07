#ifndef __ASM_MIPS_UCONTEXT_H
#define __ASM_MIPS_UCONTEXT_H

/* from bits/sigset.h of glibc 2.2 */
#define _LIBC_SIGSET_NWORDS (1024 / (8 * sizeof (unsigned long int)))

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
	unsigned long	  pad[_LIBC_SIGSET_NWORDS - _NSIG_WORDS];
};

#endif /* __ASM_MIPS_UCONTEXT_H */
