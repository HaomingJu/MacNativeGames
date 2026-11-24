#define main guest_loader_main
#include "../runtime/native_probe.c"
#undef main
#include <assert.h>
extern int mtl_probe_midclear(void);
extern int mtl_probe_integer_clear(void);
int main(void){
 setenv("TOTK_GPU_ALLOW_SUBMIT","1",1);
 for(unsigned i=0;i<8;i++)assert(!pthread_key_create(&keys[i],NULL));runtime_hle_tsd_off=(uint64_t)keys[4]*8;
 runtime_thread_init();svc_thread_main_init();
 assert(mtl_probe_integer_clear()==4096);assert(mtl_probe_midclear()==9216);
 puts("整数全目标清除、带掩码裁剪清除、32 位边界及浮点清除回归通过");return 0;
}
