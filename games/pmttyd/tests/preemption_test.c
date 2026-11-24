#define main guest_loader_main
#include "../runtime/native_probe.c"
#undef main
#include <assert.h>
int main(void){
 static Record records[4];for(unsigned i=0;i<4;i++)modules[i].record=records+i;
 for(unsigned i=0;i<8;i++)assert(!pthread_key_create(&keys[i],NULL));runtime_hle_tsd_off=(uint64_t)keys[4]*8;runtime_thread_init();svc_thread_main_init();
 volatile uint16_t *tlr=pthread_getspecific(keys[2]);tlr[0x100/2]=0;tlr[0x102/2]=1;
 uint64_t regs[31];for(unsigned i=0;i<31;i++)regs[i]=0x12340000+i;
 assert(memory_svc(0x36,regs)==1&&regs[0]==0&&tlr[0x102/2]==0&&tlr[0x100/2]==0);
 for(unsigned i=1;i<31;i++)assert(regs[i]==0x12340000+i);
 static uint8_t image[16];static Gate gate={.ret=8,.arg=0x36};table=(uint8_t *)&gate;modules[0].base=image;records[0].ngate=1;
 tlr[0x102/2]=1;regs[0]=99;runtime_dispatch((uintptr_t)image+8,regs);assert(regs[0]==0&&!tlr[0x102/2]);for(unsigned i=1;i<31;i++)assert(regs[i]==0x12340000+i);
 puts("延期抢占中断标志、SVC 分派与寄存器保持测试通过");return 0;
}
