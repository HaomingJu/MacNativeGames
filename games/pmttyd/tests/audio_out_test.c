#include <assert.h>
#include <stdatomic.h>
#include "../../../shared/runtime/audio_out.c"
static IpcHandler registered;
static IpcSession opened;
static _Atomic unsigned signals;
_Noreturn void runtime_fail(const char *why){fprintf(stderr,"%s\n",why);abort();}
_Noreturn void ipc_unimplemented(const IpcSession *s,const IpcRequest *rq){(void)s;(void)rq;abort();}
void runtime_ipc_register(const char *name,IpcHandler handler,IpcOpen open,void (*close)(void *)){assert(!strcmp(name,"audout:u"));(void)open;(void)close;registered=handler;}
uint32_t runtime_ipc_new_session(const char *name,IpcHandler handler,void *state,void (*close)(void *)){opened=(IpcSession){.name=name,.handler=handler,.state=state,.close=close};return 10;}
uint32_t runtime_event_new(void){return 1;}
uint32_t runtime_event_readable(uint32_t writable){assert(writable==1);return 2;}
void runtime_event_signal(uint32_t writable){assert(writable==1);atomic_fetch_add(&signals,1);}
int runtime_core_away(void){return 0;}
void runtime_core_back(int *away){assert(!*away);}
void *ipc_guest(uint64_t addr,uint64_t size){(void)size;assert(addr);return (void *)(uintptr_t)addr;}
const void *ipc_arg(const IpcRequest *rq,uint32_t off,uint32_t n){assert(off+n<=rq->data_size);return rq->data+off;}
void ipc_out(IpcResponse *rs,const void *p,uint32_t n){assert(rs->data_size+n<=sizeof rs->data);memcpy(rs->data+rs->data_size,p,n);rs->data_size+=n;}
int main(void){
 setenv("BY3_AUDIO","0",1);
 uint32_t params[2]={48000,2};IpcRequest rq={.cmd=1,.data=(uint8_t *)params,.data_size=sizeof params};IpcResponse rs={0};
 assert(registered);registered(NULL,&rq,&rs);assert(rs.nmove==1&&rs.move[0]==10&&rs.data_size==16);
 AudioOut *o=opened.state;assert(o->silent&&!o->aq&&o->state==STATE_STOPPED);
 rq=(IpcRequest){.cmd=4};rs=(IpcResponse){0};opened.handler(&opened,&rq,&rs);assert(rs.ncopy==1&&rs.copy[0]==2);
 int16_t samples[960]={0};AudioOutBuffer desc={.sample_buffer=(uintptr_t)samples,.capacity=sizeof samples,.data_size=sizeof samples};uint64_t tag=42;
 rq=(IpcRequest){.cmd=3,.na=1,.data=(uint8_t *)&tag,.data_size=sizeof tag};rq.a[0]=(IpcBuffer){(uintptr_t)&desc,sizeof desc,0};rs=(IpcResponse){0};opened.handler(&opened,&rq,&rs);
 struct timespec pause={0,20000000};nanosleep(&pause,NULL);assert(!atomic_load(&signals));
 rq=(IpcRequest){.cmd=1};opened.handler(&opened,&rq,&rs);
 uint64_t deadline=now_ns()+1000000000ull;while(!atomic_load(&signals)&&now_ns()<deadline){struct timespec tick={0,1000000};nanosleep(&tick,NULL);}assert(atomic_load(&signals));
 uint64_t tags[2]={0};rq=(IpcRequest){.cmd=5,.nb=1};rq.b[0]=(IpcBuffer){(uintptr_t)tags,sizeof tags,0};rs=(IpcResponse){0};opened.handler(&opened,&rq,&rs);uint32_t count;memcpy(&count,rs.data,4);assert(count==1&&tags[0]==42&&tags[1]==0);
 rq=(IpcRequest){.cmd=10};rs=(IpcResponse){0};opened.handler(&opened,&rq,&rs);uint64_t frames;memcpy(&frames,rs.data,8);assert(frames==480);
 opened.close(opened.state);puts("AudioOut 注册、静音定时播放、完成事件、缓冲区回收与采样计数测试通过");
}
