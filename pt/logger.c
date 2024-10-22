/* 
 * *
 * Sergej Schumilo, 2019 <sergej@schumilo.de>
 * Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include "pt/logger.h"

#ifdef SAMPLE_RAW_SINGLE
#define SAMPLE_RAW_SINGLE_TARGET "/dev/shm/kafl_pt_%d"

int sample_raw_single_id = 0;
FILE* sample_raw_single_file = NULL;

void init_sample_raw_single(uint32_t id){
	sample_raw_single_id = id;
	char name[256];
	snprintf(name, 256, SAMPLE_RAW_SINGLE_TARGET, sample_raw_single_id);
	if (sample_raw_single_file)
		fclose(sample_raw_single_file);
	sample_raw_single_file = fopen(name, "wb"); 
}

void sample_raw_single(void* buffer, int bytes){
	if (sample_raw_single_file){
		fwrite(buffer, sizeof(char), bytes, sample_raw_single_file);
		fflush(sample_raw_single_file);
	}
}
#endif

#ifdef SAMPLE_RAW
#define SAMPLE_RAW_TARGET "/tmp/sample_raw_%d"

int sample_raw_id = 0;
FILE* sample_raw_file = NULL;

void init_sample_raw(void){
	char name[256];
	snprintf(name, 256, SAMPLE_RAW_TARGET, sample_raw_id++);
	if (sample_raw_file)
		fclose(sample_raw_file);
	sample_raw_file = fopen(name, "wb"); 
}

void sample_raw(void* buffer, int bytes){
	if (sample_raw_file)
		fwrite(buffer, sizeof(char), bytes, sample_raw_file);
}
#endif

#ifdef SAMPLE_DECODED
#define SAMPLE_DECODED_TARGET "/tmp/traces/sample_decoded_%d"

int sample_decoded_id = 0;
FILE* sample_decoded_file = NULL;

void init_sample_decoded(void){
	char name[256];
	snprintf(name, 256, SAMPLE_DECODED_TARGET, sample_decoded_id++);
	if (sample_decoded_file)
		fclose(sample_decoded_file);
	sample_decoded_file = fopen(name, "w"); 
}

void sample_decoded(uint64_t addr){
	if (sample_decoded_file)
		fprintf(sample_decoded_file, "%lx\n", addr);
}
#endif

#ifdef SAMPLE_DECODED_DETAILED
#define SAMPLE_DETAILED_TARGET "/tmp/traces/sample_detailed_%d"

int sample_detailed_id = 0;
FILE* sample_detailed_file = NULL;

void init_sample_decoded_detailed(void){
	char name[256];
	snprintf(name, 256, SAMPLE_DETAILED_TARGET, sample_detailed_id++);
	if (sample_detailed_file)
		fclose(sample_detailed_file);
	sample_detailed_file = fopen(name, "w"); 
}
#endif

void sample_decoded_detailed(const char *format, ...){
	#ifdef SAMPLE_DECODED_DETAILED
	va_list args;
	va_start(args, format);
	printf(format, args);
	va_end(args);
	#endif
}
