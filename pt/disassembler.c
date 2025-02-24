/*
 * *
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later 
 */


#include "debug.h"
#include "pt/disassembler.h"
#include "qemu/log.h"
#include "pt/memory_access.h"

#define LOOKUP_TABLES		5
#define IGN_MOD_RM			0
#define IGN_OPODE_PREFIX	0
#define MODRM_REG(x)		(x << 3)
#define MODRM_AND			0b00111000

#define limit_check(prev, next, limit) (!((limit >= prev) & (limit <= next)))
#define out_of_bounds(self, addr) ((addr < self->min_addr) | (addr > self->max_addr))

#define FAST_ARRAY_LOOKUP

#ifdef FAST_ARRAY_LOOKUP
uint64_t* lookup_area = NULL;
#endif
cofi_ins cb_lookup[] = {
	{X86_INS_JAE,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_JA,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_JBE,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_JB,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_JCXZ,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_JECXZ,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_JE,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_JGE,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JG,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JLE,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JL,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JNE,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JNO,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JNP,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JNS,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JO,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JP,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JRCXZ,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
    {X86_INS_JS,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_LOOP,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_LOOPE,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_LOOPNE,	IGN_MOD_RM,	IGN_OPODE_PREFIX},
};

/* unconditional direct branch */
cofi_ins udb_lookup[] = {
	{X86_INS_JMP,		IGN_MOD_RM,	0xe9},
	{X86_INS_JMP,		IGN_MOD_RM, 0xeb},
	{X86_INS_CALL,		IGN_MOD_RM,	0xe8},	
};

/* indirect branch */
cofi_ins ib_lookup[] = {
	{X86_INS_JMP,		MODRM_REG(4),	0xff},
	{X86_INS_CALL,		MODRM_REG(2),	0xff},	
};

/* near ret */
cofi_ins nr_lookup[] = {
	{X86_INS_RET,		IGN_MOD_RM,	0xc3},
	{X86_INS_RET,		IGN_MOD_RM,	0xc2},
};
 
/* far transfers */ 
cofi_ins ft_lookup[] = {
	{X86_INS_INT3,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_INT,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_INT1,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_INTO,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_IRET,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_IRETD,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_IRETQ,		IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_JMP,		IGN_MOD_RM,		0xea},
	{X86_INS_JMP,		MODRM_REG(5),	0xff},
	{X86_INS_CALL,		IGN_MOD_RM,		0x9a},
	{X86_INS_CALL,		MODRM_REG(3),	0xff},
	{X86_INS_RET,		IGN_MOD_RM,		0xcb},
	{X86_INS_RET,		IGN_MOD_RM,		0xca},
	{X86_INS_SYSCALL,	IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_SYSENTER,	IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_SYSEXIT,	IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_SYSRET,	IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_VMLAUNCH,	IGN_MOD_RM,	IGN_OPODE_PREFIX},
	{X86_INS_VMRESUME,	IGN_MOD_RM,	IGN_OPODE_PREFIX},
};

uint16_t cmp_lookup[] = {
	X86_INS_CMP,
	X86_INS_CMPPD,
	X86_INS_CMPPS,
	X86_INS_CMPSB,
	X86_INS_CMPSD,
	X86_INS_CMPSQ,
	X86_INS_CMPSS,
	X86_INS_CMPSW,
	X86_INS_CMPXCHG16B,
	X86_INS_CMPXCHG,
	X86_INS_CMPXCHG8B,
};


cofi_ins* lookup_tables[] = {
	cb_lookup,
	udb_lookup,
	ib_lookup,
	nr_lookup,
	ft_lookup,
};

uint8_t lookup_table_sizes[] = {
	22,
	3,
	2,
	2,
	19
};

/* ===== kAFL disassembler cofi list ===== */

static cofi_list* create_list_head(void){
	cofi_list* head = malloc(sizeof(cofi_list));
	if (head != NULL){
		head->list_ptr = NULL;
		head->cofi_ptr = NULL;
		head->cofi_target_ptr = NULL;
		//head->cofi = NULL;
		head->cofi.type = NO_DISASSEMBLY;
		return head;
	}
	return NULL;
}

static void free_list(cofi_list* head){
	cofi_list *tmp1, *tmp2;
	tmp1 = head;
	while (1){
		tmp2 = tmp1;
		if(tmp1 == NULL){
			break;
		}
		tmp1 = tmp1->list_ptr;
		//if (tmp2->cofi != NULL){
		//	free(tmp2->cofi);
		//}
		free(tmp2);
	}
}

static cofi_list* new_list_element(cofi_list* predecessor){ //, cofi_header* cofi){
	if(predecessor){
		cofi_list* next = malloc(sizeof(cofi_list));
		if (next){
			predecessor->list_ptr = next;
			next->list_ptr = NULL;
			next->cofi_ptr = NULL;
			next->cofi_target_ptr = NULL;
			//next->cofi = cofi;
			next->cofi.type = NO_DISASSEMBLY;
			return next;
		}
	}
	return NULL;
}

static void edit_cofi_ptr(cofi_list* element, cofi_list* target){
	if (element){
		element->cofi_ptr = target;
	}
}

/* ===== kAFL disassembler hashmap ===== */

#ifdef FAST_ARRAY_LOOKUP
static void map_put(disassembler_t* self, uint64_t addr, uint64_t ref){
	lookup_area[self->max_addr-addr] = ref;
}

static int map_get(disassembler_t* self, uint64_t addr, uint64_t* ref){
	*ref = lookup_area[self->max_addr-addr];
	return !(*ref);
}
#else
static void map_put(disassembler_t* self, uint64_t addr, uint64_t ref){
	int ret;
	khiter_t k;
	k = kh_put(ADDR0, self->map, addr, &ret); 
	kh_value(self->map, k) = ref;
}

static int map_get(disassembler_t* self, uint64_t addr, uint64_t* ref){
	khiter_t k;
	k = kh_get(ADDR0, self->map, addr); 
	if(k != kh_end(self->map)){
		*ref = kh_value(self->map, k); 
		return 0;
	} 
	return 1;
}
#endif

/* ===== kAFL disassembler engine ===== */

static inline uint64_t fast_strtoull(const char *hexstring){
	uint64_t result = 0;
	uint8_t i = 0;
	if (hexstring[1] == 'x' || hexstring[1] == 'X')
		i = 2;
	for (; hexstring[i]; i++)
		result = (result << 4) + (9 * (hexstring[i] >> 6) + (hexstring[i] & 017));
	return result;
}

static inline uint64_t hex_to_bin(char* str){
	//return (uint64_t)strtoull(str, NULL, 16);
	return fast_strtoull(str);
}

static cofi_type opcode_analyzer(disassembler_t* self, cs_insn *ins){
	uint8_t i, j;
	cs_x86 details = ins->detail->x86;
	
	for (i = 0; i < LOOKUP_TABLES; i++){
		for (j = 0; j < lookup_table_sizes[i]; j++){
			if (ins->id == lookup_tables[i][j].opcode){
				
				/* check MOD R/M */
				if (lookup_tables[i][j].modrm != IGN_MOD_RM && lookup_tables[i][j].modrm != (details.modrm & MODRM_AND))
						continue;	
						
				/* check opcode prefix byte */
				if (lookup_tables[i][j].opcode_prefix != IGN_OPODE_PREFIX && lookup_tables[i][j].opcode_prefix != details.opcode[0])
						continue;
#ifdef DEBUG
				/* found */
				//printf("%lx (%d)\t%s\t%s\t\t", ins->address, i, ins->mnemonic, ins->op_str);
				//print_string_hex("      \t", ins->bytes, ins->size);
#endif
				return i;
				
			}
		}
	}
	return NO_COFI_TYPE;
}

int get_capstone_mode(CPUState *cpu){
	switch(cpu->disassembler_word_width){
		case 64: 
			return CS_MODE_64;
		case 32: 
			return CS_MODE_32;
		default:
			assert(false);
	}
}

static cofi_list* analyse_assembly(disassembler_t* self, uint64_t base_address){
	csh handle;
	cs_insn *insn;
	cofi_type type;
  //cofi_header* tmp = NULL;
	uint64_t tmp_list_element = 0;
	bool last_nop = false;
	uint64_t cofi = 0;
	uint8_t* code = NULL;
	uint8_t tmp_code[x86_64_PAGE_SIZE*2];
	size_t code_size = 0;
	uint64_t address = base_address;
	cofi_list* predecessor = NULL;
	cofi_list* first = NULL;
  	//bool abort_disassembly = false;

	code_size = x86_64_PAGE_SIZE - (address & ~x86_64_PAGE_MASK);
	if (!read_virtual_memory(address, tmp_code, code_size, self->cpu))
		return NULL;
	if (code_size < 15) {
		// instruction may continue onto next page, try reading it..
		if (read_virtual_memory(address, tmp_code, code_size + x86_64_PAGE_SIZE, self->cpu))
			code_size += x86_64_PAGE_SIZE;
	}
	code = tmp_code;

	assert(cs_open(CS_ARCH_X86, get_capstone_mode(self->cpu), &handle) == CS_ERR_OK);
	cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
	// parse unrecognized instructions as data (endbr32/endbr64)
	cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
	insn = cs_malloc(handle);

	QEMU_PT_DEBUG(DISASM_PREFIX, "Analyse ASM: %lx (%zd), max_addr=%lx", address, code_size, self->max_addr);

	while(cs_disasm_iter(handle, (const uint8_t**)&code, &code_size, &address, insn)) {	

		QEMU_PT_DEBUG(DISASM_PREFIX, "Loop: %lx:\t%s\t%s, last_nop=%d", insn->address, insn->mnemonic, insn->op_str, last_nop);

		if (insn->address > self->max_addr){
			break;
		}
			
		type = opcode_analyzer(self, insn);
		
		if (!last_nop){
			if (cofi)
				predecessor = self->list_element;

			self->list_element = new_list_element(self->list_element);
			self->list_element->cofi.type = NO_COFI_TYPE;
			self->list_element->cofi.ins_addr = insn->address;
			self->list_element->cofi.ins_size = insn->size;
			self->list_element->cofi.target_addr = 0;

			edit_cofi_ptr(predecessor, self->list_element);
		}
		
		if (!map_get(self, insn->address, (uint64_t *)&tmp_list_element)){
			if(((cofi_list *)tmp_list_element)->cofi_ptr){
				edit_cofi_ptr(self->list_element, (cofi_list *)tmp_list_element);
				break;
			} else {
				self->list_element = (cofi_list *)tmp_list_element;
			}
		}
		
		if (type != NO_COFI_TYPE){
			cofi++;
			last_nop = false;
			self->list_element->cofi.type = type;
			self->list_element->cofi.ins_addr = insn->address;
			self->list_element->cofi.ins_size = insn->size;
			if (type == COFI_TYPE_CONDITIONAL_BRANCH || type == COFI_TYPE_UNCONDITIONAL_DIRECT_BRANCH){
				self->list_element->cofi.target_addr = hex_to_bin(insn->op_str);	
			} else {
				self->list_element->cofi.target_addr = 0;
			}
			//self->list_element->cofi = tmp;
			map_put(self, self->list_element->cofi.ins_addr, (uint64_t)(self->list_element));
			//if(type == COFI_TYPE_INDIRECT_BRANCH || type == COFI_TYPE_NEAR_RET || type == COFI_TYPE_FAR_TRANSFERS){
				//don't disassembly through ret and similar instructions to avoid disassembly inline data
				//however we need to finish the cofi ptr datatstructure therefore we take a second loop iteration and abort
				//after last_nop = false ist handeled
				//abort_disassembly = true;
				//QEMU_PT_DEBUG(DISASM_PREFIX, "ABORT_ASSEMBLY=TRUE");
			//}
		} else {
			last_nop = true;
			map_put(self, insn->address, (uint64_t)(self->list_element));
		}
		
		if (!first){
			first = self->list_element;
		}

		//if (abort_disassembly){
		//	break;
		//}
	}
	
	cs_free(insn, 1);
	cs_close(&handle);
	return first;
}
disassembler_t* init_disassembler(CPUState *cpu, uint64_t min_addr, uint64_t max_addr, void (*pt_bitmap)(uint64_t)){
	disassembler_t* res = malloc(sizeof(disassembler_t));
	res->cpu = cpu;
	res->min_addr = min_addr;
	res->max_addr = max_addr;
	res->handler = pt_bitmap;
	res->list_head = create_list_head();
	res->list_element = res->list_head;
	res->has_pending_indirect_branch = false;
	res->pending_indirect_branch_src = 0;

#ifdef FAST_ARRAY_LOOKUP
	assert((max_addr-min_addr) <= (128 << 20)); /* up to 128MB trace region (results in 512MB lookup table...) */
	lookup_area = malloc(sizeof(uint64_t) * (max_addr-min_addr));
	memset(lookup_area, 0x00, (sizeof(uint64_t) * (max_addr-min_addr)));
#else
	res->map = kh_init(ADDR0);
#endif

	return res;
}

void destroy_disassembler(disassembler_t* self){
#ifdef FAST_ARRAY_LOOKUP
	free(lookup_area);
#else
	kh_destroy(ADDR0, self->map);
#endif
	free_list(self->list_head);
	free(self);
}

static inline cofi_list* get_obj(disassembler_t* self, uint64_t entry_point){
	cofi_list *tmp_obj;

	if (out_of_bounds(self, entry_point)){
		assert(false);
		return NULL;
	}

	if(map_get(self, entry_point, (uint64_t *)&tmp_obj)){
		tmp_obj = analyse_assembly(self, entry_point);
	}

	// Decoding can fail on code read or decoding errors
	// Fuzzing will usually still work but traces may not be accurate.
	if (!tmp_obj || !tmp_obj->cofi_ptr)
		return NULL;

	return tmp_obj;
}

void disassembler_flush(disassembler_t* self){
  self->has_pending_indirect_branch = false;
  self->pending_indirect_branch_src = 0;
}

void inform_disassembler_target_ip(disassembler_t* self, uint64_t target_ip){
  if(self->has_pending_indirect_branch){
  	disassembler_flush(self);
  }
}

//#define DEBUG_TRACE_RETURN
#ifndef DEBUG_TRACE_RETURN
#define check_return(msg) do { return true; } while (0)
#else
#define check_return(msg) \
	do { \
		if (count_tnt(tnt_cache_state)) { \
			WRITE_SAMPLE_DECODED_DETAILED("Error %s\n", msg); \
			printf("Trap %s: in trace_disassembler()\n", msg); \
			asm("int $3\r\n"); \
			return false; \
		} \
		return true; \
	} while (0)
#endif

 __attribute__((hot)) bool trace_disassembler(disassembler_t* self, uint64_t entry_point, uint64_t limit, tnt_cache_t* tnt_cache_state, uint64_t fup_tip){

	cofi_list *obj, *last_obj;
		
	inform_disassembler_target_ip(self, entry_point);

	obj = get_obj(self, entry_point);

	if (!obj || !limit_check(entry_point, obj->cofi.ins_addr, limit)){
		check_return("1");
	}
	if (likely(entry_point != fup_tip))
		self->handler(entry_point);

	while(true){
		
		if (!obj) return false;

		switch(obj->cofi.type){

			case COFI_TYPE_CONDITIONAL_BRANCH:
				switch(process_tnt_cache(tnt_cache_state)){

					case TNT_EMPTY:
						WRITE_SAMPLE_DECODED_DETAILED("(%d)\t%%lx\tCACHE EMPTY\n", COFI_TYPE_CONDITIONAL_BRANCH, obj->cofi.ins_addr);
						return false;

					case TAKEN:
						WRITE_SAMPLE_DECODED_DETAILED("(%d)\t%lx\t(Taken)\n", COFI_TYPE_CONDITIONAL_BRANCH, obj->cofi.ins_addr);
						last_obj = obj;
						self->handler(obj->cofi.target_addr);
						if(!obj->cofi_target_ptr){
							obj->cofi_target_ptr = get_obj(self, obj->cofi.target_addr);
						}
						obj = obj->cofi_target_ptr;

						if (!obj || !limit_check(last_obj->cofi.target_addr, obj->cofi.ins_addr, limit)){
							check_return("2");
						}
						break;
					case NOT_TAKEN:
						WRITE_SAMPLE_DECODED_DETAILED("(%d)\t%lx\t(Not Taken)\n", COFI_TYPE_CONDITIONAL_BRANCH ,obj->cofi.ins_addr);

						last_obj = obj;
						self->handler((obj->cofi.ins_addr)+obj->cofi.ins_size);
						/* fix if cofi_ptr is null */
    					if(!obj->cofi_ptr){
    						obj->cofi_ptr = get_obj(self, obj->cofi.ins_addr+obj->cofi.ins_size);
    					}
						obj = obj->cofi_ptr;

						if(!obj || !limit_check(last_obj->cofi.ins_addr, obj->cofi.ins_addr, limit)){
							check_return("3");
						}
						break;
				}
				break;

			case COFI_TYPE_UNCONDITIONAL_DIRECT_BRANCH:
				WRITE_SAMPLE_DECODED_DETAILED("(%d)\t%lx\n", COFI_TYPE_UNCONDITIONAL_DIRECT_BRANCH ,obj->cofi.ins_addr);
				last_obj = obj;
				if(!obj->cofi_target_ptr){
					obj->cofi_target_ptr = get_obj(self, obj->cofi.target_addr);
				}
				obj = obj->cofi_target_ptr;

				if(!obj || !limit_check(last_obj->cofi.target_addr, obj->cofi.ins_addr, limit)){
					check_return("4");
				}
				break;

			case COFI_TYPE_INDIRECT_BRANCH:
				self->handler(obj->cofi.ins_addr); //BROKEN, TODO move to inform_disassembler_target_ip
				
				WRITE_SAMPLE_DECODED_DETAILED("(2)\t%lx\n",obj->cofi.ins_addr);
				return true;

			case COFI_TYPE_NEAR_RET:
				WRITE_SAMPLE_DECODED_DETAILED("(3)\t%lx\n",obj->cofi.ins_addr);
				return true;

			case COFI_TYPE_FAR_TRANSFERS:
				WRITE_SAMPLE_DECODED_DETAILED("(4)\t%lx\n",obj->cofi.ins_addr);
				return true;

			case NO_COFI_TYPE:
				WRITE_SAMPLE_DECODED_DETAILED("(5)\t%lx\n",obj->cofi.ins_addr);

				if(!(obj->cofi_ptr) || !limit_check(obj->cofi.ins_addr, obj->cofi.ins_addr, limit)){
					check_return("(5)");
				}
				obj = obj->cofi_ptr;
				break;
			case NO_DISASSEMBLY:
				assert(false);
		}
	}

	assert(false);
	return false;
}
