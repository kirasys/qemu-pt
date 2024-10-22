/* 
 * *
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later 
 */



#pragma once

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/log.h"

#define QEMU_PT_PREFIX		"[QEMU-PT] "
#define CORE_PREFIX			"Core:  "
#define MEM_PREFIX			"Mem:   "
#define RELOAD_PREFIX		"Reload:"
#define PT_PREFIX			"PT:    "
#define INTERFACE_PREFIX	"Iface: "
#define DISASM_PREFIX		"Diasm: "
#define AGENT_PREFIX		"Agent: "

#define COLOR	"\033[1;35m"
#define ENDC	"\033[0m"

/* _PRINTF is the standard logging enabled with -D */
/* _DEBUG is activated with -d kafl cmdline */
/* _ERROR is printed to stdout (or logged if logging is enabled) */
//#define DEBUG_QEMU
#ifdef DEBUG_QEMU
#define QEMU_PT_PRINTF(PREFIX, format, ...) qemu_log(QEMU_PT_PREFIX PREFIX format "\n", ##__VA_ARGS__)
#define QEMU_PT_ERROR(PREFIX, format, ...)  printf(QEMU_PT_PREFIX PREFIX format "\n", ##__VA_ARGS__)
#else
#define QEMU_PT_PRINTF(PREFIX, format, ...) (void)0
#define QEMU_PT_ERROR(PREFIX, format, ...)  (void)0
#endif


#define PT_DEBUG_DISABLE
#ifndef PT_DEBUG_DISABLE
#define QEMU_PT_DEBUG(PREFIX, format, ...)  qemu_log_mask(LOG_KAFL, QEMU_PT_PREFIX PREFIX format "\n", ##__VA_ARGS__)
#else
#define QEMU_PT_DEBUG(PREFIX, format, ...)  (void)0
#endif
