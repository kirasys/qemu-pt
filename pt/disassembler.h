/*
 * *
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later 
 */


#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/time.h>
#include <inttypes.h>
#include <capstone/capstone.h>
#include <capstone/x86.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "qemu/osdep.h"
#include "pt/khash.h"
#include "pt/tnt_cache.h"
#include "pt/logger.h"

KHASH_MAP_INIT_INT(ADDR0, uint64_t)

typedef struct{
	uint16_t opcode;
	uint8_t modrm;
	uint8_t opcode_prefix;
} cofi_ins;

typedef enum cofi_types{
	COFI_TYPE_CONDITIONAL_BRANCH,
	COFI_TYPE_UNCONDITIONAL_DIRECT_BRANCH,
	COFI_TYPE_INDIRECT_BRANCH,
	COFI_TYPE_NEAR_RET,
	COFI_TYPE_FAR_TRANSFERS,
	NO_COFI_TYPE,
	NO_DISASSEMBLY,
} cofi_type;


typedef struct {
	uint64_t ins_addr;
	uint64_t target_addr;
	uint16_t ins_size;
	cofi_type type;
} cofi_header;

typedef struct cofi_list {
	struct cofi_list *list_ptr;
	struct cofi_list *cofi_ptr;
	struct cofi_list *cofi_target_ptr;
	cofi_header cofi;
} cofi_list;

typedef struct disassembler_s{
	CPUState *cpu;
	uint64_t min_addr;
	uint64_t max_addr;
	void (*handler)(uint64_t);
	khash_t(ADDR0) *map;
	cofi_list* list_head;
	cofi_list* list_element;
	bool debug;
	bool has_pending_indirect_branch;
	uint64_t pending_indirect_branch_src;
} disassembler_t;

disassembler_t* init_disassembler(CPUState *cpu, uint64_t min_addr, uint64_t max_addr, void (*handler)(uint64_t));

int get_capstone_mode(CPUState *cpu);
void disassembler_flush(disassembler_t* self);
void inform_disassembler_target_ip(disassembler_t* self, uint64_t target_ip);
 __attribute__((hot)) bool trace_disassembler(disassembler_t* self, uint64_t entry_point, uint64_t limit, tnt_cache_t* tnt_cache_state, uint64_t fup_tip);
void destroy_disassembler(disassembler_t* self);

#endif
