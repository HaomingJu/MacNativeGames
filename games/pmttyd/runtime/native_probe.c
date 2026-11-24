/* 原生装载探针：只执行 SDK 计时函数，不把探针成功当作游戏启动。 */
#include <stdint.h>
#include <stdatomic.h>
#include "ipc.h"
#include "runtime.h"
#ifdef PMTTYD_METAL
#include "save_host.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>
#include <CommonCrypto/CommonDigest.h>
#include <mach/mach_time.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

typedef struct { char magic[8],version[8]; uint32_t count,format; uint64_t plt,slot,strings,string_size,size; } Header;
typedef struct { char name[16],digest[72]; uint64_t size,text,ro,data,patch,key,init,ctor,array,array_size,symbols,relocs,gates; uint32_t nsym,nrel,ngate,reserved; } Record;
typedef struct { uint64_t value,size; uint32_t name; uint8_t defined,binding,type,reserved; } Symbol;
typedef struct { uint64_t where; uint32_t kind,symbol; int64_t addend; } Reloc;
typedef struct { uint64_t ret,original; uint32_t timer,arg; } Gate;
typedef struct { const Record *record; uint8_t *base; const Symbol *symbols; } Module;
_Static_assert(sizeof(Header)==64 && sizeof(Record)==208 && sizeof(Symbol)==24,"表结构");
static Module modules[4];
static const Header *header;
static const uint8_t *table;
pthread_key_t runtime_keys[8];
#define keys runtime_keys
uint64_t runtime_hle_tsd_off;
extern void runtime_gate(void);
#ifdef PMTTYD_METAL
extern void runtime_front_run(void (*callback)(void));
extern void runtime_app_arguments(int *argc,char ***argv);
extern void *runtime_gfx_override(const char *name);
extern int runtime_gfx_fault(void *,int,void *);
#else
/* 无图形探针不提供 Metal 替换，遇到原始图形服务会明确停止。 */
static void runtime_front_run(void (*callback)(void)){callback();}
static void *runtime_gfx_override(const char *name){(void)name;return NULL;}
static int runtime_gfx_fault(void *addr,int write,void *context){(void)addr;(void)write;(void)context;return 0;}
#endif
static _Atomic unsigned gate_calls;
static __thread uint64_t last_guest_fp,last_guest_lr;
static void need(int ok,const char *why) { if(!ok){fprintf(stderr,"[pmttyd] 失败：%s\n",why);exit(1);} }
_Noreturn void runtime_fail(const char *message){fprintf(stderr,"[pmttyd] %s\n",message);exit(2);}
void runtime_trace_guest(uint64_t fp,uint64_t lr){
 for(unsigned depth=0;depth<16;depth++){
  for(unsigned i=0;i<4;i++)if(lr>=(uintptr_t)modules[i].base&&lr-(uintptr_t)modules[i].base<modules[i].record->size)fprintf(stderr,"[pmttyd] 回溯 %s+%#llx\n",modules[i].record->name,(unsigned long long)(lr-(uintptr_t)modules[i].base));
  uint64_t frame[2];mach_vm_size_t got=0;
  if(!fp||mach_vm_read_overwrite(mach_task_self(),fp,16,(uintptr_t)frame,&got)!=KERN_SUCCESS||got!=16)break;
  lr=frame[1];if(frame[0]<=fp)break;fp=frame[0];
 }
}
static void *private_memory[256];
static pthread_mutex_t private_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t private_new(void *object){pthread_mutex_lock(&private_lock);for(unsigned i=1;i<256;i++)if(!private_memory[i]){private_memory[i]=object;pthread_mutex_unlock(&private_lock);return 0x08000000u+i;}runtime_fail("共享内存句柄表已满");}
static void *private_get(uint32_t h){unsigned i=h-0x08000000u;pthread_mutex_lock(&private_lock);void *p=i<256?private_memory[i]:NULL;pthread_mutex_unlock(&private_lock);return p;}
static size_t up(size_t n){return (n+16383)&~(size_t)16383;}
static uint8_t *read_file(const char *path,size_t *size){
 FILE *f=fopen(path,"rb");need(f!=NULL,"打开文件");need(fseek(f,0,SEEK_END)==0,"文件大小");long n=ftell(f);need(n>0,"空文件");rewind(f);
 uint8_t *p=malloc((size_t)n);need(p!=NULL,"分配文件缓冲区");need(fread(p,1,n,f)==(size_t)n,"读取文件");fclose(f);*size=(size_t)n;return p;
}
static void range(uint64_t off,uint64_t count,uint64_t stride){need(off<=header->size&&count<=(header->size-off)/stride,"表范围");}
static const char *name_of(const Symbol *s){need(s->name<header->string_size,"符号名称范围");const char *p=(const char *)table+header->strings+s->name;need(memchr(p,0,header->string_size-s->name)!=NULL,"名称终止符");return p;}
static uintptr_t find(const char *name){
 for(unsigned i=0;i<4;i++)for(unsigned j=0;j<modules[i].record->nsym;j++){
  const Symbol *s=modules[i].symbols+j;
  if(s->defined&&strcmp(name_of(s),name)==0){need(s->value<=modules[i].record->key,"符号地址范围");return (uintptr_t)modules[i].base+s->value;}
 }
 return 0;
}
int runtime_module_offset(uint64_t pc,const char **name,uint64_t *offset){for(unsigned i=0;i<4;i++)if(pc>=(uintptr_t)modules[i].base&&pc-(uintptr_t)modules[i].base<modules[i].record->size){*name=modules[i].record->name;*offset=pc-(uintptr_t)modules[i].base;return 1;}return 0;}
int runtime_guest_code_pc(uint64_t pc){for(unsigned i=0;i<4;i++)if(pc>=(uintptr_t)modules[i].base&&pc-(uintptr_t)modules[i].base<modules[i].record->text)return 1;return 0;}
void *runtime_symbol(const char *name){return (void *)find(name);}
uint64_t runtime_now_ns(void){mach_timebase_info_data_t tb;mach_timebase_info(&tb);return (uint64_t)((__uint128_t)mach_absolute_time()*tb.numer/tb.denom);}
void runtime_describe_address(uint64_t address,char *out,size_t size){const char *name;uint64_t offset;if(runtime_module_offset(address,&name,&offset))snprintf(out,size,"%s+%#llx",name,(unsigned long long)offset);else snprintf(out,size,"%#llx",(unsigned long long)address);}
static uint32_t event_new(void){uint32_t writable=runtime_event_new();return runtime_event_readable(writable);}
void runtime_thread_init(void){
 if(!pthread_getspecific(keys[1])){void *tls=calloc(1,8192),*tlr=calloc(1,512),*dummy=calloc(1,1024);need(tls&&tlr&&dummy,"游戏线程环境");memcpy((char *)tlr+0x1f8,&dummy,8);need(!pthread_setspecific(keys[0],NULL)&&!pthread_setspecific(keys[1],tls)&&!pthread_setspecific(keys[2],tlr)&&!pthread_setspecific(keys[3],(void *)runtime_gate),"游戏线程槽");}
}
typedef struct {uint8_t *data;uint64_t size;} HostSharedMemory;
static struct {uint64_t address,size;} shared_maps[64];
static unsigned shared_map_count;
#define HEAP_CAP (4ULL<<30)
#define STACK_CAP (256ULL<<20)
#define ALIAS_CAP (64ULL<<30)
#define SHARED_CAP (256ULL<<20)
static uint8_t *heap_region,*stack_region,*alias_region,*shared_region;
static uint64_t heap_bytes,alias_bytes;
static uint8_t alias_pages[ALIAS_CAP/4096];
static struct {uint64_t dst,src,size;} stack_maps[256];
static unsigned stack_map_count;
static uint8_t *reserve_at(uint64_t address,uint64_t size){
 mach_vm_address_t at=address;
 kern_return_t kr=mach_vm_allocate(mach_task_self(),&at,size,VM_FLAGS_FIXED);
 if(kr!=KERN_SUCCESS)fprintf(stderr,"[pmttyd] 地址 %#llx 大小 %#llx：%s\n",(unsigned long long)address,(unsigned long long)size,mach_error_string(kr));
 need(kr==KERN_SUCCESS,"预留指定空闲地址区域");
 need(mach_vm_protect(mach_task_self(),at,size,FALSE,VM_PROT_NONE)==KERN_SUCCESS,"区域初始不可访问");
 return (uint8_t *)(uintptr_t)at;
}
static void memory_init(void){
 if(heap_region)return;
 for(uint64_t candidate=0x300000000ULL;candidate<0xf00000000ULL;candidate+=0x200000000ULL){
  mach_vm_address_t at=candidate;
  if(mach_vm_allocate(mach_task_self(),&at,HEAP_CAP+STACK_CAP,VM_FLAGS_FIXED)!=KERN_SUCCESS)continue;
  need(mach_vm_protect(mach_task_self(),at,HEAP_CAP+STACK_CAP,FALSE,VM_PROT_NONE)==KERN_SUCCESS,"堆栈区域权限");
  heap_region=(void *)(uintptr_t)at;stack_region=heap_region+HEAP_CAP;break;
 }
 need(heap_region!=NULL,"寻找空闲的客体堆栈区域");
 alias_region=reserve_at(0x7000000000ULL,ALIAS_CAP);
 for(uint64_t candidate=0x600000000ULL;candidate<0xf00000000ULL;candidate+=0x20000000ULL){
  mach_vm_address_t at=candidate;if(mach_vm_allocate(mach_task_self(),&at,SHARED_CAP,VM_FLAGS_FIXED)!=KERN_SUCCESS)continue;
  need(mach_vm_protect(mach_task_self(),at,SHARED_CAP,FALSE,VM_PROT_NONE)==KERN_SUCCESS,"共享地址区权限");shared_region=(void *)(uintptr_t)at;break;
 }
 need(shared_region!=NULL,"寻找空闲共享地址区");
 need(heap_region!=MAP_FAILED&&stack_region!=MAP_FAILED&&alias_region!=MAP_FAILED,"预留客体地址空间");
 need((uintptr_t)heap_region+HEAP_CAP<(1ULL<<39)&&(uintptr_t)alias_region+ALIAS_CAP<=(1ULL<<39),"HIPC 39 位地址范围");
}
static int memory_svc(unsigned num,uint64_t *r){
 if(num==0x16&&private_get((uint32_t)r[0])){unsigned i=(uint32_t)r[0]-0x08000000u;pthread_mutex_lock(&private_lock);private_memory[i]=NULL;pthread_mutex_unlock(&private_lock);r[0]=0;return 1;}
 if(svc_thread((int)num,r))return 1;
 if(num==0x36){volatile uint16_t *tlr=pthread_getspecific(keys[2]);need(tlr!=NULL,"抢占同步线程环境");tlr[0x102/2]=0;r[0]=0;return 1;}
 if(num>=0x100&&num<0x120){unsigned reg=num-0x100;if(reg==18)pthread_setspecific(keys[0],(void *)0x8555c005ULL);else if(reg<31)r[reg]=0x8555c005ULL;return 1;}
 if(num>=0x200&&num<0x220){unsigned reg=num-0x200;uintptr_t address=reg==18?(uintptr_t)pthread_getspecific(keys[0]):reg<31?r[reg]:0;sys_dcache_flush((void *)(address&~127ULL),128);return 1;}
 if(num==0x14){
  runtime_trace_guest(r[29],r[30]);
  fprintf(stderr,"[pmttyd] 解除共享页 %#llx+%#llx\n",(unsigned long long)r[1],(unsigned long long)r[2]);
  unsigned found=shared_map_count;for(unsigned i=0;i<shared_map_count;i++)if(shared_maps[i].address==r[1]&&shared_maps[i].size==r[2])found=i;
  if(found==shared_map_count){r[0]=0xd401;return 1;}uint64_t lo=r[1]&~16383ULL,hi=up(r[1]+r[2]);need(mprotect((void *)(uintptr_t)lo,hi-lo,PROT_NONE)==0,"解除时间共享映射");shared_maps[found]=shared_maps[--shared_map_count];r[0]=0;return 1;
 }

 if(num==0x13){
  fprintf(stderr,"[pmttyd] 共享页映射 %#llx+%#llx\n",(unsigned long long)r[1],(unsigned long long)r[2]);
  HostSharedMemory *memory=private_get((uint32_t)r[0]);if(!memory||r[2]!=memory->size||(r[1]&4095)){r[0]=0xe401;return 1;}
  uint64_t lo=r[1]&~16383ULL,hi=up(r[1]+r[2]),base=(uintptr_t)shared_region;
  if(lo<base||hi>base+SHARED_CAP){r[0]=0xd401;return 1;}
  for(unsigned i=0;i<shared_map_count;i++)if(r[1]<shared_maps[i].address+shared_maps[i].size&&shared_maps[i].address<r[1]+r[2]){r[0]=0xd401;return 1;}
  need(mprotect((void *)(uintptr_t)lo,hi-lo,PROT_READ|PROT_WRITE)==0,"提交共享页");
  need(shared_map_count<64,"共享映射表容量");shared_maps[shared_map_count].address=r[1];shared_maps[shared_map_count++].size=r[2];
  memcpy((void *)(uintptr_t)r[1],memory->data,memory->size);
  need(mprotect((void *)(uintptr_t)lo,hi-lo,PROT_READ)==0,"时间共享页只读");r[0]=0;return 1;
 }

 if(num==0x26){uint32_t result=0;if(r[1]&&r[2]>=4)memcpy(&result,(void *)(uintptr_t)r[1],4);fprintf(stderr,"[pmttyd] SDK Break：reason=%#llx result=%#x\n",(unsigned long long)r[0],result);runtime_trace_guest(r[29],r[30]);runtime_fail("原始 SDK 主动中止");}

 if(num==4||num==5){
  memory_init();uint64_t dst=r[0],src=r[1],size=r[2],base=(uintptr_t)stack_region;
  fprintf(stderr,"[pmttyd] 栈映射 SVC %u：dst=%#llx src=%#llx size=%#llx base=%#llx\n",num,(unsigned long long)dst,(unsigned long long)src,(unsigned long long)size,(unsigned long long)base);
  if(!size||((dst|src|size)&4095)||dst<base||size>STACK_CAP||dst-base>STACK_CAP-size){r[0]=0xdc01;return 1;}
  unsigned found=stack_map_count;for(unsigned i=0;i<stack_map_count;i++)if(stack_maps[i].dst==dst&&stack_maps[i].src==src&&stack_maps[i].size==size)found=i;
  uint64_t lo=dst&~16383ULL,hi=up(dst+size),slo=up(src),shi=(src+size)&~16383ULL;
  if(num==4){
   need(stack_map_count<256,"栈映射表容量");
   for(unsigned i=0;i<stack_map_count;i++)need(dst+size<=stack_maps[i].dst||dst>=stack_maps[i].dst+stack_maps[i].size,"栈映射不能重叠");
   need(mprotect((void *)(uintptr_t)lo,hi-lo,PROT_READ|PROT_WRITE)==0,"提交栈页");memcpy((void *)(uintptr_t)dst,(void *)(uintptr_t)src,size);
   if(shi>slo)need(mprotect((void *)(uintptr_t)slo,shi-slo,PROT_NONE)==0,"栈映射源页权限");
   stack_maps[stack_map_count].dst=dst;stack_maps[stack_map_count].src=src;stack_maps[stack_map_count++].size=size;
  }else{
   if(found==stack_map_count){r[0]=0xd401;return 1;}
   if(shi>slo)need(mprotect((void *)(uintptr_t)slo,shi-slo,PROT_READ|PROT_WRITE)==0,"恢复栈源页");
   memcpy((void *)(uintptr_t)src,(void *)(uintptr_t)dst,size);stack_maps[found]=stack_maps[--stack_map_count];
  }
  r[0]=0;return 1;
 }

 if(num==0x2c){
  memory_init();uint64_t address=r[0],size=r[1],base=(uintptr_t)alias_region;
  fprintf(stderr,"[pmttyd] 物理映射 %#llx+%#llx\n",(unsigned long long)address,(unsigned long long)size);
  if((address&4095)||(size&4095)||!size||address<base||address-base>ALIAS_CAP||size>ALIAS_CAP-(address-base)){r[0]=0xdc01;return 1;}
  unsigned first=(unsigned)((address-base)/4096),pages=(unsigned)(size/4096);
  uint64_t fresh=0;for(unsigned i=0;i<pages;i++)if(!alias_pages[first+i])fresh+=4096;
  uint64_t images=0;for(unsigned i=0;i<4;i++)images+=modules[i].record->key;
  if(fresh+alias_bytes+heap_bytes+images>0xcd500000ULL){r[0]=0x10801;return 1;}
  need(mprotect((void *)(uintptr_t)(address&~16383ULL),up((address&16383)+size),PROT_READ|PROT_WRITE)==0,"映射别名区域宿主页");
  memset(alias_pages+first,1,pages);alias_bytes+=fresh;r[0]=0;return 1;
 }

 if(num==6){
  uint64_t addr=r[2],base=addr&~0xfffULL,size=4096;unsigned state=0,perm=0;
  mach_vm_address_t at=addr;mach_vm_size_t span=0;vm_region_basic_info_data_64_t ri;
  mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=0;
  kern_return_t kr=mach_vm_region(mach_task_self(),&at,&span,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&ri,&count,&object);
  if(object)mach_port_deallocate(mach_task_self(),object);
  if(kr==KERN_SUCCESS){
   if(at>addr){base=addr&~0xfffULL;size=at-base;
    for(unsigned i=0;i<shared_map_count;i++)if((shared_maps[i].address&~16383ULL)==at)size=shared_maps[i].address-base;
   }
   else{base=at;size=span;perm=ri.protection;state=perm?3:0;}
  }
  uintptr_t top=(uintptr_t)pthread_get_stackaddr_np(pthread_self());size_t stack=pthread_get_stacksize_np(pthread_self());
  if(addr>=top-stack&&addr<top){base=top-stack;size=stack;state=0xb;perm=3;}
  for(unsigned i=0;i<shared_map_count;i++){
   uint64_t lo=shared_maps[i].address&~16383ULL,hi=up(shared_maps[i].address+shared_maps[i].size);
   if(addr>=lo&&addr<hi){
    if(addr<shared_maps[i].address){base=lo;size=shared_maps[i].address-lo;state=0;perm=0;}
    else if(addr>=shared_maps[i].address+shared_maps[i].size){base=shared_maps[i].address+shared_maps[i].size;
     mach_vm_address_t next=hi;mach_vm_size_t span_next;vm_region_basic_info_data_64_t info_next;mach_msg_type_number_t n=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t port=0;
     kern_return_t next_result=mach_vm_region(mach_task_self(),&next,&span_next,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info_next,&n,&port);if(port)mach_port_deallocate(mach_task_self(),port);
     size=(next_result==KERN_SUCCESS?next:(1ULL<<39))-base;state=0;perm=0;}
    else{base=shared_maps[i].address;size=shared_maps[i].size;state=6;perm=1;}
   }
  }
  if(addr>=(uintptr_t)shared_region&&addr-(uintptr_t)shared_region<SHARED_CAP){
   base=(uintptr_t)shared_region;uint64_t end=base+SHARED_CAP;state=0;perm=0;
   for(unsigned i=0;i<shared_map_count;i++){
    uint64_t finish=shared_maps[i].address+shared_maps[i].size;
    if(finish<=addr&&finish>base)base=finish;
    if(shared_maps[i].address>addr&&shared_maps[i].address<end)end=shared_maps[i].address;
   }
   size=end-base;
   for(unsigned i=0;i<shared_map_count;i++)if(addr>=shared_maps[i].address&&addr-shared_maps[i].address<shared_maps[i].size){base=shared_maps[i].address;size=shared_maps[i].size;state=6;perm=1;}
  }
  unsigned attribute=0;
  if(addr>=(uintptr_t)stack_region&&addr-(uintptr_t)stack_region<STACK_CAP){
   base=(uintptr_t)stack_region;uint64_t end=base+STACK_CAP;state=0;perm=0;
   for(unsigned i=0;i<stack_map_count;i++){
    uint64_t finish=stack_maps[i].dst+stack_maps[i].size;
    if(finish<=addr&&finish>base)base=finish;
    if(stack_maps[i].dst>addr&&stack_maps[i].dst<end)end=stack_maps[i].dst;
   }
   size=end-base;
  }
  for(unsigned i=0;i<stack_map_count;i++){
   if(addr>=stack_maps[i].dst&&addr-stack_maps[i].dst<stack_maps[i].size){base=stack_maps[i].dst;size=stack_maps[i].size;state=0xb;perm=3;}
   if(addr>=stack_maps[i].src&&addr-stack_maps[i].src<stack_maps[i].size){base=stack_maps[i].src;size=stack_maps[i].size;state=5;perm=0;attribute=4;}
  }
  struct {uint64_t base,size;uint32_t state,attr,perm,ipc,device,pad;} info={base,size,state,attribute,perm,0,0,0};
  if(r[0])memcpy((void *)(uintptr_t)r[0],&info,sizeof info);r[0]=0;r[1]=0;return 1;
 }

 if(num==0x29){
  uint64_t value=0;memory_init();
  switch(r[1]){
   case 0:value=7;break;case 1:value=0x0ffffffff0000000ULL;break;
   case 2:value=(uintptr_t)alias_region;break;case 3:value=ALIAS_CAP;break;
   case 4:value=(uintptr_t)heap_region;break;case 5:value=HEAP_CAP;break;
   case 6:case 21:value=0xcd500000ULL;break;
   case 7:case 22:for(unsigned i=0;i<4;i++)value+=modules[i].record->key;value+=heap_bytes+alias_bytes;break;
   case 11:arc4random_buf(&value,sizeof value);break;
   case 12:value=(uintptr_t)shared_region;break;case 13:value=SHARED_CAP;break;
   case 14:value=(uintptr_t)stack_region;break;case 15:value=STACK_CAP;break;
   case 16:value=8ULL<<20;break; /* 此本体 NPDM 的 +0x14 为 0x800000。 */
   case 18:value=0x0100ECD018EBE000ULL;break;
   default:return 0;
  }
  fprintf(stderr,"[pmttyd] GetInfo(%llu) = %#llx\n",(unsigned long long)r[1],(unsigned long long)value);
  r[0]=0;r[1]=value;return 1;
 }
 if(num==1){
  memory_init();uint64_t size=r[1];
  if(size>HEAP_CAP||(size&0x1fffff)){r[0]=0xca01;return 1;}
  if(size>heap_bytes)need(mprotect(heap_region+heap_bytes,size-heap_bytes,PROT_READ|PROT_WRITE)==0,"映射客体堆");
  if(size<heap_bytes)need(mprotect(heap_region+size,heap_bytes-size,PROT_NONE)==0,"缩小客体堆");
  heap_bytes=size;r[0]=0;r[1]=(uintptr_t)heap_region;return 1;
 }
 return 0;
}
void runtime_dispatch(uint64_t ret,uint64_t *regs){
 for(unsigned i=0;i<4;i++){
  const Record *r=modules[i].record;const Gate *g=(const void *)(table+r->gates);
  for(unsigned j=0;j<r->ngate;j++)if(ret==(uintptr_t)modules[i].base+g[j].ret){
   if(!g[j].timer){
    last_guest_fp=regs[29];last_guest_lr=regs[30];runtime_core_checkpoint();
    if(memory_svc(g[j].arg,regs)||runtime_ipc_svc(g[j].arg,regs))return;
    fprintf(stderr,"[pmttyd] 待实现 SVC %#x，来自 %s+%#llx\n",g[j].arg,r->name,(unsigned long long)g[j].original);
    fprintf(stderr,"[pmttyd] 参数 x0=%#llx x1=%#llx x2=%#llx x3=%#llx\n",(unsigned long long)regs[0],(unsigned long long)regs[1],(unsigned long long)regs[2],(unsigned long long)regs[3]);
    exit(2);
   }
   need(g[j].timer==1,"计时门类型");need(g[j].arg<31&&g[j].arg!=18,"计时寄存器");
   mach_timebase_info_data_t tb;mach_timebase_info(&tb);
   __uint128_t ns=(__uint128_t)mach_absolute_time()*tb.numer/tb.denom;
   regs[g[j].arg]=(uint64_t)(ns*12/625);gate_calls++;return;
  }
 }
 need(0,"未知宿主门返回点");
}
static void logger(IpcSession *session,const IpcRequest *request,IpcResponse *response){
 (void)response;
 if(request->cmd==0){
  const IpcBuffer *buffer=request->nx?request->x:request->na?request->a:NULL;
  need(buffer!=NULL&&buffer->size<=65536,"日志消息范围");
  const unsigned char *data=ipc_guest(buffer->addr,buffer->size);
  unsigned checksum=0;for(uint64_t i=0;i<buffer->size;i++)checksum+=data[i];
  fprintf(stderr,"[pmttyd] SDK 日志包：%llu 字节，校验和 %u\n",(unsigned long long)buffer->size,checksum);return;
 }
 ipc_unimplemented(session,request);
}
static void log_manager(IpcSession *session,const IpcRequest *request,IpcResponse *response){
 if(request->cmd==0){response->move[response->nmove++]=runtime_ipc_new_session("logger",logger,NULL,NULL);return;}
 ipc_unimplemented(session,request);
}
static void power_session(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 uint32_t *configuration=s->state;
 if(rq->cmd==0){uint32_t mode=IPC_ARG(rq,uint32_t,0),value=IPC_ARG(rq,uint32_t,4);if(mode>1){rs->result=0xd801;return;}configuration[mode]=value;return;}
 if(rq->cmd==1){uint32_t mode=IPC_ARG(rq,uint32_t,0);if(mode>1){rs->result=0xd801;return;}IPC_OUT(rs,configuration[mode]);return;}
 ipc_unimplemented(s,rq);
}
static void power_manager(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==0){uint32_t *configuration=calloc(2,sizeof *configuration);need(configuration!=NULL,"性能配置状态");rs->move[rs->nmove++]=runtime_ipc_new_session("apm-session",power_session,configuration,free);return;}
 ipc_unimplemented(s,rq);
}
typedef struct {uint32_t event,writable;unsigned pending;} AppState;
static pthread_mutex_t window_rights_lock=PTHREAD_MUTEX_INITIALIZER;
static IpcSession *window_rights_owner;
static uint8_t focus_handling[3],out_of_focus_suspending,operation_notifications,performance_notifications,resume_notifications;
static uint32_t terminate_result,cpu_boost_mode,exit_lock_count;
typedef struct {uint8_t data[0x88];_Atomic unsigned refs;} LaunchStorage;
static void launch_close(void *p){LaunchStorage *storage=p;if(atomic_fetch_sub(&storage->refs,1)==1)free(storage);}
static void launch_accessor(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 LaunchStorage *storage=s->state;
 fprintf(stderr,"[pmttyd] 启动参数访问器 cmd=%u data=%u b=%u\n",rq->cmd,rq->data_size,rq->nb);
 if(rq->cmd==0){uint64_t size=sizeof storage->data;IPC_OUT(rs,size);return;}
 if(rq->cmd==11){uint64_t offset=IPC_ARG(rq,uint64_t,0);if(offset>sizeof storage->data){rs->result=0xd401;return;}int pointer=!(rq->nb&&rq->b[0].size);IpcBuffer b=pointer&&rq->nc?rq->c[0]:rq->nb?rq->b[0]:(IpcBuffer){0};
  need(b.size&&b.size<=sizeof storage->data-offset,"启动参数读缓冲区");memcpy(ipc_guest(b.addr,b.size),storage->data+offset,b.size);if(pointer)rs->xout[rs->nxout++]=b;
  fprintf(stderr,"[pmttyd] 启动参数读取 offset=%llu size=%llu\n",(unsigned long long)offset,(unsigned long long)b.size);return;}
 ipc_unimplemented(s,rq);
}
static void launch_storage(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==0){LaunchStorage *storage=s->state;atomic_fetch_add(&storage->refs,1);rs->move[rs->nmove++]=runtime_ipc_new_session("launch-accessor",launch_accessor,storage,launch_close);return;}ipc_unimplemented(s,rq);
}
static unsigned launch_user_pending=1;
static void application_functions(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(!strcmp(s->name,"application-functions")&&rq->cmd==1){
  if(IPC_ARG(rq,uint32_t,0)!=2||!launch_user_pending){rs->result=0x480;return;}
  LaunchStorage *storage=calloc(1,sizeof *storage);need(storage!=NULL,"本地用户启动参数");storage->refs=1;
  uint32_t magic=0xc79497ca;uint64_t uid=1;memcpy(storage->data,&magic,4);storage->data[4]=1;memcpy(storage->data+8,&uid,8);
  rs->move[rs->nmove++]=runtime_ipc_new_session("launch-storage",launch_storage,storage,launch_close);launch_user_pending=0;return;
 } /* 已从 SDK 核对 0x88 字节预选用户布局；选择本地虚拟用户。 */
 if(!strcmp(s->name,"application-functions")&&rq->cmd==21){const char language[8]="zh-Hans";ipc_out(rs,language,8);return;}

 if(!strcmp(s->name,"application-functions")&&rq->cmd==22){terminate_result=IPC_ARG(rq,uint32_t,0);fprintf(stderr,"[pmttyd] 应用终止结果：%#x\n",terminate_result);return;}

 if(!strcmp(s->name,"application-functions")&&rq->cmd==130){rs->copy[rs->ncopy++]=event_new();return;}
 if(!strcmp(s->name,"self-controller")&&rq->cmd==1){exit_lock_count++;return;}
 if(!strcmp(s->name,"self-controller")&&rq->cmd==2){if(exit_lock_count)exit_lock_count--;else rs->result=0xd801;return;}
 if(!strcmp(s->name,"self-controller")&&rq->cmd==14){resume_notifications=IPC_ARG(rq,uint8_t,0);return;}
 if(!strcmp(s->name,"self-controller")&&rq->cmd==11){operation_notifications=IPC_ARG(rq,uint8_t,0);return;}
 if(!strcmp(s->name,"self-controller")&&rq->cmd==12){performance_notifications=IPC_ARG(rq,uint8_t,0);return;}
 if(!strcmp(s->name,"self-controller")&&rq->cmd==16){out_of_focus_suspending=IPC_ARG(rq,uint8_t,0);return;}
 if(!strcmp(s->name,"self-controller")&&rq->cmd==13){memcpy(focus_handling,ipc_arg(rq,0,3),3);return;}
 if(!strcmp(s->name,"self-controller")&&rq->cmd==91){uint64_t suspended=0;IPC_OUT(rs,suspended);return;}
 if(!strcmp(s->name,"common-state")){
  AppState *state=s->state;
  if(rq->cmd==5){uint8_t docked=1;ipc_out(rs,&docked,1);return;}
  if(rq->cmd==6){uint32_t mode=1;IPC_OUT(rs,mode);return;}
  if(rq->cmd==66){cpu_boost_mode=IPC_ARG(rq,uint32_t,0);return;}
  if(rq->cmd==0){rs->copy[rs->ncopy++]=state->event;return;}
  if(rq->cmd==1){if(!state->pending){rs->result=0x680;return;}state->pending=0;uint64_t regs[31]={0};regs[0]=state->event;svc_thread(0x12,regs);uint32_t message=15;IPC_OUT(rs,message);return;}
  if(rq->cmd==9){uint8_t focus=1;ipc_out(rs,&focus,sizeof focus);return;}
 }
 if(!strcmp(s->name,"common-state")&&rq->cmd==900){uint32_t region=0;IPC_OUT(rs,region);return;}
 if(!strcmp(s->name,"window-controller")&&rq->cmd==10){pthread_mutex_lock(&window_rights_lock);if(window_rights_owner&&window_rights_owner!=s)rs->result=0xd801;else window_rights_owner=s;pthread_mutex_unlock(&window_rights_lock);return;}
 if(!strcmp(s->name,"window-controller")&&rq->cmd==1){uint64_t id=(uint64_t)getpid();IPC_OUT(rs,id);return;}
 ipc_unimplemented(s,rq);
}
static void application_proxy(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==20){rs->move[rs->nmove++]=runtime_ipc_new_session("application-functions",application_functions,NULL,NULL);return;}
 const char *name=NULL;
 switch(rq->cmd){case 0:name="common-state";break;case 1:name="self-controller";break;case 2:name="window-controller";break;case 3:name="audio-controller";break;case 4:name="display-controller";break;case 11:name="library-applet-creator";break;case 1000:name="debug-functions";break;}
 if(name){
  AppState *state=NULL;
  if(!strcmp(name,"common-state")){state=calloc(1,sizeof *state);need(state!=NULL,"应用通知状态");state->writable=runtime_event_new();state->event=runtime_event_readable(state->writable);state->pending=1;runtime_event_signal(state->writable);}
  rs->move[rs->nmove++]=runtime_ipc_new_session(name,application_functions,state,free);return;
 }
 ipc_unimplemented(s,rq);
}
static void applet_manager(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==0){rs->move[rs->nmove++]=runtime_ipc_new_session("application-proxy",application_proxy,NULL,NULL);return;}
 ipc_unimplemented(s,rq);
}
static void addon_manager(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(!strcmp(s->name,"time-zone")&&rq->cmd==101){
  time_t timestamp=(time_t)IPC_ARG(rq,int64_t,0);struct tm value;need(localtime_r(&timestamp,&value)!=NULL,"日历时间转换");
  struct {int16_t year;int8_t month,day,hour,minute,second,pad;} calendar={(int16_t)(value.tm_year+1900),(int8_t)(value.tm_mon+1),(int8_t)value.tm_mday,(int8_t)value.tm_hour,(int8_t)value.tm_min,(int8_t)value.tm_sec,0};
  struct {int32_t weekday,year_day;char name[8];uint8_t dst,pad[3];int32_t offset;} info={.weekday=value.tm_wday,.year_day=value.tm_yday,.dst=value.tm_isdst>0,.offset=(int32_t)value.tm_gmtoff};
  _Static_assert(sizeof calendar==8&&sizeof info==24,"日历 IPC 布局");
  snprintf(info.name,sizeof info.name,"%s",value.tm_zone?value.tm_zone:"UTC");IPC_OUT(rs,calendar);IPC_OUT(rs,info);return;
 }
 if(!strcmp(s->name,"aoc:u")&&rq->cmd==50){return;} /* 未提供 DLC，没有待挂载附加内容。 */
 if(!strcmp(s->name,"aoc:u")&&(rq->cmd==0||rq->cmd==2)){uint32_t count=0;IPC_OUT(rs,count);return;}
 ipc_unimplemented(s,rq);
}
static void parental_control(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 (void)rs;if(rq->cmd==1){*(unsigned *)s->state=1;return;}ipc_unimplemented(s,rq);
}
static void parental_manager(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==0||rq->cmd==1){unsigned *state=calloc(1,sizeof *state);need(state!=NULL,"家长控制会话状态");rs->move[rs->nmove++]=runtime_ipc_new_session("parental-control",parental_control,state,free);return;}
 ipc_unimplemented(s,rq);
}
static void random_service(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 (void)rs;if(rq->cmd!=0){ipc_unimplemented(s,rq);}
 if(rq->nb!=1){runtime_fail("随机数输出缓冲区数量");}
 IpcBuffer b=rq->b[0];need(b.size<=1048576,"随机数请求大小");arc4random_buf(ipc_guest(b.addr,b.size),b.size);
}
static void time_manager(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==20){
  HostSharedMemory *memory=calloc(1,sizeof *memory);need(memory!=NULL,"时间共享内存对象");memory->size=4096;memory->data=calloc(1,4096);need(memory->data!=NULL,"时间共享页");
  uint8_t source[16];arc4random_buf(source,sizeof source);mach_timebase_info_data_t tb;mach_timebase_info(&tb);
  int64_t steady=(int64_t)((__uint128_t)mach_absolute_time()*tb.numer/tb.denom/1000000000ULL),offset=(int64_t)time(NULL)-steady;
  /* 已从本体 SDK SharedMemoryClient 核对：用户上下文 0x38、网络上下文 0x80、连续时钟 0xd0。 */
  for(unsigned pos=0x40;pos<=0x88;pos+=0x48){memcpy(memory->data+pos,&offset,8);memcpy(memory->data+pos+8,&steady,8);memcpy(memory->data+pos+16,source,16);}
  memcpy(memory->data+0x100,source,16);memory->data[0xcc]=1;
  rs->copy[rs->ncopy++]=private_new(memory);return;
 }

 const char *name=NULL;switch(rq->cmd){case 0:name="user-clock";break;case 1:name="network-clock";break;case 2:name="steady-clock";break;case 3:name="time-zone";break;case 4:name="local-clock";break;}
 if(name){rs->move[rs->nmove++]=runtime_ipc_new_session(name,addon_manager,NULL,NULL);return;}
 ipc_unimplemented(s,rq);
}
static void settings_service(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 static const char *const codes[]={"ja","en-US","fr","de","it","es","zh-CN","ko","nl","pt","ru","zh-TW","en-GB","fr-CA","es-419","zh-Hans","zh-Hant","pt-BR"};
 if(rq->cmd==0){const char code[8]="zh-Hans";ipc_out(rs,code,8);return;}
 if(rq->cmd==2){int32_t language=IPC_ARG(rq,int32_t,0);if(language<0||language>=18){rs->result=0x3c69;return;}char code[8]={0};memcpy(code,codes[language],strlen(codes[language]));ipc_out(rs,code,8);return;}
 if(rq->cmd==1||rq->cmd==5){
  IpcBuffer buffer=rq->nb?rq->b[0]:rq->nc?rq->c[0]:(IpcBuffer){0};unsigned limit=rq->cmd==1?15:18,count=0;
  uint64_t *out=ipc_guest(buffer.addr,buffer.size);for(;count<limit&&(count+1)*8<=buffer.size;count++){uint64_t value=0;memcpy(&value,codes[count],strlen(codes[count]));out[count]=value;}
  int32_t written=(int32_t)count;IPC_OUT(rs,written);if(!rq->nb&&rq->nc)rs->xout[rs->nxout++]=(IpcBuffer){buffer.addr,count*8,buffer.index};return;
 }
 if(rq->cmd==3||rq->cmd==6){int32_t count=rq->cmd==3?15:18;IPC_OUT(rs,count);return;}
 if(rq->cmd==4){int32_t region=0;IPC_OUT(rs,region);return;}
 if(rq->cmd==8){uint8_t kiosk=0;IPC_OUT(rs,kiosk);return;}
 ipc_unimplemented(s,rq);
}
static const char *romfs_path;
typedef struct {int fd;uint64_t size;} RomStorage;
static void storage_close(void *p){RomStorage *s=p;close(s->fd);free(s);}
static void storage_handler(IpcSession *session,const IpcRequest *rq,IpcResponse *rs){
 RomStorage *storage=session->state;
 if(rq->cmd==4){IPC_OUT(rs,storage->size);return;}
 if(rq->cmd==0){
  int64_t offset=IPC_ARG(rq,int64_t,0),length=IPC_ARG(rq,int64_t,8);
  if(offset<0||length<0||(uint64_t)offset>storage->size||(uint64_t)length>storage->size-(uint64_t)offset){rs->result=0xd401;return;}
  need(rq->nb==1&&(uint64_t)length<=rq->b[0].size,"RomFS 输出缓冲区");
  uint8_t *out=ipc_guest(rq->b[0].addr,rq->b[0].size);size_t done=0;
  while(done<(uint64_t)length){ssize_t n=pread(storage->fd,out+done,(size_t)length-done,offset+done);need(n>0,"读取 RomFS");done+=(size_t)n;}return;
 }
 ipc_unimplemented(session,rq);
}
static void filesystem_proxy(IpcSession *session,const IpcRequest *rq,IpcResponse *rs){
#ifdef PMTTYD_METAL
 if(rq->cmd==51){runtime_save_open(rs);return;}
#endif

 if(rq->cmd==1005){uint32_t mode=0;IPC_OUT(rs,mode);return;} /* 默认不记录文件访问日志。 */
 if(rq->cmd==1){return;} /* 初始化当前进程会话；数据仅供本进程使用。 */
 if(rq->cmd==200){
  need(romfs_path!=NULL,"boot 需提供已校验的 RomFS 路径");RomStorage *s=calloc(1,sizeof *s);need(s!=NULL,"RomFS 会话");s->fd=open(romfs_path,O_RDONLY);need(s->fd>=0,"打开已提取 RomFS");struct stat st;need(fstat(s->fd,&st)==0&&st.st_size>0,"RomFS 大小");s->size=(uint64_t)st.st_size;
  rs->move[rs->nmove++]=runtime_ipc_new_session("romfs-storage",storage_handler,s,storage_close);return;
 }
 ipc_unimplemented(session,rq);
}
static HostSharedMemory *hid_memory;
static unsigned npad_active;
static uint32_t npad_style_set,npad_ids[10];
static unsigned npad_id_count;
static uint8_t npad_assignment[10];
static void hid_resource(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==0){if(!hid_memory){hid_memory=calloc(1,sizeof *hid_memory);need(hid_memory!=NULL,"HID 共享内存对象");hid_memory->size=0x40000;hid_memory->data=calloc(1,hid_memory->size);need(hid_memory->data!=NULL,"HID 共享内存");}rs->copy[rs->ncopy++]=private_new(hid_memory);return;}ipc_unimplemented(s,rq);
}
static void vibration_list(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==0){uint32_t device=IPC_ARG(rq,uint32_t,0);uint32_t *state=s->state;if(state[0]>=127){rs->result=0xd801;return;}state[1+state[0]++]=device;return;}ipc_unimplemented(s,rq);
}
static void hid_service(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==200){struct {uint32_t type,position;} device={0,0};IPC_OUT(rs,device);return;} /* 未连接物理振动硬件。 */

 if(rq->cmd==203){uint32_t *state=calloc(128,sizeof *state);need(state!=NULL,"振动设备列表");rs->move[rs->nmove++]=runtime_ipc_new_session("vibration-list",vibration_list,state,free);return;}

 if(rq->cmd==120||rq->cmd==122){uint32_t id=IPC_ARG(rq,uint32_t,8),index=id==32?8:id;if(index>=10){rs->result=0xd801;return;}npad_assignment[index]=rq->cmd==120?1:0;return;}
 if(rq->cmd==102){need(rq->nx==1&&rq->x[0].size<=sizeof npad_ids&&!(rq->x[0].size&3),"Npad 标识清单大小");npad_id_count=(unsigned)(rq->x[0].size/4);memcpy(npad_ids,ipc_guest(rq->x[0].addr,rq->x[0].size),rq->x[0].size);return;}
 if(rq->cmd==103||rq->cmd==109){npad_active=1;return;}if(rq->cmd==104){npad_active=0;return;}
 if(rq->cmd==100){npad_style_set=IPC_ARG(rq,uint32_t,8);return;}if(rq->cmd==101){IPC_OUT(rs,npad_style_set);return;}

 if(rq->cmd==0){rs->move[rs->nmove++]=runtime_ipc_new_session("hid-app-resource",hid_resource,NULL,NULL);return;}
 ipc_unimplemented(s,rq);
}
static const uint64_t local_user[2]={1,0};
static uint64_t account_application_id;
static void local_account_manager(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==0){uint8_t online=0;IPC_OUT(rs,online);return;}ipc_unimplemented(s,rq);
}
static void account_service(IpcSession *s,const IpcRequest *rq,IpcResponse *rs){
 if(rq->cmd==101){if(memcmp(ipc_arg(rq,0,16),local_user,16)){rs->result=0xd07c;return;}rs->move[rs->nmove++]=runtime_ipc_new_session("local-account-manager",local_account_manager,NULL,NULL);return;}

 if(rq->cmd==150){uint8_t locked=0;IPC_OUT(rs,locked);return;}
 if(rq->cmd==51){uint8_t network=IPC_ARG(rq,uint8_t,0);const uint64_t none[2]={0,0};ipc_out(rs,network?none:local_user,16);return;}
 if(rq->cmd==0){int32_t count=1;IPC_OUT(rs,count);return;}
 if(rq->cmd==100||rq->cmd==160){account_application_id=IPC_ARG(rq,uint64_t,0);return;} /* 登记本地应用会话，不连接网络账号。 */
 if(rq->cmd==2||rq->cmd==3){need(rq->nb==1&&rq->b[0].size>=16,"账号列表输出空间");void *out=ipc_guest(rq->b[0].addr,rq->b[0].size);memset(out,0,rq->b[0].size);memcpy(out,local_user,16);return;}
 if(rq->cmd==1){uint8_t exists=!memcmp(ipc_arg(rq,0,16),local_user,16);IPC_OUT(rs,exists);return;}
 if(rq->cmd==4){ipc_out(rs,local_user,16);return;}
 ipc_unimplemented(s,rq);
}
static void ready(void){fprintf(stderr,"[pmttyd] SDK 装载回调\n");}
static void constructors(void){
 for(unsigned i=0;i<3;i++){Module *m=modules+i;fprintf(stderr,"[pmttyd] 初始化 %s\n",m->record->name);((void(*)(void))(m->base+m->record->init))();}
}
static void finished(void){need(0,"SDK 返回游戏退出回调");}
static uintptr_t sdk_start;
static void boot_sdk(void){
 if(!pthread_getspecific(keys[1])){
  void *tls=calloc(1,8192),*tlr=calloc(1,512);need(tls&&tlr,"游戏主线程环境");
  need(!pthread_setspecific(keys[1],tls)&&!pthread_setspecific(keys[2],tlr)&&!pthread_setspecific(keys[3],(void *)runtime_gate),"游戏主线程 TLS");
 }
 if(!pthread_getspecific(keys[4])){void *host=malloc(8u<<20);need(host!=NULL,"主线程宿主栈");need(!pthread_setspecific(keys[4],(char *)host+(8u<<20)-64),"主线程宿主栈槽");}
 runtime_thread_init();uint32_t main_handle=svc_thread_main_init();
 ((void(*)(uint64_t,uint64_t,void(*)(void),void(*)(void),void(*)(void)))sdk_start)(main_handle,0,ready,constructors,finished);
 runtime_fail("SDK Start 意外返回");
}
static void crash(int signal,siginfo_t *info,void *context){
 ucontext_t *u=context;
 if(runtime_gfx_fault(info->si_addr,(u->uc_mcontext->__es.__esr>>6)&1,context))return;
 uint64_t pc=u->uc_mcontext->__ss.__pc;
 fprintf(stderr,"[pmttyd] 信号 %d fault=%p pc=%#llx lr=%#llx\n",signal,info->si_addr,(unsigned long long)pc,(unsigned long long)u->uc_mcontext->__ss.__lr);
 for(unsigned i=0;i<4;i++)if(pc>=(uintptr_t)modules[i].base&&pc-(uintptr_t)modules[i].base<modules[i].record->size)fprintf(stderr,"[pmttyd] 指令位于 %s+%#llx\n",modules[i].record->name,(unsigned long long)(pc-(uintptr_t)modules[i].base));
 fprintf(stderr,"[pmttyd] sp=%#llx fp=%#llx esr=%#llx\n",(unsigned long long)u->uc_mcontext->__ss.__sp,(unsigned long long)u->uc_mcontext->__ss.__fp,(unsigned long long)u->uc_mcontext->__es.__esr);
 for(unsigned i=0;i<8;i++)fprintf(stderr,"[pmttyd] x%u=%#llx\n",i,(unsigned long long)u->uc_mcontext->__ss.__x[i]);
 runtime_trace_guest(u->uc_mcontext->__ss.__fp,u->uc_mcontext->__ss.__lr);_exit(128+signal);
}
int main(int argc,char **argv){
#ifdef PMTTYD_METAL
 runtime_app_arguments(&argc,&argv);
#endif
 struct sigaction action={0};action.sa_sigaction=crash;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,NULL);sigaction(SIGBUS,&action,NULL);sigaction(SIGILL,&action,NULL);

 need((argc==3||argc==4||argc==5),"用法：native-probe <runtime-tables.bin> <rewritten-dir>");need(sizeof(void*)==8,"64 位宿主");
 size_t size;table=read_file(argv[1],&size);need(size>=sizeof(Header),"表头大小");header=(const void *)table;
 need(!memcmp(header->magic,"TOTKTAB1",8)&&!memcmp(header->version,"pmttyd00",8)&&header->count==4&&header->format==1&&header->size==size,"表头");
 range(64,4,sizeof(Record));range(header->strings,header->string_size,1);
 for(unsigned i=0;i<8;i++)need(pthread_key_create(&keys[i],NULL)==0,"Darwin TLS 键");
 runtime_hle_tsd_off=(uint64_t)keys[4]*8;
 for(unsigned i=0;i<4;i++){
  Module *m=modules+i;const Record *r=(const Record *)(table+64)+i;m->record=r;
  need(memchr(r->name,0,16)!=NULL&&memchr(r->digest,0,72)!=NULL,"模块名称");
  range(r->symbols,r->nsym,24);range(r->relocs,r->nrel,24);range(r->gates,r->ngate,24);m->symbols=(const void *)(table+r->symbols);
  char path[4096];need(snprintf(path,sizeof path,"%s/%s.patched.bin",argv[2],r->name)<(int)sizeof path,"产物路径");
  size_t bytes;uint8_t *image=read_file(path,&bytes);need(bytes==r->size,"产物大小");
  unsigned char digest[32];char hex[65];CC_SHA256(image,(CC_LONG)bytes,digest);for(unsigned j=0;j<32;j++)snprintf(hex+j*2,3,"%02x",digest[j]);need(!strcmp(hex,r->digest),"产物 SHA256");
  need(r->text<=r->ro&&r->ro<=r->data&&r->data<=r->key&&r->key+16384<=r->patch&&r->patch<bytes,"模块布局");
  m->base=mmap(NULL,up(bytes),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);need(m->base!=MAP_FAILED,"映射模块");memcpy(m->base,image,bytes);free(image);
  uint64_t *key=(void *)(m->base+r->key);for(unsigned j=0;j<8;j++)key[j]=(uint64_t)keys[j]*8;
  fprintf(stderr,"[pmttyd] 映射 %s：%u 条重定位\n",r->name,r->nrel);
 }
 /* 首个计时探针不执行构造器；完整绑定先失败即停，不用空函数掩盖缺失符号。 */
 unsigned unresolved=0;
 for(unsigned i=0;i<4;i++){
  Module *m=modules+i;const Record *r=m->record;const Reloc *rel=(const void *)(table+r->relocs);
  for(unsigned j=0;j<r->nrel;j++){
   const Reloc *p=rel+j;need(p->where>=r->data&&p->where<=r->key-8&&p->symbol<r->nsym,"重定位范围");uintptr_t value;
   if(p->kind==1027){need(p->addend>=0&&(uint64_t)p->addend<=r->key,"相对重定位范围");value=(uintptr_t)m->base+p->addend;}
   else{
    need(p->kind==257||p->kind==1025||p->kind==1026,"重定位种类");const Symbol *s=m->symbols+p->symbol;
    value=(uintptr_t)runtime_gfx_override(name_of(s));
    if(!value)value=s->defined?(uintptr_t)m->base+s->value:find(name_of(s));
    if(!value&&s->binding!=2){if(unresolved++<20)fprintf(stderr,"[pmttyd] 未解析 %s：%s\n",r->name,name_of(s));continue;}
    if(p->kind!=1026)value+=p->addend;
   }
   memcpy(m->base+p->where,&value,8);
  }
 }
 need(!unresolved,"存在强导入未解析，拒绝执行");
 for(unsigned i=0;i<4;i++){
  Module *m=modules+i;const Record *r=m->record;
  need(mprotect(m->base,up(r->text),PROT_READ|PROT_EXEC)==0,"text RX");
  need(mprotect(m->base+r->patch,up(r->size)-r->patch,PROT_READ|PROT_EXEC)==0,"跳板 RX");
  need(mprotect(m->base+r->key,16384,PROT_READ)==0,"键页只读");
  sys_icache_invalidate(m->base,up(r->text));sys_icache_invalidate(m->base+r->patch,up(r->size)-r->patch);
 }
 void *tls=calloc(1,8192),*tlr=calloc(1,512);need(tls&&tlr,"客体 TLS");
 need(!pthread_setspecific(keys[1],tls)&&!pthread_setspecific(keys[2],tlr)&&!pthread_setspecific(keys[3],(void *)runtime_gate),"设置客体线程槽");
 uintptr_t tp;__asm__ volatile("mrs %0,tpidrro_el0":"=r"(tp));for(unsigned i=0;i<4;i++)need(*(void **)(tp+keys[i]*8)==pthread_getspecific(keys[i]),"Darwin 线程槽 ABI");
 uintptr_t entry=find("_ZN2nn2os13GetSystemTickEv");need(entry!=0,"SDK 计时入口");
 uint64_t previous=0;for(unsigned i=0;i<1000;i++){uint64_t now=((uint64_t(*)(void))entry)();need(now>=previous&&now>0,"SDK 计时返回值");previous=now;}
 need(gate_calls==1000,"计时门调用数量");
 if(argc>=4){
#ifdef PMTTYD_METAL
  setenv("TOTK_GPU_ALLOW_SUBMIT","1",1);
#endif

  unsetenv("TOTK_SHADER_CONTRIB"); /* 原型运行不发送玩家数据或着色器贡献。 */
  if(argc==5)romfs_path=argv[4];
  runtime_ipc_register("lm",log_manager,NULL,NULL);
  runtime_ipc_register("apm",power_manager,NULL,NULL);
  runtime_ipc_register("appletOE",applet_manager,NULL,NULL);
  runtime_ipc_register("aoc:u",addon_manager,NULL,NULL);
  runtime_ipc_register("ectx:aw",addon_manager,NULL,NULL);
  runtime_ipc_register("pctl",parental_manager,NULL,NULL);
  runtime_ipc_register("pctl:s",parental_manager,NULL,NULL);
  runtime_ipc_register("pctl:a",parental_manager,NULL,NULL);
  runtime_ipc_register("pctl:r",parental_manager,NULL,NULL);
  runtime_ipc_register("csrng",random_service,NULL,NULL);
  runtime_ipc_register("time:u",time_manager,NULL,NULL);
  runtime_ipc_register("set",settings_service,NULL,NULL);
  runtime_ipc_register("fsp-srv",filesystem_proxy,NULL,NULL);
  runtime_ipc_register("hid",hid_service,NULL,NULL);
  runtime_ipc_register("acc:u0",account_service,NULL,NULL);
  need(!strcmp(argv[3],"boot"),"未知模式");
  uintptr_t start=find("_ZN2nn4init5StartEmmPFvvES2_S2_");need(start!=0,"SDK 启动入口");
  fprintf(stderr,"[pmttyd] 进入原始 SDK Start（首次初始化诊断）\n");
  sdk_start=start;
  runtime_front_run(boot_sdk);
  need(0,"SDK Start 意外返回");
 }
 puts("[pmttyd] 原生 SDK 计时探针通过：1000 次客体 ARM64 → 宿主门 → 返回；未执行游戏入口。");return 0;
}
