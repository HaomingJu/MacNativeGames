/* 本地存档文件系统：目录与编译工作区分离，路径不能越过存档根目录。 */
#include "runtime.h"
#include "save_host.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

typedef struct SaveFile SaveFile;
typedef struct {int fd;_Atomic unsigned refs;pthread_mutex_t lock;SaveFile *files;} SaveRoot;
struct SaveFile {int fd;SaveRoot *root;SaveFile *next;};
static void need(int ok,const char *reason){if(!ok)runtime_fail(reason);}
static uint32_t fs_error(void){return errno==ENOENT?0x202:errno==EEXIST?0x402:errno==EACCES?0x1a02:0x602;}
static void root_release(SaveRoot *root){if(atomic_fetch_sub(&root->refs,1)==1){close(root->fd);pthread_mutex_destroy(&root->lock);free(root);}}
static void root_close(void *state){root_release(state);}
static void file_close(void *state){SaveFile *file=state;SaveRoot *root=file->root;pthread_mutex_lock(&root->lock);SaveFile **p=&root->files;while(*p!=file)p=&(*p)->next;*p=file->next;fsync(file->fd);close(file->fd);pthread_mutex_unlock(&root->lock);free(file);root_release(root);}
static const char *input_path(const IpcRequest *rq,unsigned index){
 need(index<rq->nx&&rq->x[index].size>0&&rq->x[index].size<=0x301,"存档路径缓冲区");const char *path=ipc_guest(rq->x[index].addr,rq->x[index].size);need(memchr(path,0,rq->x[index].size)!=NULL,"存档路径终止符");return path;
}
/* 逐级 openat + O_NOFOLLOW，拒绝 '..' 以及目录符号链接。返回最后一层父目录。 */
static int parent(SaveRoot *root,const char *path,char name[256]){
 while(*path=='/')path++;need(strlen(path)<0x301,"存档路径长度");char copy[0x301];strcpy(copy,path);int fd=dup(root->fd);need(fd>=0,"存档目录句柄");
 char *state=NULL,*part=strtok_r(copy,"/",&state);if(!part){strcpy(name,".");return fd;}
 while(part){need(strcmp(part,"..")&&strcmp(part,".")&&strlen(part)<256,"存档路径越界");char *next=strtok_r(NULL,"/",&state);if(!next){strcpy(name,part);return fd;}int child=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(child<0){close(fd);return -1;}close(fd);fd=child;part=next;}close(fd);return -1;
}
static void file_handler(IpcSession *session,const IpcRequest *rq,IpcResponse *rs){
 SaveFile *file=session->state;
 if(rq->cmd==4){struct stat st;if(fstat(file->fd,&st)){rs->result=fs_error();return;}int64_t size=st.st_size;IPC_OUT(rs,size);return;}
 if(rq->cmd==2){if(fsync(file->fd))rs->result=fs_error();return;}
 if(rq->cmd==3){int64_t size=IPC_ARG(rq,int64_t,0);if(size<0||ftruncate(file->fd,size))rs->result=fs_error();return;}
 if(rq->cmd==0||rq->cmd==1){
  int64_t offset=IPC_ARG(rq,int64_t,0),size=IPC_ARG(rq,int64_t,8);if(offset<0||size<0||size>(64LL<<20)){rs->result=0x602;return;}
  int reading=rq->cmd==0,pointer=reading&&!(rq->nb&&rq->b[0].size);
  IpcBuffer buffer=reading?(pointer&&rq->nc?rq->c[0]:rq->nb?rq->b[0]:(IpcBuffer){0}):(rq->na&&rq->a[0].size?rq->a[0]:rq->nx?rq->x[0]:(IpcBuffer){0});
  need((uint64_t)size<=buffer.size,"存档读写缓冲区大小");uint8_t *data=ipc_guest(buffer.addr,buffer.size);int64_t done=0;
  while(done<size){ssize_t n=reading?pread(file->fd,data+done,(size_t)(size-done),offset+done):pwrite(file->fd,data+done,(size_t)(size-done),offset+done);if(n<0){if(errno==EINTR)continue;rs->result=fs_error();return;}if(!n)break;done+=n;}
  if(reading){IPC_OUT(rs,done);if(pointer)rs->xout[rs->nxout++]=(IpcBuffer){buffer.addr,(uint64_t)done,buffer.index};}return;
 }
 ipc_unimplemented(session,rq);
}
static void filesystem_handler(IpcSession *session,const IpcRequest *rq,IpcResponse *rs){
 SaveRoot *root=session->state;
 if(rq->cmd==10){pthread_mutex_lock(&root->lock);for(SaveFile *f=root->files;f;f=f->next)if(fsync(f->fd))rs->result=fs_error();if(fsync(root->fd))rs->result=fs_error();pthread_mutex_unlock(&root->lock);return;}
 if(rq->cmd==0||rq->cmd==1||rq->cmd==2||rq->cmd==3||rq->cmd==7||rq->cmd==8){
  char name[256];int dir=parent(root,input_path(rq,0),name);if(dir<0){rs->result=fs_error();return;}
  if(rq->cmd==0){int64_t size=IPC_ARG(rq,int64_t,0);int fd=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)rs->result=fs_error();else{if(size<0||ftruncate(fd,size))rs->result=fs_error();close(fd);}}
  else if(rq->cmd==1||rq->cmd==3){if(unlinkat(dir,name,rq->cmd==3?AT_REMOVEDIR:0))rs->result=fs_error();}
  else if(rq->cmd==2){if(mkdirat(dir,name,0700))rs->result=fs_error();}
  else if(rq->cmd==7){struct stat st;if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW))rs->result=fs_error();else{uint32_t type=S_ISDIR(st.st_mode)?0:1;IPC_OUT(rs,type);}}
  else{
   uint32_t mode=IPC_ARG(rq,uint32_t,0);int flags=(mode&3)==3?O_RDWR:(mode&2)?O_WRONLY:O_RDONLY;int fd=openat(dir,name,flags|O_NOFOLLOW);
   if(fd<0)rs->result=fs_error();else{SaveFile *file=calloc(1,sizeof *file);need(file!=NULL,"存档文件对象");file->fd=fd;file->root=root;atomic_fetch_add(&root->refs,1);pthread_mutex_lock(&root->lock);file->next=root->files;root->files=file;pthread_mutex_unlock(&root->lock);rs->move[rs->nmove++]=runtime_ipc_new_session("save-file",file_handler,file,file_close);}
  }
  close(dir);return;
 }
 if(rq->cmd==5||rq->cmd==6){char old[256],new[256];int a=parent(root,input_path(rq,0),old),b=parent(root,input_path(rq,1),new);if(a<0||b<0)rs->result=fs_error();else if(renameat(a,old,b,new))rs->result=fs_error();if(a>=0)close(a);if(b>=0)close(b);return;}
 ipc_unimplemented(session,rq);
}
void runtime_save_open(IpcResponse *rs){
 const char *home=getenv("HOME");need(home!=NULL,"用户主目录");char path[4096];need(snprintf(path,sizeof path,"%s/Library/Application Support/MacNativeGames/PaperMarioTTYD/profile",home)<(int)sizeof path,"存档根目录长度");
#ifdef PMTTYD_SELFTEST
 const char *test=getenv("PMTTYD_TEST_SAVE_DIR");if(test){need(strlen(test)<sizeof path,"自测存档目录长度");strcpy(path,test);}
#endif
 for(char *p=path+1;*p;p++)if(*p=='/'){*p=0;if(mkdir(path,0700)&&errno!=EEXIST)runtime_fail("创建存档父目录");*p='/';}
 if(mkdir(path,0700)&&errno!=EEXIST)runtime_fail("创建存档目录");int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);need(fd>=0,"打开存档根目录");
 SaveRoot *root=calloc(1,sizeof *root);need(root!=NULL,"存档文件系统对象");root->fd=fd;root->refs=1;pthread_mutex_init(&root->lock,NULL);rs->move[rs->nmove++]=runtime_ipc_new_session("save-filesystem",filesystem_handler,root,root_close);
}
