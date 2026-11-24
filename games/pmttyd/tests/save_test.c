#include <assert.h>
#include "../runtime/save_host.c"
static IpcSession made;
_Noreturn void runtime_fail(const char *why){fprintf(stderr,"%s\n",why);abort();}
_Noreturn void ipc_unimplemented(const IpcSession *s,const IpcRequest *r){(void)s;(void)r;abort();}
uint32_t runtime_ipc_new_session(const char *name,IpcHandler handler,void *state,void (*close)(void *)){made=(IpcSession){.name=name,.handler=handler,.state=state,.close=close};return 1;}
const void *ipc_arg(const IpcRequest *rq,uint32_t offset,uint32_t size){assert(offset+size<=rq->data_size);return rq->data+offset;}
void *ipc_guest(uint64_t address,uint64_t size){(void)size;return (void *)(uintptr_t)address;}
void ipc_out(IpcResponse *rs,const void *data,uint32_t size){assert(rs->data_size+size<=sizeof rs->data);memcpy(rs->data+rs->data_size,data,size);rs->data_size+=size;}
int main(int argc,char **argv){assert(argc==2);SaveRoot *root=calloc(1,sizeof *root);root->fd=open(argv[1],O_RDONLY|O_DIRECTORY);assert(root->fd>=0);root->refs=1;pthread_mutex_init(&root->lock,NULL);IpcSession session={.state=root};IpcResponse response={0};int64_t size=16;const char path[]="/test.sav";
 IpcRequest request={.cmd=0,.nx=1,.data=(uint8_t *)&size,.data_size=8};request.x[0]=(IpcBuffer){(uintptr_t)path,sizeof path,0};filesystem_handler(&session,&request,&response);assert(response.result==0);
 uint32_t mode=3;request.cmd=8;request.data=(uint8_t *)&mode;request.data_size=4;response=(IpcResponse){0};filesystem_handler(&session,&request,&response);assert(response.result==0);IpcSession file=made;
 int64_t args[2]={0,4};uint32_t value=0x12345678;request=(IpcRequest){.cmd=1,.na=1,.data=(uint8_t *)args,.data_size=16};request.a[0]=(IpcBuffer){(uintptr_t)&value,4,0};response=(IpcResponse){0};file_handler(&file,&request,&response);assert(response.result==0);
 uint32_t readback=0;request.cmd=0;request.na=0;request.nb=1;request.b[0]=(IpcBuffer){(uintptr_t)&readback,4,0};response=(IpcResponse){0};file_handler(&file,&request,&response);assert(response.result==0&&readback==value);request=(IpcRequest){.cmd=10};filesystem_handler(&session,&request,&response);assert(response.result==0);
 file.close(file.state);root_close(root);puts("存档创建、真实读写、同步提交与生命周期测试通过");return 0;
}
