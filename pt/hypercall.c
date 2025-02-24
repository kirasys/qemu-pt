/*
 * *
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
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/runstate.h"
#include "sysemu/kvm_int.h"
#include "sysemu/kvm.h"
#include "migration/snapshot.h"
#include "pt.h"
#include "pt/hypercall.h"
#include "pt/memory_access.h"
#include "pt/interface.h"
#include "pt/printk.h"
#include "pt/debug.h"
#include "pt/synchronization.h"

bool hprintf_enabled = false;
bool notifiers_enabled = false;
uint32_t hprintf_counter = 0;

bool create_snapshot_enabled = true;
bool hypercall_enabled = false;
void* payload_buffer = NULL;
void* payload_buffer_guest = NULL;
void* program_buffer = NULL;
char buffer[INFO_SIZE];
char hprintf_buffer[HPRINTF_SIZE];
void* argv = NULL;

static bool init_state = true;

void (*handler)(char, void*) = NULL; 
void* s = NULL;

typedef struct Driver_information {
	uint8_t* image;
	uint64_t imagebase;
	uint64_t imagesize;
} driver_information;

driver_information driver_info = {0, 0xFFFFFFFFFFFFFFFF, 0};

void pt_setup_disable_create_snapshot(void){
	create_snapshot_enabled = false;
}

bool pt_hypercalls_enabled(void){
	return hypercall_enabled;
}

void pt_setup_enable_hypercalls(void){
	hypercall_enabled = true;
}

void pt_setup_snd_handler(void (*tmp)(char, void*), void* tmp_s){
	s = tmp_s;
	handler = tmp;
}

bool hypercall_snd_char(char val){
	if (handler != NULL){
		handler(val, s);
		return true;
	}
	return false;
}

void hypercall_reset_hprintf_counter(void){
	hprintf_counter = 0;
}

void pt_setup_program(void* ptr){
	program_buffer = ptr;
}

void pt_setup_payload(void* ptr){
	payload_buffer = ptr;
}

void handle_hypercall_irpt_ip_filtering(struct kvm_run *run, CPUState *cpu) {
	if(hypercall_enabled){
		uint8_t filter_id = 0;	//TODO - support multiple filter. 
		uint64_t start = run->hypercall.args[0];
		uint64_t end = run->hypercall.args[1];
		
		pt_reset_bitmap();
		pt_reset_coverage_map();

		if (start && end) {
			if (driver_info.image)
				free(driver_info.image);
			driver_info.image = malloc(end - start + 1);
			driver_info.imagebase = start;
			driver_info.imagesize = end-start;

			read_virtual_memory(driver_info.imagebase, driver_info.image, driver_info.imagesize , cpu);
			pt_enable_ip_filtering(cpu, filter_id, start, end, false);
			return;
		}
		write_virtual_memory(driver_info.imagebase, driver_info.image, driver_info.imagesize , cpu);
	}
}

bool handle_hypercall_kafl_next_payload(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		if (init_state){
			synchronization_lock(cpu);
		} else {
			synchronization_lock(cpu);
			if (!write_virtual_memory((uint64_t)payload_buffer_guest, payload_buffer, PAYLOAD_SIZE, cpu))
				assert(false);
			return true;
		}
	}
	return false;
}

void handle_hypercall_kafl_acquire(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		//printf("acquire\n");
		if (!init_state){
			if (pt_enable(cpu, false) == 0){
				cpu->pt_enabled = true;
			}
		}
	}
}

void handle_hypercall_get_payload(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		if(payload_buffer){
			QEMU_PT_PRINTF(CORE_PREFIX, "Got payload address:\t%llx", run->hypercall.args[0]);
			payload_buffer_guest = (void*)run->hypercall.args[0];
			write_virtual_memory((uint64_t)payload_buffer_guest, payload_buffer, PAYLOAD_SIZE, cpu);
		}
	}
}

void handle_hypercall_get_program(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		if(program_buffer){
			QEMU_PT_PRINTF(CORE_PREFIX, "Got program address:\t%llx", run->hypercall.args[0]);
			write_virtual_memory((uint64_t)run->hypercall.args[0], program_buffer, PROGRAM_SIZE, cpu);
		}
	}
}

void handle_hypercall_kafl_release(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		if (init_state){
			init_state = false;	

			hypercall_snd_char(KAFL_PROTO_RELEASE);
		} else {
			synchronization_disable_pt(cpu);
		}
	}
}


void handle_hypercall_kafl_cr3(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		QEMU_PT_PRINTF(CORE_PREFIX, "Got CR3 address:\t\t%llx", run->hypercall.args[0]);
		pt_set_cr3(cpu, run->hypercall.args[0], false);

		if (cpu->disassembler_word_width == 0) {
			if (run->hypercall.longmode) {
				QEMU_PT_PRINTF(CORE_PREFIX, "Auto-detected word width as 64bit (longmode=%d)", run->hypercall.longmode);
				cpu->disassembler_word_width = 64;
			} else {
				QEMU_PT_PRINTF(CORE_PREFIX, "Auto-detected word width as 32bit (longmode=%d)", run->hypercall.longmode);
				cpu->disassembler_word_width = 32;
			}
		}
	}
}

void handle_hypercall_kafl_submit_panic(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		QEMU_PT_PRINTF(CORE_PREFIX, "Patching PANIC address:\t%llx, longmode=%x", run->hypercall.args[0], run->hypercall.longmode);
		if(notifiers_enabled){
			if (run->hypercall.longmode) {
				write_virtual_memory(run->hypercall.args[0], (uint8_t*)PANIC_PAYLOAD_64, PAYLOAD_BUFFER_SIZE, cpu);
			} else {
				write_virtual_memory(run->hypercall.args[0], (uint8_t*)PANIC_PAYLOAD_32, PAYLOAD_BUFFER_SIZE, cpu);
			}
		}
	}
}

void handle_hypercall_kafl_submit_kasan(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		QEMU_PT_PRINTF(CORE_PREFIX, "Patching kASAN address:\t%llx, longmode=%x", run->hypercall.args[0], run->hypercall.longmode);
		if(notifiers_enabled){
			if (run->hypercall.longmode){
				write_virtual_memory(run->hypercall.args[0], (uint8_t*)KASAN_PAYLOAD_64, PAYLOAD_BUFFER_SIZE, cpu);
			} else {
				write_virtual_memory(run->hypercall.args[0], (uint8_t*)KASAN_PAYLOAD_32, PAYLOAD_BUFFER_SIZE, cpu);
			}
		}
	}
}

void handle_hypercall_kafl_panic(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		if(run->hypercall.args[0]){
			QEMU_PT_DEBUG(CORE_PREFIX, "Panic in user mode!");
		} else{
			QEMU_PT_DEBUG(CORE_PREFIX, "Panic in kernel mode!");
		}
		hypercall_snd_char(KAFL_PROTO_CRASH);
	}
}

void handle_hypercall_kafl_timeout(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		QEMU_PT_DEBUG(CORE_PREFIX, "Timeout detected!");
		hypercall_snd_char(KAFL_PROTO_TIMEOUT);
	}
}

void handle_hypercall_kafl_kasan(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		if(run->hypercall.args[0]){
			QEMU_PT_DEBUG(CORE_PREFIX, "ASan notification in user mode!");
		} else{
			QEMU_PT_DEBUG(CORE_PREFIX, "ASan notification in kernel mode!");
		}
		hypercall_snd_char(KAFL_PROTO_KASAN);
	}
}

void handle_hypercall_irpt_lock(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled) {
		synchronization_lock(cpu);
		hypercall_snd_char(KAFL_PROTO_LOCK);
	}
}

void handle_hypercall_irpt_memwrite(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled) {
		// read source data.
		read_virtual_memory(run->hypercall.args[1], (uint8_t*)buffer, run->hypercall.args[2], cpu);

		// write
		write_virtual_memory(run->hypercall.args[0], (uint8_t*)buffer, run->hypercall.args[2], cpu);
	}
}

void handle_hypercall_kafl_snapshot(struct kvm_run *run, CPUState *cpu){
	/*
	if(create_snapshot_enabled){
		Error *err = NULL;
		QEMU_PT_PRINTF(CORE_PREFIX, "Creating snapshot <kafl> ...");
		qemu_mutex_lock_iothread();
		kvm_cpu_synchronize_state(qemu_get_cpu(0));
		save_snapshot("kafl", &err);
        if (err)
            error_reportf_err(err, "Error: ");

		qemu_mutex_unlock_iothread();
		QEMU_PT_PRINTF(CORE_PREFIX, "Done. Shutting down..");
		qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_SIGNAL);
	}
	*/
	printf("kAFL: VM PAUSED - CREATE SNAPSHOT NOW!\n");
	vm_stop(RUN_STATE_PAUSED);
}

void handle_hypercall_kafl_info(struct kvm_run *run, CPUState *cpu){
	read_virtual_memory((uint64_t)run->hypercall.args[0], (uint8_t*)buffer, INFO_SIZE, cpu);
	FILE* info_file_fd = fopen(INFO_FILE, "w");
	fprintf(info_file_fd, "%s\n", buffer);
	fclose(info_file_fd);
	if(hypercall_enabled){
		hypercall_snd_char(KAFL_PROTO_INFO);
	}
	qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_SIGNAL);
}

void enable_hprintf(void){
	QEMU_PT_DEBUG(CORE_PREFIX, "Enable hprintf support");
	hprintf_enabled = true;
}

void enable_notifies(void){
	notifiers_enabled = true;
}

void enable_reload_mode(void){
	assert(false);
}

void hprintf(char* msg){
	char file_name[256];
	if(!(hprintf_counter >= HPRINTF_LIMIT) && hprintf_enabled){
		if(hypercall_enabled){
			snprintf(file_name, 256, "%s.%d", HPRINTF_FILE, hprintf_counter);
			//printf("%s: %s\n", __func__, msg);
			FILE* printf_file_fd = fopen(file_name, "w");
			fprintf(printf_file_fd, "%s", msg);
			fclose(printf_file_fd);
			hypercall_snd_char(KAFL_PROTO_PRINTF);
		}
		hprintf_counter++;
	}
}

void handle_hypercall_kafl_printf(struct kvm_run *run, CPUState *cpu){
	//printf("%s\n", __func__);
	
	if(!(hprintf_counter >= HPRINTF_LIMIT) && hprintf_enabled){
		//read_virtual_memory((uint64_t)run->hypercall.args[0], (uint8_t*)hprintf_buffer, HPRINTF_SIZE, cpu);
		//hprintf(hprintf_buffer);
	}
	#ifdef DEBUG_QEMU
	read_virtual_memory((uint64_t)run->hypercall.args[0], (uint8_t*)hprintf_buffer, HPRINTF_SIZE, cpu);
	QEMU_PT_PRINTF(AGENT_PREFIX, "%s", hprintf_buffer);
	#endif
}


void handle_hypercall_kafl_printk(struct kvm_run *run, CPUState *cpu){
	if(!notifiers_enabled){
		if (hypercall_enabled && hprintf_enabled){
			if(kafl_linux_printk(cpu)){
				handle_hypercall_kafl_panic(run, cpu);
			}
		}
	}
}

void handle_hypercall_kafl_printk_addr(struct kvm_run *run, CPUState *cpu){
	if(!notifiers_enabled){
		printf("%s\n", __func__);
		printf("%lx\n", (uint64_t)run->hypercall.args[0]);
		write_virtual_memory((uint64_t)run->hypercall.args[0], (uint8_t*)PRINTK_PAYLOAD, PRINTK_PAYLOAD_SIZE, cpu);
		printf("Done\n");
	}		
}

void handle_hypercall_kafl_user_submit_mode(struct kvm_run *run, CPUState *cpu){
	//printf("%s\n", __func__);
	switch((uint64_t)run->hypercall.args[0]){
		case KAFL_MODE_64:
			QEMU_PT_PRINTF(CORE_PREFIX, "Target reports 64bit word width");
			cpu->disassembler_word_width = 64;
			break;
		case KAFL_MODE_32:
			QEMU_PT_PRINTF(CORE_PREFIX, "Target reports 32bit word width");
			cpu->disassembler_word_width = 32;
			break;
		case KAFL_MODE_16:
			QEMU_PT_PRINTF(CORE_PREFIX, "Target reports 16bit word width");
			cpu->disassembler_word_width = 16;
			break;
		default:
			QEMU_PT_ERROR(CORE_PREFIX, "Error: target uses unknown word width!");
			cpu->disassembler_word_width = -1;
			break;
	}
}

void handle_hypercall_kafl_user_abort(struct kvm_run *run, CPUState *cpu){
	if(hypercall_enabled){
		hypercall_snd_char(KAFL_PROTO_PT_ABORT);
	}
	printf("[QEMU] Fatal error occured in the agent.exe\n");
	printf("[QEMU] Please check driver path, device_name, etc..\n");
	//qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_SIGNAL);
}
