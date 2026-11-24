/* 固定 ARM64 跳板：宿主实现使用宿主栈，相同函数保持相同指针。 */
#include "runtime.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
extern char runtime_hle_thunks[];
void *runtime_hle_targets[4096];
int runtime_hle_trace;
static const char *names[4096];
static unsigned used;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
void *runtime_hle_wrap_named(void *fn,const char *name){
 if(!fn)return NULL;pthread_mutex_lock(&lock);unsigned i=0;while(i<used&&runtime_hle_targets[i]!=fn)i++;
 if(i==used){if(used==4096)runtime_fail("宿主调用跳板表已满");__atomic_store_n(&runtime_hle_targets[used++],fn,__ATOMIC_RELEASE);}
 if(!names[i]&&name)names[i]=strdup(name);pthread_mutex_unlock(&lock);return runtime_hle_thunks+(size_t)i*16;
}
void *runtime_hle_wrap(void *fn){return runtime_hle_wrap_named(fn,NULL);}
void runtime_hle_note(uint64_t index){if(index<4096)fprintf(stderr,"[pmttyd] 宿主调用 %s\n",names[index]?names[index]:"未命名");}

extern uint64_t runtime_hle_tsd_off;
void runtime_hle_stack_set(uintptr_t top){pthread_setspecific((pthread_key_t)(runtime_hle_tsd_off/8),(void *)(top&~(uintptr_t)15));}
