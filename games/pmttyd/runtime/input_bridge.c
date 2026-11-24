/* 键盘作为本地虚拟 FullKey 手柄；游戏读取原有的 40 字节 Npad ABI。 */
#include "runtime.h"
#include "input_bridge.h"
#include <stdint.h>
#include <string.h>
typedef struct {const char *name;void *fn;const char *why;} GfxEntry;
typedef struct {int64_t sampling;uint64_t buttons;int32_t lx,ly,rx,ry;uint32_t attributes,pad;} NpadState;
_Static_assert(sizeof(NpadState)==40,"Npad ABI");
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static uint8_t pressed[128];
static int64_t sample;
static uint64_t controller_buttons;
static int32_t controller_lx,controller_ly,controller_rx,controller_ry;
void pm_input_controller(uint64_t buttons,int32_t lx,int32_t ly,int32_t rx,int32_t ry){pthread_mutex_lock(&lock);controller_buttons=buttons;controller_lx=lx;controller_ly=ly;controller_rx=rx;controller_ry=ry;pthread_mutex_unlock(&lock);}
void pm_input_key(unsigned key,int down){if(key>=128)return;pthread_mutex_lock(&lock);pressed[key]=!!down;pthread_mutex_unlock(&lock);}
void pm_input_clear(void){pthread_mutex_lock(&lock);memset(pressed,0,sizeof pressed);pthread_mutex_unlock(&lock);}
static uint32_t pad_style(const uint32_t *id){return id&&*id==0?1:0;}
static void full_state(NpadState *out,const uint32_t *id){
 if(!out||!id)runtime_fail("手柄输出参数");pthread_mutex_lock(&lock);*out=(NpadState){.sampling=++sample};
 if(*id==0){
  out->attributes=1;out->buttons=controller_buttons;out->rx=controller_rx;out->ry=controller_ry;
  const struct {unsigned key,button;} map[]={{6,0},{49,0},{7,1},{8,2},{9,3},{12,6},{14,7},{18,8},{20,9},{36,10},{51,11},{123,12},{126,13},{124,14},{125,15}};
  for(unsigned i=0;i<sizeof map/sizeof *map;i++)if(pressed[map[i].key])out->buttons|=1ULL<<map[i].button;
  out->lx=((int)pressed[2]-(int)pressed[0])*32767;out->ly=((int)pressed[13]-(int)pressed[1])*32767;
  if(!out->lx)out->lx=controller_lx;if(!out->ly)out->ly=controller_ly;
 }
 pthread_mutex_unlock(&lock);
}
static void disconnected_state(NpadState *out,const uint32_t *id){(void)id;if(!out)runtime_fail("手柄输出参数");pthread_mutex_lock(&lock);*out=(NpadState){.sampling=++sample};pthread_mutex_unlock(&lock);}
__attribute__((used,section("__DATA,totkgfx"))) static const GfxEntry entries[]={
 {"_ZN2nn3hid15GetNpadStyleSetERKj",pad_style,"本地键盘手柄样式"},
 {"_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj",full_state,"本地键盘手柄状态"},
 {"_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj",disconnected_state,"未连接 Joy-Con"},
 {"_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj",disconnected_state,"桌面不处于掌机手柄模式"}
};
