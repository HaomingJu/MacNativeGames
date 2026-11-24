/* 把本游戏的 NVN 动态入口绑定到自持 Metal 后端。 */
#include "runtime.h"
#include "movie_host.h"
#include "../third_party/d2r_gfx/gfx/nvn/nvn_mtl.h"
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>

extern void *runtime_hle_wrap_named(void *fn,const char *name);
typedef struct {const char *name;void *fn;const char *why;} GfxEntry;
typedef struct {const char *name;uint8_t *base;size_t size;} module_t;
uint64_t g_t0_ns;
uint64_t now_ns(void){mach_timebase_info_data_t t;mach_timebase_info(&t);return (uint64_t)((__uint128_t)mach_absolute_time()*t.numer/t.denom);}
uint64_t guest_ns(void){return now_ns();}
module_t *module_of(const void *p){(void)p;return NULL;}
static int (*fault_hook)(void *,int,void *);
static void (*write_hook)(void *,size_t);
void dm_fault_hook_register(int (*fn)(void *,int,void *)){if(fault_hook)runtime_fail("重复图形故障处理器");fault_hook=fn;}
void dm_guest_write_prep_register(void (*fn)(void *,size_t)){if(write_hook)runtime_fail("重复图形写入处理器");write_hook=fn;}
void dm_res_report_register(void (*fn)(uint32_t,const char *)){(void)fn;}
void dm_guest_write_prepare(void *p,size_t n){if(write_hook)write_hook(p,n);}
int runtime_gfx_fault(void *addr,int write,void *context){return fault_hook?fault_hook(addr,write,context):0;}
void crash_set_wait_dump(void (*fn)(void)){(void)fn;}
int dm_detclock_enabled(void){return 0;}
__thread sigjmp_buf *runtime_fault_guard;
_Atomic int runtime_survey_pending;
const int32_t runtime_survey_sync[2]={-1,-1};
void runtime_survey_tick(uint64_t frame){(void)frame;}
uintptr_t runtime_frame_controller_return_pc(void){return 0;}
void dm_guest_frame_tick(void){}
void dm_shot_written(const char *path,int ok){fprintf(stderr,"[pmttyd] 截图 %s：%s\n",path,ok?"成功":"失败");}
void *runtime_gfx_override(const char *name){unsigned long bytes=0;const GfxEntry *entries=(const void *)getsectiondata(&_mh_execute_header,"__DATA","totkgfx",&bytes);for(unsigned i=0;entries&&i<bytes/sizeof *entries;i++)if(!strcmp(name,entries[i].name))return runtime_hle_wrap_named(entries[i].fn,entries[i].name);void *fn=runtime_movie_override(name);if(!fn)fn=runtime_codec_override(name);return runtime_hle_wrap_named(fn,name);}
typedef void *(*Alloc)(unsigned long,unsigned long,void *);
typedef void (*Free)(void *,void *);
typedef void *(*Realloc)(void *,unsigned long,void *);
struct Allocator {Alloc alloc;Free free;Realloc realloc;void *user;};
struct Allocator g_nv_alloc,g_nv_devtools_alloc;
static void graphics_allocator(Alloc a,Free f,Realloc r,void *u){g_nv_alloc=(struct Allocator){a,f,r,u};}
static void graphics_dev_allocator(Alloc a,Free f,Realloc r,void *u){g_nv_devtools_alloc=(struct Allocator){a,f,r,u};}
static void graphics_initialize(void *memory,unsigned long size){if(!memory||!size)runtime_fail("图形工作内存无效");g_t0_ns=now_ns();mtl_init();fprintf(stderr,"[pmttyd] Metal 图形上下文已初始化\n");}
__attribute__((used,section("__DATA,totkgfx"))) static const GfxEntry initialize_entry={"_ZN2nv18InitializeGraphicsEPvm",graphics_initialize,"Metal 图形上下文"};
__attribute__((used,section("__DATA,totkgfx"))) static const GfxEntry allocator_entry={"_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_",graphics_allocator,"游戏分配器回调"};
__attribute__((used,section("__DATA,totkgfx"))) static const GfxEntry dev_allocator_entry={"_ZN2nv28SetGraphicsDevtoolsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_",graphics_dev_allocator,"游戏调试分配器回调"};

/* 尚未启用的可选功能：不改变游戏帧步长、比例和分辨率。 */
float runtime_aspect_now(void){return 0;}
void runtime_frame_step_update(uint64_t now){(void)now;}
int runtime_scene_resolution_set(unsigned w,unsigned h){(void)w;(void)h;return 0;}
void runtime_watch_set(uintptr_t addr,size_t size){(void)addr;(void)size;runtime_fail("尚未接入可选图形写入监视器");}
void runtime_watch_tick(uint64_t frame){(void)frame;}
void runtime_boot_status(const char *stage,const char *detail){fprintf(stderr,"[pmttyd] %s：%s\n",stage,detail);}
void runtime_boot_count(const char *stage,uint64_t done,uint64_t total){if(done==total)fprintf(stderr,"[pmttyd] %s：%llu/%llu\n",stage,(unsigned long long)done,(unsigned long long)total);}
void runtime_warm_progress(uint64_t done,uint64_t total,uint64_t shaders,uint64_t shader_total,uint64_t pipelines,uint64_t pipeline_total){(void)shaders;(void)shader_total;(void)pipelines;(void)pipeline_total;if(done==total)fprintf(stderr,"[pmttyd] 着色器准备：%llu/%llu\n",(unsigned long long)done,(unsigned long long)total);}
void runtime_warm_done(void){fprintf(stderr,"[pmttyd] 着色器准备完成\n");}

/* 游戏只导入这四个 nn::vi 接口；显示与图层由 Cocoa/Metal 提供，不运行 Tegra 显示驱动。 */
extern void *front_metal_layer(void);
typedef struct {uint32_t magic;} NativeDisplay;
typedef struct {uint32_t magic;NativeDisplay *display;void *metal_layer;} NativeLayer;
static NativeDisplay native_display={0x56494450};
static void vi_initialize(void){if(!front_metal_layer())runtime_fail("Cocoa 显示尚未创建");}
static uint32_t vi_open_default(NativeDisplay **out){if(!out)runtime_fail("显示输出指针");*out=&native_display;return 0;}
static uint32_t vi_create_layer(NativeLayer **out,NativeDisplay *display){if(!out||display!=&native_display)runtime_fail("显示对象不匹配");NativeLayer *layer=calloc(1,sizeof *layer);if(!layer)runtime_fail("图层分配");*layer=(NativeLayer){0x56494c59,display,front_metal_layer()};*out=layer;return 0;}
static uint32_t vi_native_window(void **out,NativeLayer *layer){if(!out||!layer||layer->magic!=0x56494c59)runtime_fail("图层对象不匹配");*out=layer->metal_layer;return 0;}
__attribute__((used,section("__DATA,totkgfx"))) static const GfxEntry vi_entries[]={
 {"_ZN2nn2vi10InitializeEv",vi_initialize,"Cocoa 显示初始化"},
 {"_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE",vi_open_default,"默认 Cocoa 显示"},
 {"_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayE",vi_create_layer,"创建 Metal 图层"},
 {"_ZN2nn2vi15GetNativeWindowEPPvPNS0_5LayerE",vi_native_window,"Metal 原生窗口"}
};
