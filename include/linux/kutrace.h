/*
 * include/linux/kutrace.h
 *
 * Author: Richard Sites <dick.sites@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Signed-off-by: Richard Sites <dick.sites@gmail.com>
 */


/*
 * 2019.02.14 dsites hooks for kernel/user tracing
 * 2020.01.29 dsites scaffold PC sampling
 * 2021.04.08 dsites Move all MSR access to the loadable moduleg
 */

#ifndef _LINUX_KUTRACE_H
#define _LINUX_KUTRACE_H

#include <linux/types.h>

/* Updated 2019.03.03 to allow 64-bit syscalls 0..510 and */
/*  32-bit syscalls 512..1022 */

/* Take over last syscall number for controlling kutrace */
#define __NR_kutrace_control 1023

/* Take over last syscall64 number for tracing scheduler call/return */
#define KUTRACE_SCHEDSYSCALL 511

/* kutrace_control() commands */
#define KUTRACE_CMD_OFF 0
#define KUTRACE_CMD_ON 1
#define KUTRACE_CMD_FLUSH 2
#define KUTRACE_CMD_RESET 3
#define KUTRACE_CMD_STAT 4
#define KUTRACE_CMD_GETCOUNT 5
#define KUTRACE_CMD_GETWORD 6
#define KUTRACE_CMD_INSERT1 7
#define KUTRACE_CMD_INSERTN 8
#define KUTRACE_CMD_GETIPCWORD 9
#define KUTRACE_CMD_TEST 10
#define KUTRACE_CMD_VERSION 11


/* This is a *shortened* list of kernel-mode raw trace 12-bit event numbers */
/* See user-mode kutrace_lib.h for the full set */

/* Entry to provide names for PIDs */
#define KUTRACE_PIDNAME       0x002


// Specials are point events
#define KUTRACE_USERPID       0x200	/* Context switch: new PID */
#define KUTRACE_RPCIDREQ      0x201
#define KUTRACE_RPCIDRESP     0x202
#define KUTRACE_RPCIDMID      0x203
#define KUTRACE_RPCIDRXPKT    0x204
#define KUTRACE_RPCIDTXPKT    0x205
#define KUTRACE_RUNNABLE      0x206  /* Set process runnable: PID */
#define KUTRACE_IPI           0x207  /* Send IPI; receive is an interrupt */
#define KUTRACE_MWAIT         0x208	/* C-states */
#define KUTRACE_PSTATE        0x209	/* P-states */
#define KUTRACE_RX_PKT        0x214 	/* Raw packet received w/32-byte payload hash */ 
#define KUTRACE_TX_PKT        0x215 	/* Raw packet sent w/32-byte payload hash */


/* Entry to provide a PC sample at timer interrupts (profiling) */
#define KUTRACE_PC            0x280	/* added 2020.01.29 */


/* These are in blocks of 256 numbers */
#define KUTRACE_TRAP      0x0400     /* AKA fault */
#define KUTRACE_IRQ       0x0500
#define KUTRACE_TRAPRET   0x0600
#define KUTRACE_IRQRET    0x0700


/* These are in blocks of 512 numbers */
#define KUTRACE_SYSCALL64 0x0800
#define KUTRACE_SYSRET64  0x0A00
#define KUTRACE_SYSCALL32 0x0C00
#define KUTRACE_SYSRET32  0x0E00

/* Specific trap number for page fault */
#define KUTRACE_PAGEFAULT  14

/* Specific IRQ numbers. See arch/x86/include/asm/irq_vectors.h */
/* unneeded #define KUTRACE_LOCAL_TIMER_VECTOR		0xec */

/* Reuse the spurious_apic vector to show bottom halves exeuting */
#define KUTRACE_BOTTOM_HALF	255



/* Procedure interface to loadable module or compiled-in kutrace.c */
struct kutrace_ops {
	void (*kutrace_trace_1)(u64 num, u64 arg);
	void (*kutrace_trace_2)(u64 num, u64 arg1, u64 arg2);
	void (*kutrace_trace_many)(u64 num, u64 len, const char *arg);
	u64 (*kutrace_trace_control)(u64 command, u64 arg);
};

/* Packet filter parameters */
struct kutrace_nf {
	u64 hash_init;
	u64 hash_mask[3];
};

/* Per-cpu struct */
struct kutrace_traceblock {
	atomic64_t next;	/* Next u64 in current per-cpu trace block */
	u64 *limit;		/* Off-the-end u64 in current per-cpu block */
	u64 prior_cycles;	/* IPC tracking */
	u64 prior_inst_retired;	/* IPC tracking */
};


#ifdef CONFIG_KUTRACE
/* Global variables used by kutrace. Defined in kernel/kutrace/kutrace.c */
extern bool kutrace_tracing;
extern struct kutrace_ops kutrace_global_ops;
extern u64 *kutrace_pid_filter;
extern struct kutrace_nf kutrace_net_filter;

/* Insert pid name if first time seen. Races don't matter here. */
#define kutrace_pidname(next) \
	if (kutrace_tracing) { \
		pid_t pid16 = next->pid & 0xffff; \
		pid_t pid_hi = pid16 >> 6; \
		u64 pid_bit = 1ul << (pid16 & 0x3f); \
		if ((kutrace_pid_filter[pid_hi] & pid_bit) == 0) { \
			u64 name_entry[3]; \
			name_entry[0] = pid16; \
			memcpy(&name_entry[1], next->comm, 16); \
			(*kutrace_global_ops.kutrace_trace_many)( \
			 KUTRACE_PIDNAME, 3l, (const char*)&name_entry[0]); \
			kutrace_pid_filter[pid_hi] |= pid_bit; \
		} \
	}

/* Unconditionally insert or reset pid name. Races don't matter here. */
#define kutrace_pidrename(next) \
	if (kutrace_tracing) { \
		pid_t pid16 = next->pid & 0xffff; \
		pid_t pid_hi = pid16 >> 6; \
		u64 pid_bit = 1ul << (pid16 & 0x3f); \
		if (true) { \
			u64 name_entry[3]; \
			name_entry[0] = pid16; \
			memcpy(&name_entry[1], next->comm, 16); \
			(*kutrace_global_ops.kutrace_trace_many)( \
			 KUTRACE_PIDNAME, 3l, (const char*)&name_entry[0]); \
			kutrace_pid_filter[pid_hi] |= pid_bit; \
		} \
	}

/* Filter packet payload and if passes insert 32-byte hash into trace */
/* Mask first payload 24 bytes, XOR, and check for expected value */
/* ku_payload may well not be 8-byte aligned, but only 4-byte */
#define kutrace_pkttrace(rx_tx, ku_payload) \
do { \
	u64 hash = kutrace_net_filter.hash_init; \
	hash ^= (ku_payload[0] & kutrace_net_filter.hash_mask[0]); \
	hash ^= (ku_payload[1] & kutrace_net_filter.hash_mask[1]); \
	hash ^= (ku_payload[2] & kutrace_net_filter.hash_mask[2]); \
	hash ^= (hash >> 32); \
	hash &= 0x00000000ffffffffLLU;	/* The filter hash */ \
	if (hash == 0) { \
		/* We passed the filter; now hash first 32 bytes and record */ \
		hash = ku_payload[0] ^ ku_payload[1] ^ ku_payload[2] ^ ku_payload[3]; \
		hash ^= (hash >> 32); \
		hash &= 0x00000000ffffffffLLU; \
		kutrace1(rx_tx, hash); \
	} \
} while(0);

#define	kutrace1(event, arg) \
	if (kutrace_tracing) { \
		(*kutrace_global_ops.kutrace_trace_1)(event, arg); \
	}

/* map_nr moves 32-bit syscalls 0x200..3FF to 0x400..5FF */
#define	kutrace_map_nr(nr) (nr + (nr & 0x200)) 

#else

#define	kutrace_pidname(next)
#define	kutrace_pidrename(next)
#define	kutrace1(event, arg)
#define	kutrace_map_nr(nr) (nr) 

#endif


#endif /* _LINUX_KUTRACE_H */

