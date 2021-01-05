/*
 * *
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later 
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "sysemu/kvm.h"
#include "migration/migration.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qom/object_interfaces.h"
#include "chardev/char-fe.h"
#include "sysemu/hostmem.h"
#include "sysemu/qtest.h"
#include "qapi/visitor.h"
#include "exec/ram_addr.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include "pt.h"
#include "pt/hypercall.h"
#include "pt/interface.h"
#include "pt/debug.h"
#include "pt/synchronization.h"
#include "pt/asm_decoder.h"

#include <time.h>

#define CONVERT_UINT64(x) (uint64_t)(strtoull(x, NULL, 16))

#define TYPE_KAFLMEM "kafl"
#define KAFLMEM(obj) \
		OBJECT_CHECK(kafl_mem_state, (obj), TYPE_KAFLMEM)

uint32_t irpt_bitmap_size = DEFAULT_IRPT_BITMAP_SIZE;
uint32_t irpt_coverage_map_size = DEFAULT_IRPT_COVERAGE_MAP_SIZE;

static void pci_kafl_guest_realize(DeviceState *dev, Error **errp);

typedef struct kafl_mem_state {
	DeviceState parent_obj;

	Chardev *kafl_chr_drv_state;
	CharBackend chr;

	char* data_bar_fd_0;
	char* data_bar_fd_1;
	char* data_bar_fd_2;
	char* bitmap_file;
	char* coverage_map_file;

	char* ip_filter[4][2];

	bool irq_filter;
	uint64_t bitmap_size;
	uint64_t coverage_map_size;

	bool debug_mode; 	/* support for hprintf */
	bool notifier;
	bool reload_mode;
	bool disable_snapshot;
	bool lazy_vAPIC_reset;
	
} kafl_mem_state;

static void kafl_guest_event(void *opaque, QEMUChrEvent event){
}

static void send_char(char val, void* tmp_s){
	kafl_mem_state *s = tmp_s;
	qemu_chr_fe_write(&s->chr, (const uint8_t *) &val, 1);
}

static int kafl_guest_can_receive(void * opaque){
	return sizeof(int64_t);
}

static void kafl_guest_receive(void *opaque, const uint8_t * buf, int size){
	kafl_mem_state *s = opaque;
	int i;				
	for(i = 0; i < size; i++){
		switch(buf[i]){
			case KAFL_PROTO_RELEASE:
				synchronization_unlock();
				break;
			
			case KAFL_PROTO_COVER_ON:
				pt_turn_on_coverage_map();
				break;
			
			case KAFL_PROTO_COVER_OFF:
				pt_turn_off_coverage_map();
				break;

			case KAFL_PROTO_RELOAD:
				assert(false);
				synchronization_reload_vm();
				break;

			/* finalize iteration (dump and decode PT data) in case of timeouts */
			case KAFL_PROTO_FINALIZE:
				synchronization_disable_pt(qemu_get_cpu(0));
				send_char('F', s);
				break;
		}
	}
}

static int kafl_guest_create_memory_bar(kafl_mem_state *s, int region_num, uint64_t bar_size, const char* file, Error **errp){
	void * ptr;
	int fd;
	struct stat st;
	
	fd = open(file, O_CREAT|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
	assert(ftruncate(fd, bar_size) == 0);
	stat(file, &st);
	QEMU_PT_DEBUG(INTERFACE_PREFIX, "new shm file: (max size: %lx) %lx", bar_size, st.st_size);
	
	assert(bar_size >= st.st_size);
	ptr = mmap(0, bar_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		error_setg_errno(errp, errno, "Failed to mmap memory");
		return -1;
	}

	switch(region_num){
		case 1:	pt_setup_program((void*)ptr);
				break;
		case 2:	pt_setup_payload((void*)ptr);
				break;
	}

	pt_setup_snd_handler(&send_char, s);

	return 0;
}

static int kafl_guest_setup_bitmap(kafl_mem_state *s, uint32_t bitmap_size, Error **errp){
	void * ptr;
	int fd;
	struct stat st;
	
	fd = open(s->bitmap_file, O_CREAT|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
	assert(ftruncate(fd, bitmap_size) == 0);
	stat(s->bitmap_file, &st);
	assert(bitmap_size == st.st_size);
	ptr = mmap(0, bitmap_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		error_setg_errno(errp, errno, "Failed to mmap memory");
		return -1;
	}
	pt_setup_bitmap((void*)ptr);

	return 0;
}

static int kafl_guest_setup_coverage_map(kafl_mem_state *s, uint32_t coverage_map_size, Error **errp){
	void * ptr;
	int fd;
	struct stat st;
	
	fd = open(s->coverage_map_file, O_CREAT|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
	assert(ftruncate(fd, coverage_map_size) == 0);
	stat(s->coverage_map_file, &st);
	assert(coverage_map_size == st.st_size);
	ptr = mmap(0, coverage_map_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		error_setg_errno(errp, errno, "Failed to mmap memory");
		return -1;
	}
	pt_setup_coverage_map((void*)ptr);

	return 0;
}

static void pci_kafl_guest_realize(DeviceState *dev, Error **errp){
	kafl_mem_state *s = KAFLMEM(dev);

	if(s->bitmap_size <= 0){
		s->bitmap_size = DEFAULT_IRPT_BITMAP_SIZE;
	}
	irpt_bitmap_size = (uint32_t)s->bitmap_size;

	if(s->coverage_map_size <= 0){
		s->coverage_map_size = DEFAULT_IRPT_COVERAGE_MAP_SIZE;
	}
	irpt_coverage_map_size = (uint32_t)s->coverage_map_size;
	
	if (s->data_bar_fd_0 != NULL)
		kafl_guest_create_memory_bar(s, 1, PROGRAM_SIZE, s->data_bar_fd_0, errp);
	if (s->data_bar_fd_1 != NULL)
		kafl_guest_create_memory_bar(s, 2, PAYLOAD_SIZE, s->data_bar_fd_1, errp);
	
	if(&s->chr)
		qemu_chr_fe_set_handlers(&s->chr, kafl_guest_can_receive, kafl_guest_receive, kafl_guest_event, NULL, s, NULL, true);
	if(s->bitmap_file)
		kafl_guest_setup_bitmap(s, irpt_bitmap_size, errp);
	if(s->coverage_map_file)
		kafl_guest_setup_coverage_map(s, irpt_coverage_map_size, errp);

	if(s->irq_filter){
	}

	if(s->debug_mode){
		enable_hprintf();
	}

	if(s->notifier){
		enable_notifies();
	}

	if(s->reload_mode){
		enable_reload_mode();
	}

	if(s->disable_snapshot){
		pt_setup_disable_create_snapshot();
	}

	if(s->lazy_vAPIC_reset){
    assert(false);
	}


	pt_setup_enable_hypercalls();
	asm_decoder_compile();
}

static Property kafl_guest_properties[] = {
	DEFINE_PROP_CHR("chardev", kafl_mem_state, chr),
	DEFINE_PROP_STRING("shm0", kafl_mem_state, data_bar_fd_0),
	DEFINE_PROP_STRING("shm1", kafl_mem_state, data_bar_fd_1),
	DEFINE_PROP_STRING("bitmap", kafl_mem_state, bitmap_file),
	DEFINE_PROP_STRING("coverage_map", kafl_mem_state, coverage_map_file),
	/* 
	 * Since DEFINE_PROP_UINT64 is somehow broken (signed/unsigned madness),
	 * let's use DEFINE_PROP_STRING and post-process all values via strtol...
	 */
	/*
	DEFINE_PROP_STRING("ip0_a", kafl_mem_state, ip_filter[0][0]),
	DEFINE_PROP_STRING("ip0_b", kafl_mem_state, ip_filter[0][1]),
	DEFINE_PROP_STRING("ip1_a", kafl_mem_state, ip_filter[1][0]),
	DEFINE_PROP_STRING("ip1_b", kafl_mem_state, ip_filter[1][1]),
	DEFINE_PROP_STRING("ip2_a", kafl_mem_state, ip_filter[2][0]),
	DEFINE_PROP_STRING("ip2_b", kafl_mem_state, ip_filter[2][1]),
	DEFINE_PROP_STRING("ip3_a", kafl_mem_state, ip_filter[3][0]),
	DEFINE_PROP_STRING("ip3_b", kafl_mem_state, ip_filter[3][1]),
	*/
	DEFINE_PROP_BOOL("irq_filter", kafl_mem_state, irq_filter, false),
	DEFINE_PROP_UINT64("bitmap_size", kafl_mem_state, bitmap_size, DEFAULT_IRPT_BITMAP_SIZE),
	DEFINE_PROP_UINT64("coverage_map_size", kafl_mem_state, coverage_map_size, DEFAULT_IRPT_COVERAGE_MAP_SIZE),
	DEFINE_PROP_BOOL("debug_mode", kafl_mem_state, debug_mode, false),
	DEFINE_PROP_BOOL("crash_notifier", kafl_mem_state, notifier, true),
	DEFINE_PROP_BOOL("reload_mode", kafl_mem_state, reload_mode, true),
	DEFINE_PROP_BOOL("disable_snapshot", kafl_mem_state, disable_snapshot, false),
	DEFINE_PROP_BOOL("lazy_vAPIC_reset", kafl_mem_state, lazy_vAPIC_reset, false),

	DEFINE_PROP_END_OF_LIST(),
};

static void kafl_guest_class_init(ObjectClass *klass, void *data){
	DeviceClass *dc = DEVICE_CLASS(klass);
	//PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
	dc->realize = pci_kafl_guest_realize;
	//k->class_id = PCI_CLASS_MEMORY_RAM;
	//dc->props = kafl_guest_properties;
	device_class_set_props(dc, kafl_guest_properties);
	set_bit(DEVICE_CATEGORY_MISC, dc->categories);
	dc->desc = "KAFL Inter-VM shared memory";
}

static void kafl_guest_init(Object *obj){
}

static const TypeInfo kafl_guest_info = {
	.name          = TYPE_KAFLMEM,
	.parent        = TYPE_DEVICE,
	.instance_size = sizeof(kafl_mem_state),
	.instance_init = kafl_guest_init,
	.class_init    = kafl_guest_class_init,
};

static void kafl_guest_register_types(void){
	type_register_static(&kafl_guest_info);
}

type_init(kafl_guest_register_types)
