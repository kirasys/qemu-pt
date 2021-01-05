/*
 * *
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PT_H
#define PT_H

void pt_sync(void);
void pt_reset_bitmap(void);
void pt_reset_coverage_map(void);
void pt_setup_bitmap(void* ptr);
void pt_setup_coverage_map(void* ptr);

void pt_turn_on_coverage_map(void);
void pt_turn_off_coverage_map(void);

int pt_enable(CPUState *cpu, bool hmp_mode);
int pt_disable(CPUState *cpu, bool hmp_mode);
int pt_enable_ip_filtering(CPUState *cpu, uint8_t addrn, uint64_t ip_a, uint64_t ip_b, bool hmp_mode);
int pt_disable_ip_filtering(CPUState *cpu, uint8_t addrn, bool hmp_mode);
int pt_set_cr3(CPUState *cpu, uint64_t val, bool hmp_mode);

void pt_kvm_init(CPUState *cpu);
void pt_pre_kvm_run(CPUState *cpu);
void pt_post_kvm_run(CPUState *cpu);

void pt_handle_overflow(CPUState *cpu);
void pt_dump(CPUState *cpu, int bytes);
void pt_bitmap(uint64_t addr);
#endif
