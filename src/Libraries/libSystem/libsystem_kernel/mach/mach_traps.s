/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 * 
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/*
 * Mach Operating System
 * Copyright (c) 1991,1990,1989,1988,1987,1986 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/*
 * This file ships from upstream as license header only - the real trap
 * stub bodies were never present in this source drop (confirmed: nm on
 * our built libsystem_kernel.a shows zero "trap" symbols at all, and the
 * upstream xnu-7195.121.3/libsyscall/mach/mach_traps.s snapshot is
 * byte-identical to this file's stub state). Every one of task_self_trap,
 * host_self_trap, thread_self_trap, mach_reply_port and mach_msg_trap
 * itself was undefined, meaning mach_msg() - and therefore all MIG IPC,
 * including the host/processor calls already relied on elsewhere in this
 * project - would fault the first time any of them actually got called.
 *
 * <mach/i386/syscall_sw.h>'s kernel_trap() macro (LEAF/END-based) isn't
 * usable as-is: LEAF/END aren't defined anywhere for x86_64 in this tree,
 * only for arm, and this file's plain ".s" extension means it is NOT run
 * through cpp (only ".S" is), so C #define macros/#include here are
 * silently never expanded. Use a real GNU-as .macro instead, and compute
 * the Mach-class negative trap number as a plain assembler expression
 * rather than via the C SYSCALL_CONSTRUCT_MACH() macro.
 *
 * Move the 4th arg from %rcx to %r10 (syscall clobbers %rcx as the return
 * address, so the x86_64 ABI's syscall stubs always relay the 4th argument
 * through %r10 instead). Traps needing a 7th/8th argument
 * (mach_msg_trap/mach_msg_overwrite_trap's `notify` port) drop it - real
 * xnu has long ignored that parameter kernel-side.
 */
.macro MACH_TRAP_STUB name, num
	.text
	.globl \name
	.p2align 4, 0x90
\name:
	movq	%rcx, %r10
	movl	$((1<<24)|(0x00FFFFFF & -(\num))), %eax
	syscall
	ret
.endm

MACH_TRAP_STUB __kernelrpc_mach_vm_allocate_trap, 10
MACH_TRAP_STUB __kernelrpc_mach_vm_purgable_control_trap, 11
MACH_TRAP_STUB __kernelrpc_mach_vm_deallocate_trap, 12
MACH_TRAP_STUB _task_dyld_process_info_notify_get_trap, 13
MACH_TRAP_STUB __kernelrpc_mach_vm_protect_trap, 14
MACH_TRAP_STUB __kernelrpc_mach_vm_map_trap, 15
MACH_TRAP_STUB __kernelrpc_mach_port_allocate_trap, 16
MACH_TRAP_STUB __kernelrpc_mach_port_deallocate_trap, 18
MACH_TRAP_STUB __kernelrpc_mach_port_mod_refs_trap, 19
MACH_TRAP_STUB __kernelrpc_mach_port_move_member_trap, 20
MACH_TRAP_STUB __kernelrpc_mach_port_insert_right_trap, 21
MACH_TRAP_STUB __kernelrpc_mach_port_insert_member_trap, 22
MACH_TRAP_STUB __kernelrpc_mach_port_extract_member_trap, 23
MACH_TRAP_STUB __kernelrpc_mach_port_construct_trap, 24
MACH_TRAP_STUB __kernelrpc_mach_port_destruct_trap, 25
MACH_TRAP_STUB _mach_reply_port, 26
MACH_TRAP_STUB _thread_self_trap, 27
MACH_TRAP_STUB _task_self_trap, 28
MACH_TRAP_STUB _host_self_trap, 29
MACH_TRAP_STUB _mach_msg_trap, 31
MACH_TRAP_STUB _mach_msg_overwrite_trap, 32
MACH_TRAP_STUB _semaphore_signal_trap, 33
MACH_TRAP_STUB _semaphore_signal_all_trap, 34
MACH_TRAP_STUB _semaphore_signal_thread_trap, 35
MACH_TRAP_STUB _semaphore_wait_trap, 36
MACH_TRAP_STUB _semaphore_wait_signal_trap, 37
MACH_TRAP_STUB _semaphore_timedwait_trap, 38
MACH_TRAP_STUB _semaphore_timedwait_signal_trap, 39
MACH_TRAP_STUB __kernelrpc_mach_port_get_attributes_trap, 40
MACH_TRAP_STUB __kernelrpc_mach_port_guard_trap, 41
MACH_TRAP_STUB __kernelrpc_mach_port_unguard_trap, 42
MACH_TRAP_STUB _mach_generate_activity_id, 43
MACH_TRAP_STUB _task_name_for_pid, 44
MACH_TRAP_STUB _task_for_pid, 45
MACH_TRAP_STUB _pid_for_task, 46
MACH_TRAP_STUB _macx_swapon, 48
MACH_TRAP_STUB _macx_swapoff, 49
MACH_TRAP_STUB _thread_get_special_reply_port, 50
MACH_TRAP_STUB _macx_triggers, 51
MACH_TRAP_STUB _macx_backing_store_suspend, 52
MACH_TRAP_STUB _macx_backing_store_recovery, 53
MACH_TRAP_STUB _pfz_exit, 58
MACH_TRAP_STUB _swtch_pri, 59
MACH_TRAP_STUB _swtch, 60
MACH_TRAP_STUB _thread_switch, 61
MACH_TRAP_STUB _clock_sleep_trap, 62
MACH_TRAP_STUB _host_create_mach_voucher_trap, 70
MACH_TRAP_STUB _mach_voucher_extract_attr_recipe_trap, 72
MACH_TRAP_STUB __kernelrpc_mach_port_type_trap, 76
MACH_TRAP_STUB __kernelrpc_mach_port_request_notification_trap, 77
MACH_TRAP_STUB _mach_timebase_info_trap, 89
MACH_TRAP_STUB _mach_wait_until_trap, 90
MACH_TRAP_STUB _mk_timer_create_trap, 91
MACH_TRAP_STUB _mk_timer_destroy_trap, 92
MACH_TRAP_STUB _mk_timer_arm_trap, 93
MACH_TRAP_STUB _mk_timer_cancel_trap, 94
MACH_TRAP_STUB _mk_timer_arm_leeway_trap, 95
MACH_TRAP_STUB _debug_control_port_for_pid, 96
MACH_TRAP_STUB _iokit_user_client_trap, 100
