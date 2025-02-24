/*
 * *
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later 
 */

#ifndef HYPERCALL_H
#define HYPERCALL_H

#define PAYLOAD_BUFFER_SIZE		26
#define PRINTK_PAYLOAD_SIZE		4

#define KAFL_MODE_64	0
#define KAFL_MODE_32	1
#define KAFL_MODE_16	2

typedef struct{
	uint64_t ip[4];
	uint64_t size[4];
	uint8_t enabled[4];
} kAFL_ranges; 

//#define PANIC_DEBUG

/*
 * Panic Notifier Payload (x86-64)
 * fa                      cli
 * 48 c7 c0 1f 00 00 00    mov    rax,0x1f
 * 48 c7 c3 08 00 00 00    mov    rbx,0x8
 * 48 c7 c1 00 00 00 00    mov    rcx,0x0
 * 0f 01 c1                vmcall
 * f4                      hlt
 */
#define PANIC_PAYLOAD_64 "\xFA\x48\xC7\xC0\x1F\x00\x00\x00\x48\xC7\xC3\x08\x00\x00\x00\x48\xC7\xC1\x00\x00\x00\x00\x0F\x01\xC1\xF4"

/*
 * Panic Notifier Payload (x86-32)
 * fa                      cli
 * b8 1f 00 00 00          mov    $0x1f,%eax
 * bb 08 00 00 00          mov    $0x8,%ebx
 * b9 00 00 00 00          mov    $0x0,%ecx
 * 0f 01 c1                vmcall
 * f4                      hlt
 */
#define PANIC_PAYLOAD_32 "\xFA\xB8\x1F\x00\x00\x00\xBB\x08\x00\x00\x00\xB9\x00\x00\x00\x00\x0F\x01\xC1\xF4"

/*
 * KASAN Notifier Payload (x86-64)
 * fa                      cli
 * 48 c7 c0 1f 00 00 00    mov    rax,0x1f
 * 48 c7 c3 09 00 00 00    mov    rbx,0x9
 * 48 c7 c1 00 00 00 00    mov    rcx,0x0
 * 0f 01 c1                vmcall
 * f4                      hlt
 */
#define KASAN_PAYLOAD_64 "\xFA\x48\xC7\xC0\x1F\x00\x00\x00\x48\xC7\xC3\x09\x00\x00\x00\x48\xC7\xC1\x00\x00\x00\x00\x0F\x01\xC1\xF4"

/*
 * KASAN Notifier Payload (x86-32)
 * fa                      cli
 * b8 1f 00 00 00          mov    $0x1f,%eax
 * bb 09 00 00 00          mov    $0x9,%ebx
 * b9 00 00 00 00          mov    $0x0,%ecx
 * 0f 01 c1                vmcall
 * f4                      hlt
 */
#define KASAN_PAYLOAD_32 "\xFA\xB8\x1F\x00\x00\x00\xBB\x09\x00\x00\x00\xB9\x00\x00\x00\x00\x0F\x01\xC1\xF4"

/*
 * printk Notifier Payload (x86-64)
 * 0f 01 c1                vmcall
 * c3                      retn
 */
#define PRINTK_PAYLOAD "\x0F\x01\xC1\xC3"

void pt_setup_program(void* ptr);
void pt_setup_payload(void* ptr);
void pt_setup_snd_handler(void (*tmp)(char, void*), void* tmp_s);
void pt_setup_enable_hypercalls(void);

void pt_disable_wrapper(CPUState *cpu);

bool pt_hypercalls_enabled(void);

void hypercall_unlock(void);
void hypercall_reload(void);

void handle_hypercall_kafl_acquire(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_get_payload(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_get_program(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_release(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_cr3(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_submit_panic(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_submit_kasan(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_panic(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_kasan(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_timeout(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_snapshot(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_info(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_printf(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_printk_addr(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_printk(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_user_submit_mode(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_kafl_user_abort(struct kvm_run *run, CPUState *cpu);

/* IRPT */
void handle_hypercall_irpt_lock(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_irpt_ip_filtering(struct kvm_run *run, CPUState *cpu);
void handle_hypercall_irpt_memwrite(struct kvm_run *run, CPUState *cpu);

void hprintf(char* msg);
void enable_hprintf(void);
void enable_notifies(void);
void enable_reload_mode(void);
void pt_setup_disable_create_snapshot(void);

bool handle_hypercall_kafl_next_payload(struct kvm_run *run, CPUState *cpu);
void hypercall_reset_hprintf_counter(void);
bool hypercall_snd_char(char val);

#endif
