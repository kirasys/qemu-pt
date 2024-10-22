/*
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "qemu-common.h"
#include "cpu.h"
#include "pt.h"
#include "pt/decoder.h"
#include "exec/memory.h"
#include "sysemu/kvm_int.h"
#include "sysemu/kvm.h"
#include "sysemu/cpus.h"
#include "pt/hypercall.h"
#include "pt/logger.h"
#include "pt/memory_access.h"
#include "pt/interface.h"
#include "pt/debug.h"

extern uint32_t irpt_bitmap_size;
extern uint32_t irpt_coverage_map_size;

uint8_t* bitmap = NULL;

uint16_t coverage_id = 0;
bool is_coveraged = false;
uint16_t* coverage_map = NULL;

uint64_t last_ip = 0ULL;
uint64_t module_base_address = 0ULL;

void pt_sync(void){
	if(bitmap){
		msync(bitmap, irpt_bitmap_size, MS_SYNC);
		if (is_coveraged)
			msync(coverage_map, irpt_coverage_map_size, MS_SYNC);
	}
}

static inline int pt_cmd_hmp_context(CPUState *cpu, uint64_t cmd){
       cpu->pt_ret = -1;
       if(pt_hypercalls_enabled()){
               QEMU_PT_ERROR(PT_PREFIX, "Error: HMP commands are ignored if kafl tracing mode is enabled (-kafl)!");
       }
       else{
               cpu->pt_cmd = cmd;
       }
       return cpu->pt_ret;
}

static int pt_cmd(CPUState *cpu, uint64_t cmd, bool hmp_mode){
	if (hmp_mode)
		return pt_cmd_hmp_context(cpu, cmd);
	
	cpu->pt_cmd = cmd;
	pt_pre_kvm_run(cpu);
	return cpu->pt_ret;
}

static inline int pt_ioctl(int fd, unsigned long request, unsigned long arg){
	if (!fd){
		return -EINVAL;
	}
	return ioctl(fd, request, arg);
}

void pt_setup_bitmap(void* ptr){
	bitmap = (uint8_t*)ptr;
}

void pt_setup_coverage_map(void* ptr){
	coverage_map = (uint16_t*)ptr;
}

void pt_turn_on_coverage_map(void) {
	is_coveraged = true;
}

void pt_turn_off_coverage_map(void) {
	is_coveraged = false;
}

void pt_reset_bitmap(void){
	if(bitmap){
		last_ip = 0ULL;
		memset(bitmap, 0x00, irpt_bitmap_size);
	}
}

void pt_reset_coverage_map(void){
	if(is_coveraged && coverage_map){
		last_ip = 0ULL;
		coverage_id = 0;
		memset(coverage_map, 0x00, irpt_coverage_map_size);
	}
}

static inline uint64_t mix_bits(uint64_t v) {
  v ^= (v >> 31);
  v *= 0x7fb5d329728ea185;
  v ^= (v >> 27);
  v *= 0x81dadef4bc2dd44d;
  v ^= (v >> 33);
  return v;
}

void pt_bitmap(uint64_t addr){
	uint32_t transition_value = 0;
	#ifdef SAMPLE_DECODED
	sample_decoded(addr);
	#endif
	if(bitmap){
		addr -= module_base_address;
		if (is_coveraged && coverage_map) {
			coverage_map[addr % (irpt_coverage_map_size/sizeof(uint16_t))] = ++coverage_id;
		}
		addr = mix_bits(addr);
		transition_value = (addr ^ (last_ip >> 1)) & 0xffffff;
		bitmap[transition_value & (irpt_bitmap_size-1)]++;
	}
	last_ip = addr; 
}

void pt_dump(CPUState *cpu, int bytes){
#ifdef SAMPLE_RAW
	sample_raw(cpu->pt_mmap, bytes);
#endif
#ifdef SAMPLE_RAW_SINGLE
	sample_raw_single(cpu->pt_mmap, bytes);
#endif
	for(uint8_t i = 0; i < INTEL_PT_MAX_RANGES; i++){
		if(cpu->pt_ip_filter_enabled[i]){			
			if (cpu->pt_target_file){
				fwrite(cpu->pt_mmap, sizeof(char), bytes, cpu->pt_target_file);
			}
			if (!cpu->intel_pt_run_trashed){
				if(!decode_buffer(cpu->pt_decoder_state[i], cpu->pt_mmap, bytes)){
					cpu->intel_pt_run_trashed = true;
				}
			}
		}
	}
	cpu->trace_size += bytes;
}


int pt_enable(CPUState *cpu, bool hmp_mode){
#ifdef SAMPLE_RAW
	init_sample_raw();
#endif
#ifdef SAMPLE_RAW_SINGLE
	init_sample_raw_single(getpid());
#endif
#ifdef SAMPLE_DECODED
	init_sample_decoded();
#endif
#ifdef SAMPLE_DECODED_DETAILED
	init_sample_decoded_detailed();
#endif
	pt_reset_bitmap();
	pt_reset_coverage_map();
	return pt_cmd(cpu, KVM_VMX_PT_ENABLE, hmp_mode);
}
	
int pt_disable(CPUState *cpu, bool hmp_mode){
	int r = pt_cmd(cpu, KVM_VMX_PT_DISABLE, hmp_mode);
	for(uint8_t i = 0; i < INTEL_PT_MAX_RANGES; i++){
		if(cpu->pt_ip_filter_enabled[i]){
			pt_decoder_flush(cpu->pt_decoder_state[i]);
		}
	}

	return r;
}

int pt_set_cr3(CPUState *cpu, uint64_t val, bool hmp_mode){
	int r = 0;
	
	if (cpu->pt_enabled){
		return -EINVAL;
	}
	if (cpu->pt_c3_filter && cpu->pt_c3_filter != val){
		QEMU_PT_DEBUG(PT_PREFIX, "Reconfigure CR3-Filtering!");
		cpu->pt_c3_filter = val;
		r += pt_cmd(cpu, KVM_VMX_PT_CONFIGURE_CR3, hmp_mode);
		return r;
	}
	cpu->pt_c3_filter = val;
	r += pt_cmd(cpu, KVM_VMX_PT_CONFIGURE_CR3, hmp_mode);
	r += pt_cmd(cpu, KVM_VMX_PT_ENABLE_CR3, hmp_mode);
	return r;
}

int pt_enable_ip_filtering(CPUState *cpu, uint8_t addrn, uint64_t ip_a, uint64_t ip_b, bool hmp_mode){
	int r = 0;

	if(addrn > 3){
		return -1;
	}

	if (cpu->pt_enabled){
		return -EINVAL;
	}
		
	if(ip_a > ip_b){
		QEMU_PT_ERROR(PT_PREFIX, "Error (ip_a > ip_b) 0x%lx-0x%lx", ip_a, ip_b);
		return -EINVAL;
	}
	
	if(cpu->pt_ip_filter_enabled[addrn]){
		pt_disable_ip_filtering(cpu, addrn, hmp_mode);
	}

#ifdef CREATE_VM_IMAGE
	uint8_t* buf;
	buf = malloc(ip_b-ip_a); // TODO memory leak?
	if(!read_virtual_memory(ip_a, buf, ip_b-ip_a, cpu)){
		QEMU_PT_ERROR(PT_PREFIX, "Error (cannot dump trace region) 0x%lx-0x%lx (size: %lx)", ip_a, ip_b, (ip_b-ip_a));
		free(buf);
		return -EINVAL;
	}

	FILE* pt_file = fopen(DECODER_MEMORY_IMAGE, "wb");
	if (!pt_file) {
		QEMU_PT_ERROR(CORE_PREFIX, "Error writing file %s)", DECODER_MEMORY_IMAGE);
	} else {
		fwrite(buf, sizeof(uint8_t), ip_b-ip_a, pt_file);
		fclose(pt_file);
	}
#endif


	QEMU_PT_DEBUG(PT_PREFIX, "Configuring new trace region (addr%d, 0x%lx-0x%lx)", addrn, ip_a, ip_b);
	
	switch(addrn){
		case 0:
		case 1:
		case 2:
		case 3:
			module_base_address = ip_a; 				// for pt_bitmap
			cpu->pt_ip_filter_a[addrn] = ip_a;
			cpu->pt_ip_filter_b[addrn] = ip_b;
			r += pt_cmd(cpu, KVM_VMX_PT_CONFIGURE_ADDR0+addrn, hmp_mode);
			r += pt_cmd(cpu, KVM_VMX_PT_ENABLE_ADDR0+addrn, hmp_mode);
			cpu->pt_ip_filter_enabled[addrn] = true;	
			cpu->pt_decoder_state[addrn] = pt_decoder_init(cpu, ip_a, ip_b, &pt_bitmap);
			break;
		default:
			r = -EINVAL;
	}
	return r;
}

int pt_disable_ip_filtering(CPUState *cpu, uint8_t addrn, bool hmp_mode){
	int r = 0;
	switch(addrn){
		case 0:
		case 1:
		case 2:
		case 3:
			r = pt_cmd(cpu, KVM_VMX_PT_DISABLE_ADDR0+addrn, hmp_mode);
			if(cpu->pt_ip_filter_enabled[addrn]){
				cpu->pt_ip_filter_enabled[addrn] = false;
				pt_decoder_destroy(cpu->pt_decoder_state[addrn]);
			}
			break;
		default:
			r = -EINVAL;
	}
	return r;
}

void pt_kvm_init(CPUState *cpu){
	int i;

	cpu->pt_cmd = 0;
	cpu->pt_arg = 0;
	cpu->pt_enabled = false;
	cpu->pt_fd = 0;
	cpu->pt_features = 0;

	for(i = 0; i < INTEL_PT_MAX_RANGES; i++){
		cpu->pt_ip_filter_enabled[i] = false;
		cpu->pt_ip_filter_a[i] = 0x0;
		cpu->pt_ip_filter_b[i] = 0x0;
		cpu->pt_decoder_state[i] = NULL;
	}

   	// setting the target's word with is critical to RQ operation
	// Initialize as invalid, set by submit_CR3 or submit_mode hypercalls
	cpu->disassembler_word_width = 0;

	cpu->pt_c3_filter = 0;
	cpu->pt_target_file = NULL;
	cpu->overflow_counter = 0;
	cpu->trace_size = 0;
	cpu->reload_pending = false;
	cpu->executing = false;
	cpu->intel_pt_run_trashed = false;
}

struct vmx_pt_filter_iprs {
	__u64 a;
	__u64 b;
};

pthread_mutex_t pt_dump_mutex = PTHREAD_MUTEX_INITIALIZER;

void pt_pre_kvm_run(CPUState *cpu){
	pthread_mutex_lock(&pt_dump_mutex);
	int ret;
	struct vmx_pt_filter_iprs filter_iprs;

	if (!cpu->pt_fd) {
		cpu->pt_fd = kvm_vcpu_ioctl(cpu, KVM_VMX_PT_SETUP_FD, (unsigned long)0);
		ret = ioctl(cpu->pt_fd, KVM_VMX_PT_GET_TOPA_SIZE, (unsigned long)0x0);
		QEMU_PT_DEBUG(PT_PREFIX, "TOPA SIZE: %x\n", ret);
		cpu->pt_mmap = mmap(0, ret, PROT_READ, MAP_SHARED, cpu->pt_fd, 0);
	}
	
	if (cpu->pt_cmd){
		switch(cpu->pt_cmd){
			case KVM_VMX_PT_ENABLE:
				if (cpu->pt_fd){
					/* dump for the very last time before enabling VMX_PT ... just in case */
					ioctl(cpu->pt_fd, KVM_VMX_PT_CHECK_TOPA_OVERFLOW, (unsigned long)0);

					if (!ioctl(cpu->pt_fd, cpu->pt_cmd, cpu->pt_arg)){
						cpu->pt_enabled = true;
					}
				}
				break;
			case KVM_VMX_PT_DISABLE:
				if (cpu->pt_fd){
					ret = ioctl(cpu->pt_fd, cpu->pt_cmd, cpu->pt_arg);
					if (ret > 0){
						QEMU_PT_DEBUG(PT_PREFIX, "KVM_VMX_PT_DISABLE %d", ret);
						pt_dump(cpu, ret);
						cpu->pt_enabled = false;
					}
				}
				break;
			
			/* ip filtering configuration */	
			case KVM_VMX_PT_CONFIGURE_ADDR0:
			case KVM_VMX_PT_CONFIGURE_ADDR1:
			case KVM_VMX_PT_CONFIGURE_ADDR2:
			case KVM_VMX_PT_CONFIGURE_ADDR3:
				filter_iprs.a = cpu->pt_ip_filter_a[(cpu->pt_cmd)-KVM_VMX_PT_CONFIGURE_ADDR0];
	   			filter_iprs.b = cpu->pt_ip_filter_b[(cpu->pt_cmd)-KVM_VMX_PT_CONFIGURE_ADDR0];
				ret = pt_ioctl(cpu->pt_fd, cpu->pt_cmd, (unsigned long)&filter_iprs);
				break;
			case KVM_VMX_PT_ENABLE_ADDR0:
			case KVM_VMX_PT_ENABLE_ADDR1:
			case KVM_VMX_PT_ENABLE_ADDR2:
			case KVM_VMX_PT_ENABLE_ADDR3:
				ret = pt_ioctl(cpu->pt_fd, cpu->pt_cmd, (unsigned long)0);
				break;
			case KVM_VMX_PT_CONFIGURE_CR3:
				ret = pt_ioctl(cpu->pt_fd, cpu->pt_cmd, cpu->pt_c3_filter);
				break;
			case KVM_VMX_PT_ENABLE_CR3:
				ret = pt_ioctl(cpu->pt_fd, cpu->pt_cmd, (unsigned long)0);
				break;
			default:
				if (cpu->pt_fd){
					ioctl(cpu->pt_fd, cpu->pt_cmd, cpu->pt_arg);  
				}
				break;
			}
		cpu->pt_cmd = 0;
		cpu->pt_ret = 0;
	}
	pthread_mutex_unlock(&pt_dump_mutex);
}

void pt_handle_overflow(CPUState *cpu){
	pthread_mutex_lock(&pt_dump_mutex);
	//printf("%s\n", __func__);
	int overflow = ioctl(cpu->pt_fd, KVM_VMX_PT_CHECK_TOPA_OVERFLOW, (unsigned long)0);
	if (overflow > 0){
			cpu->overflow_counter++;
		pt_dump(cpu, overflow);
	}  

	pthread_mutex_unlock(&pt_dump_mutex);
}

void pt_post_kvm_run(CPUState *cpu){
	pt_handle_overflow(cpu);
}
