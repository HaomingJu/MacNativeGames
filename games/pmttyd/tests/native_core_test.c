/* 合成数据验证内存、缓存门和成熟内核线程，测试不包含游戏内容。 */
#define main guest_loader_main
#include "../runtime/native_probe.c"
#undef main
#include <assert.h>
static _Atomic unsigned worker_ran;
static void guest_worker(uint64_t argument){(void)argument;worker_ran=1;uint64_t regs[31]={0};svc_thread(0x0a,regs);abort();}
int main(void){
 static Record records[4];for(unsigned i=0;i<4;i++)modules[i].record=records+i;
 for(unsigned i=0;i<8;i++)assert(!pthread_key_create(&keys[i],NULL));runtime_hle_tsd_off=(uint64_t)keys[4]*8;runtime_thread_init();svc_thread_main_init();
 uint64_t cache_regs[31];for(unsigned i=0;i<31;i++)cache_regs[i]=0x12340000+i;
 assert(memory_svc(0x109,cache_regs)&&cache_regs[9]==0x8555c005ULL);for(unsigned i=0;i<31;i++)if(i!=9)assert(cache_regs[i]==0x12340000+i);
 uint8_t *cache_memory=malloc(256);assert(cache_memory);memset(cache_memory,0x5a,256);cache_regs[9]=(uintptr_t)cache_memory;assert(memory_svc(0x209,cache_regs));for(unsigned i=0;i<256;i++)assert(cache_memory[i]==0x5a);free(cache_memory);
 memory_init();uint64_t regs[31]={0};regs[0]=(uintptr_t)alias_region;regs[1]=4096;assert(memory_svc(0x2c,regs)&&!regs[0]);*(uint32_t *)alias_region=0x1234abcd;
 regs[0]=(uintptr_t)alias_region;regs[1]=8192;assert(memory_svc(0x2c,regs)&&!regs[0]&&alias_bytes==8192&&*(uint32_t *)alias_region==0x1234abcd);
 void *stack=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(stack!=MAP_FAILED);
 memset(regs,0,sizeof regs);regs[1]=(uintptr_t)guest_worker;regs[3]=(uintptr_t)stack+16384;regs[4]=44;regs[5]=0;assert(svc_thread(0x08,regs)&&!regs[0]);uint32_t handle=(uint32_t)regs[1];
 regs[0]=handle;assert(svc_thread(0x09,regs)&&!regs[0]);regs[1]=(uintptr_t)&handle;regs[2]=1;regs[3]=5000000000ULL;assert(svc_thread(0x18,regs)&&!regs[0]);assert(worker_ran==1);puts("缓存维护、映射保留、原生线程启动/退出/等待测试通过");return 0;
}
