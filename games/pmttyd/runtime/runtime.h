#pragma once
#include <stdint.h>
#include <pthread.h>
extern pthread_key_t runtime_keys[8];
_Noreturn void runtime_fail(const char *message);
void runtime_trace_guest(uint64_t fp,uint64_t lr);

void *runtime_codec_override(const char *name);
void runtime_audio_probe(void);
uint64_t runtime_now_ns(void);
#include <stddef.h>
void dm_guest_write_prepare(void *addr,size_t size);
void *runtime_symbol(const char *name);
void runtime_thread_init(void);
uint32_t svc_thread_main_init(void);
int svc_thread(int number,uint64_t *regs);
int runtime_guest_code_pc(uint64_t pc);
void runtime_hle_stack_set(uintptr_t top);
void runtime_core_checkpoint(void);
int runtime_core_away(void);
void runtime_core_back(int *away);
void runtime_dump_threads(void);
