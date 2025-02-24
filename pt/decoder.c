/*
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later 
 *
 *
 * Note:
 *
 * This Intel PT software decoder is partially inspired and based on
 * Andi Kleen's fastdecode.c (simple-pt).
 * See: https://github.com/andikleen/simple-pt/blob/master/fastdecode.c
 *
 * Simple PT dumper
 *
 * Copyright (c) 2015, Intel Corporation
 * Author: Andi Kleen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#define _GNU_SOURCE 1
#include "pt/decoder.h"

#define LEFT(x) ((end - p) >= (x))
#define BIT(x) (1U << (x))

#define BENCHMARK 				1

#define PT_PKT_GENERIC_LEN		2
#define PT_PKT_GENERIC_BYTE0	0b00000010

#define PT_PKT_LTNT_LEN			8
#define PT_PKT_LTNT_BYTE0		PT_PKT_GENERIC_BYTE0
#define PT_PKT_LTNT_BYTE1		0b10100011

#define PT_PKT_PIP_LEN			8
#define PT_PKT_PIP_BYTE0		PT_PKT_GENERIC_BYTE0
#define PT_PKT_PIP_BYTE1		0b01000011

#define PT_PKT_CBR_LEN			4
#define PT_PKT_CBR_BYTE0		PT_PKT_GENERIC_BYTE0
#define PT_PKT_CBR_BYTE1		0b00000011

#define PT_PKT_OVF_LEN			8
#define PT_PKT_OVF_BYTE0		PT_PKT_GENERIC_BYTE0
#define PT_PKT_OVF_BYTE1		0b11110011

#define PT_PKT_PSB_LEN			16
#define PT_PKT_PSB_BYTE0		PT_PKT_GENERIC_BYTE0
#define PT_PKT_PSB_BYTE1		0b10000010

#define PT_PKT_PSBEND_LEN		2
#define PT_PKT_PSBEND_BYTE0		PT_PKT_GENERIC_BYTE0
#define PT_PKT_PSBEND_BYTE1		0b00100011

#define PT_PKT_MNT_LEN			11
#define PT_PKT_MNT_BYTE0		PT_PKT_GENERIC_BYTE0
#define PT_PKT_MNT_BYTE1		0b11000011
#define PT_PKT_MNT_BYTE2		0b10001000

#define PT_PKT_TMA_LEN			7
#define PT_PKT_TMA_BYTE0		PT_PKT_GENERIC_BYTE0
#define PT_PKT_TMA_BYTE1		0b01110011

#define PT_PKT_VMCS_LEN			7
#define PT_PKT_VMCS_BYTE0		PT_PKT_GENERIC_BYTE0
#define PT_PKT_VMCS_BYTE1		0b11001000

#define	PT_PKT_TS_LEN			2
#define PT_PKT_TS_BYTE0			PT_PKT_GENERIC_BYTE0
#define PT_PKT_TS_BYTE1			0b10000011

#define PT_PKT_MODE_LEN			2
#define PT_PKT_MODE_BYTE0		0b10011001

#define PT_PKT_TIP_LEN			8
#define PT_PKT_TIP_SHIFT		5
#define PT_PKT_TIP_MASK			0b00011111
#define PT_PKT_TIP_BYTE0		0b00001101
#define PT_PKT_TIP_PGE_BYTE0	0b00010001
#define PT_PKT_TIP_PGD_BYTE0	0b00000001
#define PT_PKT_TIP_FUP_BYTE0	0b00011101


#define TIP_VALUE_0				(0x0<<5)
#define TIP_VALUE_1				(0x1<<5)
#define TIP_VALUE_2				(0x2<<5)
#define TIP_VALUE_3				(0x3<<5)
#define TIP_VALUE_4				(0x4<<5)
#define TIP_VALUE_5				(0x5<<5)
#define TIP_VALUE_6				(0x6<<5)
#define TIP_VALUE_7				(0x7<<5)

//#define DEBUG

static decoder_state_machine_t* decoder_statemachine_new(void);
static void decoder_statemachine_reset(decoder_state_machine_t* self);

static uint8_t psb[16] = {
	0x02, 0x82, 0x02, 0x82, 0x02, 0x82, 0x02, 0x82,
	0x02, 0x82, 0x02, 0x82, 0x02, 0x82, 0x02, 0x82
};

#ifdef DECODER_LOG
static void flush_log(decoder_t* self){
	self->log.tnt64 = 0;
	self->log.tnt8 = 0;
	self->log.pip = 0;
	self->log.cbr = 0;
	self->log.ts = 0;
	self->log.ovf = 0;
	self->log.psbc = 0;
	self->log.psbend = 0;
	self->log.mnt = 0;
	self->log.tma = 0;
	self->log.vmcs = 0;
	self->log.pad = 0;
	self->log.tip = 0;
	self->log.tip_pge = 0;
	self->log.tip_pgd = 0;
	self->log.tip_fup = 0;
	self->log.mode = 0;
}
#endif

decoder_t* pt_decoder_init(CPUState *cpu, uint64_t min_addr, uint64_t max_addr, void (*pt_bitmap)(uint64_t)){
	decoder_t* res = malloc(sizeof(decoder_t));
	res->last_tip = 0;
	res->last_tip_tmp = 0;
#ifdef DECODER_LOG
	flush_log(res);
#endif
	res->disassembler_state = init_disassembler(cpu, min_addr, max_addr, pt_bitmap);
	res->tnt_cache_state = tnt_cache_init();
		/* ToDo: Free! */
	res->decoder_state = decoder_statemachine_new();
	res->decoder_state_result = malloc(sizeof(should_disasm_t));
	res->decoder_state_result->start = 0;
	res->decoder_state_result->valid = 0;
	res->decoder_state_result->valid = false;

	return res;
}

void pt_decoder_destroy(decoder_t* self){
	if(self->tnt_cache_state){
		destroy_disassembler(self->disassembler_state);
		tnt_cache_destroy(self->tnt_cache_state);
		self->tnt_cache_state = NULL;
	}
	free(self->decoder_state);
	free(self->decoder_state_result);
	free(self);
}

void pt_decoder_flush(decoder_t* self){
	self->last_tip = 0;
	self->last_tip_tmp = 0;
#ifdef DECODER_LOG
	flush_log(self);
#endif

	tnt_cache_flush(self->tnt_cache_state);
	disassembler_flush(self->disassembler_state);
	decoder_statemachine_reset(self->decoder_state);
	self->decoder_state_result->start = 0;
	self->decoder_state_result->valid = 0;
	self->decoder_state_result->valid = false;
}	


static inline void _set_disasm(should_disasm_t* self, uint64_t from, uint64_t to){
	self->valid = true;
	self->start = from;
	self->end = to;
}

static decoder_state_machine_t* decoder_statemachine_new(void){
	decoder_state_machine_t * res = (decoder_state_machine_t*)malloc(sizeof(decoder_state_machine_t));
	res->state = TraceDisabled;
	res->last_ip = 0;
	return res;
}

static void decoder_statemachine_reset(decoder_state_machine_t* self){
	self->state = TraceDisabled;
	self->last_ip = 0;
}

static inline void decoder_handle_tip(decoder_state_machine_t *self, uint64_t addr, should_disasm_t *res){
	//assert(self->state);
	res->valid= false;
	switch(self->state){
		case TraceDisabled:
			_set_disasm(res, addr, 0);
			self->state = TraceEnabledWithLastIP;
			self->last_ip = addr;
			//assert(false);
			break;
		case TraceEnabledWithLastIP:
			_set_disasm(res, self->last_ip, 0);
			self->state = TraceEnabledWithLastIP;
			self->last_ip = addr;
			break;
		case TraceEnabledWOLastIP:
			self->state = TraceEnabledWithLastIP;
			self->last_ip = addr;
			break;
	}
}

static inline void decoder_handle_pgd(decoder_state_machine_t *self, uint64_t addr, should_disasm_t *res){
	//assert(self->state);
	res->valid= false;
	switch(self->state){
		case TraceDisabled:
			//assert(false);
			break;
		case TraceEnabledWithLastIP:
			_set_disasm(res, self->last_ip, addr);
			self->state = TraceDisabled;
			self->last_ip = 0;
			break;
		case TraceEnabledWOLastIP:
			self->state = TraceDisabled;
			break;
	}
}

static inline void decoder_handle_pge(decoder_state_machine_t *self, uint64_t addr, should_disasm_t *res){
	//assert(self->state);
	res->valid= false;
	switch(self->state){
		case TraceDisabled:
			self->state = TraceEnabledWithLastIP;
			self->last_ip = addr;
			break;
		case TraceEnabledWithLastIP:
			//assert(false);
			break;
		case TraceEnabledWOLastIP:
			self->state = TraceEnabledWithLastIP;
			self->last_ip = addr;
			break;
	}
}


static inline void decoder_handle_fup(decoder_state_machine_t *self, uint64_t addr, should_disasm_t *res){
	//assert(self->state);
	res->valid= false;
	switch(self->state){
		case TraceDisabled:
			self->state = TraceDisabled;
			break;
		case TraceEnabledWithLastIP:
			_set_disasm(res, self->last_ip, addr);
			self->state = TraceEnabledWOLastIP;
			self->last_ip = 0;
		      break;
		case TraceEnabledWOLastIP:
			//assert(false);
			break;
	}
}

static inline uint64_t get_ip_val(uint8_t **pp, uint8_t *end, uint8_t len, uint64_t *last_ip){
	uint8_t *p = *pp;
	uint64_t v = *last_ip;

	if (unlikely(!LEFT(len))) {
		*last_ip = 0;
		v = 0;
		return v;
	}

	switch(len){
		case 0:
			v = 0;
			break;
		case 1:
			v = (v & ~0xffffull) | (*(uint64_t *)p & 0xffffull);
			*pp = p + 2;
			*last_ip = v;
			break;
		case 2:
			v = (v & ~0xffffffffull) | (*(uint64_t *)p & 0xffffffffull);
			*pp = p + 4;
			*last_ip = v;
			break;
		case 3:
			v = (v & ~0xffffffffffffull) | (*(uint64_t *)p & 0xffffffffffffull);
			v = ((int64_t)(v << (64 - 48))) >> (64 - 48); /* sign extension */
			*pp = p + 6;
			*last_ip = v;
			break;
		case 4:
			v = (v & ~0xffffffffffffull) | (*(uint64_t *)p & 0xffffffffffffull);
			*pp = p + 6;
			*last_ip = v;
			break;
		case 6:
			v = *(uint64_t *)p;
			*pp = p + 8;
			*last_ip = v;
			break;
		default:
			v = 0;
			break;
	}
	return v;
}

static inline uint64_t get_val(uint8_t **pp, uint8_t len){
	uint8_t*p = *pp;
	uint64_t v = 0;
	uint8_t i;
	uint8_t shift = 0;

	for (i = 0; i < len; i++, shift += 8)
		v |= ((uint64_t)(*p++)) << shift;
	*pp = p;
	return v;
}

static inline void disasm(decoder_t* self){
	should_disasm_t* res = self->decoder_state_result;
	if(res->valid){
    	WRITE_SAMPLE_DECODED_DETAILED("\n\ndisasm(%lx,%lx)\tTNT: %ld\n", res->start, res->end, count_tnt(self->tnt_cache_state));
  		trace_disassembler(self->disassembler_state, res->start, res->end, self->tnt_cache_state, self->fup_tip);
		if(unlikely(self->fup_tip))
			self->fup_tip = 0;
		
	}
}

static void tip_handler(decoder_t* self, uint8_t** p, uint8_t** end){
	self->last_tip = get_ip_val(p, *end, (*(*p)++ >> PT_PKT_TIP_SHIFT), &self->last_tip_tmp);
	WRITE_SAMPLE_DECODED_DETAILED("TIP    \t%lx\n", self->last_tip);
	decoder_handle_tip(self->decoder_state, self->last_tip, self->decoder_state_result);
	disasm(self);
#ifdef DECODER_LOG
	self->log.tip++;
#endif
}

static void tip_pge_handler(decoder_t* self, uint8_t** p, uint8_t** end){
	self->last_tip = get_ip_val(p, *end, (*(*p)++ >> PT_PKT_TIP_SHIFT), &self->last_tip_tmp);
	WRITE_SAMPLE_DECODED_DETAILED("PGE    \t%lx\n", self->last_tip);
	decoder_handle_pge(self->decoder_state, self->last_tip, self->decoder_state_result);
	disasm(self);
#ifdef DECODER_LOG
	self->log.tip_pge++;
#endif
}

static void tip_pgd_handler(decoder_t* self, uint8_t** p, uint8_t** end){
	self->last_tip = get_ip_val(p, *end, (*(*p)++ >> PT_PKT_TIP_SHIFT), &self->last_tip_tmp);
	WRITE_SAMPLE_DECODED_DETAILED("PGD    \t%lx\n", self->last_tip);
	decoder_handle_pgd(self->decoder_state, self->last_tip, self->decoder_state_result);
	disasm(self);
#ifdef DECODER_LOG
	self->log.tip_pgd++;
#endif
}

static void tip_fup_handler(decoder_t* self, uint8_t** p, uint8_t** end){
	self->fup_tip = get_ip_val(p, *end, (*(*p)++ >> PT_PKT_TIP_SHIFT), &self->last_tip_tmp);
#ifdef DECODER_LOG
	self->log.tip_fup++;
#endif
}

static inline void pip_handler(decoder_t* self, uint8_t** p){
#ifdef SAMPLE_DECODED_DETAILED
	(*p) += PT_PKT_PIP_LEN-6;
	WRITE_SAMPLE_DECODED_DETAILED("PIP\t%llx\n", (get_val(p, 6) >> 1) << 5);
#else
	(*p) += PT_PKT_PIP_LEN;
#endif
#ifdef DECODER_LOG
	self->log.pip++;
#endif
}

 __attribute__((hot)) bool decode_buffer(decoder_t* self, uint8_t* map, size_t len){
	uint8_t *end = map + len;
	uint8_t *p;

#ifdef DECODER_LOG
	flush_log(self);
#endif

#ifdef DEBUG_PACKET
	puts("[DEBUG] decode_buffer");
	for (int i=0; i<len; i++) {
		if (i && i % 0x10 == 0)
			puts("");
		
		printf("%02x ", map[i]);
	}
	puts("");
#endif

	for (p = map; p < end; ) {
		p = memmem(p, end - p, psb, PT_PKT_PSB_LEN);
		if (!p) {
			p = end;
			break;
		}
		
		while (p < end) {			
			
			switch(p[0]){
				case 0x00:
					while(!(*(++p)) && p < end){}
					#ifdef DECODER_LOG
					self->log.pad++;
					#endif
					break;
				case PT_PKT_MODE_BYTE0:
					p += PT_PKT_MODE_LEN;
					WRITE_SAMPLE_DECODED_DETAILED("MODE\n");
					#ifdef DECODER_LOG
					self->log.mode++;
					#endif
					break;
				case (PT_PKT_TIP_BYTE0 + TIP_VALUE_0):
				case (PT_PKT_TIP_BYTE0 + TIP_VALUE_1):
				case (PT_PKT_TIP_BYTE0 + TIP_VALUE_2):
				case (PT_PKT_TIP_BYTE0 + TIP_VALUE_3):
				case (PT_PKT_TIP_BYTE0 + TIP_VALUE_4):
				case (PT_PKT_TIP_BYTE0 + TIP_VALUE_5):
				case (PT_PKT_TIP_BYTE0 + TIP_VALUE_6):
				case (PT_PKT_TIP_BYTE0 + TIP_VALUE_7):
					tip_handler(self, &p, &end);
					break;
				case (PT_PKT_TIP_PGE_BYTE0 + TIP_VALUE_0):
				case (PT_PKT_TIP_PGE_BYTE0 + TIP_VALUE_1):
				case (PT_PKT_TIP_PGE_BYTE0 + TIP_VALUE_2):
				case (PT_PKT_TIP_PGE_BYTE0 + TIP_VALUE_3):
				case (PT_PKT_TIP_PGE_BYTE0 + TIP_VALUE_4):
				case (PT_PKT_TIP_PGE_BYTE0 + TIP_VALUE_5):
				case (PT_PKT_TIP_PGE_BYTE0 + TIP_VALUE_6):
				case (PT_PKT_TIP_PGE_BYTE0 + TIP_VALUE_7):
					tip_pge_handler(self, &p, &end);
					break;
				case (PT_PKT_TIP_PGD_BYTE0 + TIP_VALUE_0):
				case (PT_PKT_TIP_PGD_BYTE0 + TIP_VALUE_1):
				case (PT_PKT_TIP_PGD_BYTE0 + TIP_VALUE_2):
				case (PT_PKT_TIP_PGD_BYTE0 + TIP_VALUE_3):
				case (PT_PKT_TIP_PGD_BYTE0 + TIP_VALUE_4):
				case (PT_PKT_TIP_PGD_BYTE0 + TIP_VALUE_5):
				case (PT_PKT_TIP_PGD_BYTE0 + TIP_VALUE_6):
				case (PT_PKT_TIP_PGD_BYTE0 + TIP_VALUE_7):
					tip_pgd_handler(self, &p, &end);
					break;
				case (PT_PKT_TIP_FUP_BYTE0 + TIP_VALUE_0):
				case (PT_PKT_TIP_FUP_BYTE0 + TIP_VALUE_1):
				case (PT_PKT_TIP_FUP_BYTE0 + TIP_VALUE_2):
				case (PT_PKT_TIP_FUP_BYTE0 + TIP_VALUE_3):
				case (PT_PKT_TIP_FUP_BYTE0 + TIP_VALUE_4):
				case (PT_PKT_TIP_FUP_BYTE0 + TIP_VALUE_5):
				case (PT_PKT_TIP_FUP_BYTE0 + TIP_VALUE_6):
				case (PT_PKT_TIP_FUP_BYTE0 + TIP_VALUE_7):
					tip_fup_handler(self, &p, &end);
					break;
				case PT_PKT_GENERIC_BYTE0:
					switch(p[1]){
						case PT_PKT_LTNT_BYTE1:
							append_tnt_cache_ltnt(self->tnt_cache_state, (uint64_t)*p);
							p += PT_PKT_LTNT_LEN;
							#ifdef DECODER_LOG
							self->log.tnt64++;
							#endif
							break;
						case PT_PKT_PIP_BYTE1:
							pip_handler(self, &p);
							break;
						case PT_PKT_CBR_BYTE1:
							p += PT_PKT_CBR_LEN;
							#ifdef DECODER_LOG
							self->log.cbr++;
							#endif
							break;
						case PT_PKT_VMCS_BYTE1:
							WRITE_SAMPLE_DECODED_DETAILED("VMCS\n");
							p += PT_PKT_VMCS_LEN;
							#ifdef DECODER_LOG
							self->log.vmcs++;
							#endif
							break;
						case PT_PKT_OVF_BYTE1:
						case PT_PKT_TS_BYTE1:
							return false;
							break;
						case PT_PKT_PSBEND_BYTE1:
							p += PT_PKT_PSBEND_LEN;
							WRITE_SAMPLE_DECODED_DETAILED("PSBEND\n");
							#ifdef DECODER_LOG
							self->log.psbend++;
							#endif
							break;
						case PT_PKT_PSB_BYTE1:
							p += PT_PKT_PSB_LEN;
							WRITE_SAMPLE_DECODED_DETAILED("PSB\n");
							#ifdef DECODER_LOG
							self->log.psbc++;
							#endif
							break;
						default:
							assert(false);
					}
					break;
				/* :( */
				case 4:
				case 6:
				case 8:
				case 10:
				case 12:
				case 14:
				case 16:
				case 18:
				case 20:
				case 22:
				case 24:
				case 26:
				case 28:
				case 30:
				case 32:
				case 34:
				case 36:
				case 38:
				case 40:
				case 42:
				case 44:
				case 46:
				case 48:
				case 50:
				case 52:
				case 54:
				case 56:
				case 58:
				case 60:
				case 62:
				case 64:
				case 66:
				case 68:
				case 70:
				case 72:
				case 74:
				case 76:
				case 78:
				case 80:
				case 82:
				case 84:
				case 86:
				case 88:
				case 90:
				case 92:
				case 94:
				case 96:
				case 98:
				case 100:
				case 102:
				case 104:
				case 106:
				case 108:
				case 110:
				case 112:
				case 114:
				case 116:
				case 118:
				case 120:
				case 122:
				case 124:
				case 126:
				case 130:
				case 128:
				case 132:
				case 134:
				case 136:
				case 138:
				case 140:
				case 142:
				case 144:
				case 146:
				case 148:
				case 150:
				case 152:
				case 154:
				case 156:
				case 158:
				case 160:
				case 162:
				case 164:
				case 166:
				case 168:
				case 170:
				case 172:
				case 174:
				case 176:
				case 178:
				case 180:
				case 182:
				case 184:
				case 186:
				case 188:
				case 190:
				case 192:
				case 194:
				case 196:
				case 198:
				case 200:
				case 202:
				case 204:
				case 206:
				case 208:
				case 210:
				case 212:
				case 214:
				case 216:
				case 218:
				case 220:
				case 222:
				case 224:
				case 226:
				case 228:
				case 230:
				case 232:
				case 234:
				case 236:
				case 238:
				case 240:
				case 242:
				case 244:
				case 246:
				case 248:
				case 250:
				case 252:
				case 254:
					append_tnt_cache(self->tnt_cache_state, (uint64_t)(*p));
					p++;
					#ifdef DECODER_LOG
					self->log.tnt8++;
					#endif
					break;
				default:
					fprintf(stderr, "unkown packet : %x %x\n", *p, *(p+1));
					assert(false);
			}	
		}
	}
#ifdef DEBUG
	WRITE_SAMPLE_DECODED_DETAILED("\tTNT %d (PGE %d)\n", count_tnt(self->tnt_cache_state), self->last_tip);
#endif
	return true;
}
