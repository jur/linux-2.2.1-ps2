/*
 * trace.c - mips stack backtrace support
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License Version 2. See the file "COPYING" in the main
 * directory of this archive for more details.
 *
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <asm/inst.h>

#define REG_K0	26
#define REG_K1	27
#define REG_RA	31
#define REG_FP	30
#define REG_SP	29
#define REG_GP	28

#define SYMBOL_STRLEN 100
#define INST_SIZE	(sizeof(union mips_instruction))

extern int sprint_symbol(char *str, int sz, unsigned long addr);

#ifdef DEBUG
static
void dump_stack(unsigned long sp)
{
	int i;
	for(i = -32; i < 32; i += 4 )
		printk("%s %8.8lx %8.8lx\n", i==0?">":" ",
			(sp + i), *(__u32 *)(sp + i));
}
#endif


/* return nozero if invalid */
static
inline
int check_alignment(unsigned long p)
{
	return (p & 0x3);
}

/* return nozero if invalid */
static
inline
int check_area(unsigned long p)
{
	static unsigned long kernel_start;
	static unsigned long kernel_end;
	static unsigned long module_start;
	static unsigned long module_end;

	kernel_start = KSEG0;
	kernel_end = KSEG1;
	module_start = VMALLOC_START;
	module_end = VMALLOC_END;

	return (!( (p >= kernel_start && p < kernel_end) ||
			(p >= module_start && p < module_end)))
		|| (p & 0x3) ;
}

static inline int __get_kword(__u32 *addr, __u32 *pc)
{
	__u32 val;
	int error = -EFAULT;

	asm("1:\n\t"
		"lw	%0, (%2);\n\t"
		"move	%1, $0;\n\t"
		".section	__ex_table,\"a\";\n\t"
		".word	1b, %3;\n\t"
		".previous;\n\t"
		: "=r"(val), "=r"(error) 
		: "r"(addr), "i"(&&out));
	*pc = val;
out:
	return error;
}

/*
 * Assume that caller have following codes...
 * 
 * 	jal	xxx 
 * 
 * or
 * 
 * 	lui	Rx, const
 * 	lw	Rx, offset(Rx)
 *	nop
 * 	jalr	Rx 
 */
static
void
verify_call_arc(unsigned long *callee, unsigned long caller)
{
	/* XXX: check caller had really call callee */
	union mips_instruction inst;
	int reg;
	__u32 addr;
	__u32 pc;
	int offset;

	if (check_area(caller)) goto unexpected;

	caller -= 2*INST_SIZE;
	inst = *(union mips_instruction *)(caller);

	// jal xxx
	if (inst.j_format.opcode == jal_op) {
		pc = (caller & 0xf0000000) 
			| (inst.j_format.target << 2);
		if ( pc != *callee ){
			addr = 0;
			goto changed;
		}
		return;
	} 
	//  jalr reg
	if (inst.r_format.opcode == spec_op
			&& inst.r_format.func == jalr_op
			&& inst.r_format.rd == REG_RA
			&& inst.r_format.re == 0
			&& inst.r_format.rt == 0) {

		reg = inst.r_format.rs;

		// skip nops
		do {
			caller -= INST_SIZE;
			if (check_area(caller)) goto unexpected;
			inst = *(union mips_instruction *)caller;
		} while ( inst.word == 0 );

		//	lw reg, xx(reg)
		if (!( inst.i_format.opcode == lw_op
				&& inst.u_format.rt == reg
				&& inst.u_format.rs == reg )) {
			goto unexpected;
		}
		offset = inst.i_format.simmediate;


		// skip nops
		do {
			caller -= INST_SIZE;
			if (check_area(caller)) goto unexpected;
			inst = *(union mips_instruction *)caller;
		} while ( inst.word == 0 );

		//	lui reg, xxx
		if (!( inst.u_format.opcode == lui_op
				&& inst.u_format.rt == reg
				&& inst.u_format.rs == 0 )) {
			goto unexpected;
		}
		addr = (inst.u_format.uimmediate << 16);
		addr = offset;

		if (__get_kword((__u32 *)addr, &pc) || check_area(pc))
			goto unexpected;
		if ( pc != *callee )
			goto changed;
		return;
	}
unexpected:
#ifdef DEBUG
	printk("verify_call_arc:unexpceted caller.\n");
#endif
	return;
changed:
#ifdef DEBUG
	printk( "verify_call_arc:change callee.(%lx -> %lx (* %lx))\n",
		*callee, (unsigned long)pc, (unsigned long)addr);
#endif
	*callee = pc;
	return;
}


/* return zero if invalid */
static
unsigned long
heuristic_find_proc_start(unsigned long pc)
{
	union mips_instruction inst;
	int found = 0;
	while (!check_area(pc)) {
		__u32 new_inst;
		if (__get_kword((__u32 *)pc, &new_inst))
			break;
		inst.word = new_inst;
		
		if ((inst.r_format.opcode == spec_op 
				&& inst.r_format.func == jr_op 
				&& inst.r_format.rs == REG_RA 
				&& inst.r_format.rd == 0 
				&& inst.r_format.re == 0 
				&& inst.r_format.rt == 0)	/* jr ra */
			|| (inst.r_format.opcode == spec_op 
				&& inst.r_format.func == jr_op 
				&& inst.r_format.rs == REG_K0 
				&& inst.r_format.rd == 0 
				&& inst.r_format.re == 0 
				&& inst.r_format.rt == 0)	/* jr k0 */
			|| (inst.r_format.opcode == spec_op 
				&& inst.r_format.func == jr_op 
				&& inst.r_format.rs == REG_K1 
				&& inst.r_format.rd == 0 
				&& inst.r_format.re == 0 
				&& inst.r_format.rt == 0)	/* jr k1 */
			|| (inst.r_format.opcode == cop0_op 
				&& inst.r_format.func == eret_op 
				&& inst.r_format.rs ==  cop_op
				&& inst.r_format.rd == 0 
				&& inst.r_format.re == 0 
				&& inst.r_format.rt == 0)	/* eret */
			|| (inst.u_format.opcode == beq_op 
				&& inst.u_format.rs ==  0
				&& inst.u_format.rt == 0
				&& inst.u_format.uimmediate == 0xffff))	
						/* beq zero,zero, self */
		{
			found = 1;
			break;
		}
			/* eret */
		pc -= INST_SIZE;
	}
	if (!found) {
		return 0;
	}
	/* skip ret itself and BD slot */
	pc += 2 * INST_SIZE;
	/* skip nops */
	while (  *(__u32 *)pc  == 0)
		pc += INST_SIZE;
	return pc;
}


/* return -1 as frame_size if invalid */
static
void
get_frame_info( unsigned long pc_start,
                unsigned long pc,
                unsigned long *ra,
                unsigned long *sp,
                unsigned long *fp,
		int *frame_size)
{
	union mips_instruction inst;
	char *errmsg;
	unsigned long eip;
	int ra_offset = 0;
	int fp_offset = 0;
	int sp_frame_size = 0;
	int fp_frame_size = 0;
	int frame_ptr = REG_SP;

	eip  = pc_start;

	while  (eip<pc) {
		__u32 new_inst;
		if (__get_kword((__u32 *)eip, &new_inst))
			break;
		inst.word = new_inst;
		if ((inst.i_format.opcode == addi_op 
				|| inst.i_format.opcode == addiu_op )
			&& inst.i_format.rs == REG_SP 
			&& inst.i_format.rt == REG_SP) {

			//addui sp,sp,-i, addi sp,sp, -i
			if (inst.i_format.simmediate > 0) break;
			sp_frame_size += -inst.i_format.simmediate;

		} else if (inst.i_format.opcode == sw_op 
			&& inst.i_format.rs == REG_SP 
			&& inst.i_format.rt == REG_RA) {

			//sw    ra,xx(sp);
			ra_offset = sp_frame_size - inst.i_format.simmediate;

		} else if (inst.i_format.opcode == sw_op 
			&& inst.i_format.rs == REG_FP 
			&& inst.i_format.rt == REG_RA) {

			//sw    ra,xx(fp);
			ra_offset = fp_frame_size - inst.i_format.simmediate;

		} else if (inst.i_format.opcode == sw_op 
			&& inst.i_format.rs == REG_SP 
			&& inst.i_format.rt == REG_FP) {

			//sw    fp,xx(sp);
			fp_offset = sp_frame_size - inst.i_format.simmediate;

		} else if ((inst.r_format.opcode == spec_op 
				&& inst.r_format.func == addu_op 
				&& inst.r_format.rd == REG_FP 
				&& inst.r_format.rs == REG_SP 
				&& inst.r_format.re == 0 
				&& inst.r_format.rt == 0) 
			   || (inst.r_format.opcode == spec_op 
				&& inst.r_format.func == or_op 
				&& inst.r_format.rd == REG_FP 
				&& inst.r_format.rs == REG_SP 
				&& inst.r_format.re == 0 
				&& inst.r_format.rt == 0) 
			   || (inst.r_format.opcode == spec_op 
				&& inst.r_format.func == daddu_op 
				&& inst.r_format.rd == REG_FP 
				&& inst.r_format.rs == REG_SP 
				&& inst.r_format.re == 0 
				&& inst.r_format.rt == 0)) {

			//move fp, sp
			//(addu fp,sp,zero, or fp,sp,zero OR daddu fp,sp,zero)
			fp_frame_size = sp_frame_size;
			frame_ptr = REG_FP;

		}
		eip += sizeof(inst);
	}

#ifdef DEBUG
	printk("===========\n");
	printk("ra_offset:%d\n", ra_offset);
	printk("fp_offset:%d\n", fp_offset);
	printk("sp_frame_size:%d\n", sp_frame_size);
	printk("fp_frame_size:%d\n", fp_frame_size);
	printk("frame_ptr:%d\n", frame_ptr);
	printk("sp:%lx\n", *sp);
	printk("fp:%lx\n", *fp);
#endif

	if (!sp_frame_size) {
		*frame_size = 0;
		goto out;
	}
	
	if ( frame_ptr == REG_SP) {
		*sp =  *sp + sp_frame_size;
		*frame_size = sp_frame_size;
	} else {
		*sp =  *fp + fp_frame_size;
		*frame_size = fp_frame_size;
	}
	if ( check_area(*sp)) {
		errmsg = "sp";
		goto error;
	}
	if (check_alignment(*frame_size)) {
		errmsg = "frame_size";
		goto error;
	}
#ifdef DEBUG
	dump_stack(*sp);
#endif
	if (ra_offset) {
		__u32 new_ra;

		if (__get_kword((__u32 *)(*sp - ra_offset), &new_ra)
			|| check_area(new_ra)){
			errmsg = "ra";
			goto error;
		}
		*ra =  new_ra;
	}
	if (fp_offset) {
		__u32 new_fp;

		if (__get_kword((__u32 *)(*sp - fp_offset), &new_fp)) {
			errmsg = "fp";
			goto error;
		}
		*fp =  new_fp;
	}

out:
#ifdef DEBUG
	printk("caller_fp:%lx\n", *fp);
	printk("caller_sp:%lx\n", *sp);
	printk("new_ra:%lx\n", *ra);
	printk("===========\n");
#endif
	return;
error:
#ifdef DEBUG
	printk("error:%s\n", errmsg);
	printk("===========\n");
#endif
	* frame_size = -1;
	return;
}

#ifdef DEBUG
static
void show_frame_size(unsigned long func, int frame_size)
{
	char callee_name[SYMBOL_STRLEN];

	callee_name[0]='\0';
	if ( !check_area(func) )
		sprint_symbol(callee_name, sizeof(callee_name)-1 ,func);
	printk("     frame size of [%8.8lx]:\"%s\": %d\n",
				func, callee_name, frame_size);
}
#endif

static
void show_pc(
		unsigned long pc,
		unsigned long sp,
		unsigned long fp)
{
	char name[SYMBOL_STRLEN];

	name[0]='\0';
	if ( !check_area(pc) )
		sprint_symbol(name, sizeof(name)-1, pc);

	printk("  [<%8.8lx>:%s]\n        sp:%8.8lx    fp:%8.8lx\n",
				pc,
				name,
				sp, fp);
}

static
void show_trace_pc(
		unsigned long func,
		unsigned long ra,
		unsigned long sp,
		unsigned long fp)
{
	char caller_name[SYMBOL_STRLEN];
	char callee_name[SYMBOL_STRLEN];

	ra -= 2*sizeof(union mips_instruction);
	callee_name[0]='\0';
	if ( !check_area(func) )
		sprint_symbol(callee_name, sizeof(callee_name)-1, func);
	caller_name[0]='\0';
	if ( !check_area(ra) )
		sprint_symbol(caller_name, sizeof(caller_name)-1, ra);

	printk("  [<%8.8lx>:%s] called by [<%8.8lx>:%s]\n",
				func,
				callee_name,
				ra,
				caller_name);
	printk("        sp:%8.8lx    fp:%8.8lx\n", sp, fp);
}

static
void
show_exception(struct pt_regs *regs, unsigned long callee)
{
	static const char *(exc_name[]) = {
		"Int", "mod", "TLBL", "TLBS",
		"AdEL", "AdES", "IBE", "DBE",
		"Syscall", "Bp", "RI", "CpU",
		"Ov", "TRAP", "VCEI", "FPE",
		"C2E", "(17)", "(18)", "(19)",
		"(20)", "(21)", "(22)", "Watch",
		"(24)", "(25)", "(26)", "(27)",
		"(28)", "(29)", "(30)", "VCED"};

	unsigned long pc = regs->cp0_epc;
	unsigned long ra = get_gpreg(regs, REG_RA);
	int exc_code = (regs->cp0_cause>>2) & 31;
	char name[SYMBOL_STRLEN];

	if (regs->cp0_cause>>31) {
		pc += sizeof(union mips_instruction);
	}

	name[0]='\0';
	if ( !check_area(callee) )
		sprint_symbol(name, sizeof(name)-1 ,callee);
	printk("  [<%8.8lx>:%s] called by exception.\n",callee, name);

	name[0]='\0';
	if ( !check_area(pc) )
		sprint_symbol(name, sizeof(name)-1 ,pc);
	printk("    EPC   : %08lx:%s\n", pc, name);

	name[0]='\0';
	if ( !check_area(ra) )
		sprint_symbol(name, sizeof(name)-1 ,ra);
	printk("    RA    : %08lx:%s\n", ra, name);
	printk("    GP    : %08lx    Status: %08lx\n",
			get_gpreg(regs, REG_GP),
			regs->cp0_status);
	printk("    Cause : %08lx    ExcCode:%s(%d)\n", 
			regs->cp0_cause, exc_name[exc_code],exc_code);

}

void
traceback(struct pt_regs *regs)
{
	int frame_size;
	unsigned long current_p;
	unsigned long stack_llim, stack_ulim;
	unsigned long pc, start_pc;
	unsigned long new_sp,new_ra,new_fp;

	if (regs) {
		current_p = get_gpreg(regs, REG_GP);
		new_ra = get_gpreg(regs, REG_RA);
		new_sp = get_gpreg(regs, REG_SP);
		new_fp = get_gpreg(regs, REG_FP);
		pc = (unsigned long)regs->cp0_epc;

	} else {
		current_p = (unsigned long) current;
		asm("\tmove %0,$sp" : "=r"(new_sp));
		asm("\tmove %0,$fp" : "=r"(new_fp));
		asm("\tmove %0,$31" : "=r"(new_ra));
		get_frame_info( (unsigned long)traceback,
			(unsigned long)&&here,
			&pc,
			&new_sp,
			&new_fp,
			&frame_size);
	    here:
	}
	stack_llim = current_p + sizeof(struct task_struct);
	stack_ulim = current_p + KERNEL_STACK_SIZE -32;
retry:
	start_pc = heuristic_find_proc_start(pc);
	show_pc(pc, new_sp, new_fp);
	while ((!check_area(pc)) && frame_size >= 0 
			&& new_sp >= stack_llim && new_sp <= stack_ulim) {
		get_frame_info( start_pc,
			pc,
			&new_ra,
			&new_sp,
			&new_fp,
			&frame_size);

		if (pc == new_ra) {
			break;
		}
		verify_call_arc(&start_pc, new_ra);
		show_trace_pc(start_pc, new_ra, new_sp, new_fp);

#ifdef DEBUG
		printk("func:%lx called from %lx(func:%lx) frame_size:%d\n", 
				start_pc, 
				new_ra,
				heuristic_find_proc_start(new_ra),
				frame_size);
#endif
		pc = new_ra;
		start_pc = heuristic_find_proc_start(pc);
	}
	if (frame_size == 0) {
		/* check stack */
		if (( new_sp + sizeof(struct pt_regs)) > stack_ulim)
			return;

		/* exception frame */
		regs = (struct pt_regs *)new_sp;
		current_p = get_gpreg(regs, REG_GP);
		stack_llim = current_p + sizeof(struct task_struct);
		stack_ulim = current_p + KERNEL_STACK_SIZE -32;

		show_exception(regs, start_pc);

		new_ra = get_gpreg(regs, REG_RA);
		new_sp = get_gpreg(regs, REG_SP);
		new_fp = get_gpreg(regs, REG_FP);
		pc = (unsigned long)regs->cp0_epc;
		if (regs->cp0_cause>>31) {
			pc += sizeof(union mips_instruction);
		}
		goto retry;
	}
}

void
traceback_me(void)
{
	traceback(0);
}

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct thread_struct *t)
{
	extern void ret_from_sys_call(void);
	unsigned long ra, sp, fp, caller;
	int frame_size;

	ra = (unsigned long) t->reg31;
	/* New born processes are a special case */
	if (ra == (unsigned long) ret_from_sys_call)
		return ra;

        sp = (unsigned long) t->reg29;
        fp = (unsigned long) t->reg30;
        caller = ra;
	get_frame_info( (unsigned long) schedule,
			caller,
			&ra,
			&sp,
			&fp,
			&frame_size);
        return ra;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched     ((unsigned long) scheduling_functions_start_here)
#define last_sched      ((unsigned long) scheduling_functions_end_here)

/*
 * Return caller of sleeping functions.
 */
unsigned long mips_get_wchan(struct task_struct *p)
{

	extern void ret_from_sys_call(void);
	unsigned long pc, start_pc;
	unsigned long ra, sp, fp, caller;
	int frame_size;
	struct thread_struct *t;
	unsigned long current_p;
	unsigned long stack_llim, stack_ulim;

	current_p = (unsigned long)p;
	stack_llim = current_p + sizeof(struct task_struct);
	stack_ulim = current_p + KERNEL_STACK_SIZE -32;
	t = &p->tss;

	ra = (unsigned long) t->reg31;
	/* New born processes are a special case */
	if (ra == (unsigned long) ret_from_sys_call)
		return ra;

        sp = (unsigned long) t->reg29;
        fp = (unsigned long) t->reg30;
        caller = ra;
	get_frame_info( (unsigned long) schedule,
			caller,
			&ra,
			&sp,
			&fp,
			&frame_size);

	pc = ra;
	while ((!check_area(pc)) && frame_size >= 0 
			&& sp >= stack_llim && sp <= stack_ulim
			&& first_sched <= pc && pc < last_sched ) {

		start_pc = heuristic_find_proc_start(pc);
		get_frame_info( start_pc,
			pc,
			&ra,
			&sp,
			&fp,
			&frame_size);
		pc = ra;
	}
	if (first_sched <= pc && pc < last_sched ) {
		return caller;
	}
	return pc;
}

