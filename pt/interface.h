/*
 * *
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later 
 */


#ifndef INTERFACE_H
#define INTERFACE_H

#define INTEL_PT_MAX_RANGES			4

#define DEFAULT_IRPT_BITMAP_SIZE	    0x10000
#define DEFAULT_IRPT_COVERAGE_MAP_SIZE	0x80000

#define PROGRAM_SIZE				(128 << 18) /* 32MB Application Data */
#define PAYLOAD_SIZE				0x10000 	
#define INFO_SIZE					(128 << 10)	/* 128KB Info Data */
#define HPRINTF_SIZE				0x1000 		/* 4KB hprintf Data */

#define INFO_FILE					"/tmp/kAFL_info.txt"
#define HPRINTF_FILE				"/tmp/kAFL_printf.txt"

#define HPRINTF_LIMIT				512

#define KAFL_PROTO_ACQUIRE			'R'
#define KAFL_PROTO_RELEASE			'D'

#define KAFL_PROTO_RELOAD			'L'
#define KAFL_PROTO_FINALIZE			'F'

#define KAFL_PROTO_CRASH			'C'
#define KAFL_PROTO_KASAN			'K'
#define KAFL_PROTO_TIMEOUT		    't'
#define KAFL_PROTO_INFO				'I'

#define KAFL_PROTO_PRINTF			'X'

#define KAFL_PROTO_PT_TRASHED			'Z'	/* thank you Intel! */
#define KAFL_PROTO_PT_TRASHED_CRASH		'M'
#define KAFL_PROTO_PT_TRASHED_KASAN		'N'

#define KAFL_PROTO_PT_ABORT				'H'

/* kirasys */
#define KAFL_PROTO_LOCK              'l'
#define KAFL_PROTO_COVER_ON          'o'
#define KAFL_PROTO_COVER_OFF         'x'
#endif
