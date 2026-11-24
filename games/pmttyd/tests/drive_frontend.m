/* 自测输入只调用本进程的 Npad 键盘适配接口，不向系统发送按键。 */
#define runtime_front_run runtime_front_run_base
#include "../runtime/frontend.m"
#undef runtime_front_run
#include <sys/stat.h>
void runtime_front_run(void (*callback)(void)){
 const char *path=getenv("PMTTYD_TEST_INPUT");
 if(path){NSString *file=@(path);__block NSDate *released=nil;__block int held=-1;
  [NSTimer scheduledTimerWithTimeInterval:0.02 repeats:YES block:^(NSTimer *timer){
   (void)timer;if(held>=0&&[released timeIntervalSinceNow]<=0){pm_input_key((unsigned)held,0);held=-1;}
   NSString *command=[NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:nil];
   if(!command)return;[[NSFileManager defaultManager] removeItemAtPath:file error:nil];
   unsigned key;double duration=0.15;if(sscanf(command.UTF8String,"tap %u %lf",&key,&duration)<1||key>=128||duration<=0||duration>2)return;
   if(held>=0)pm_input_key((unsigned)held,0);held=(int)key;pm_input_key(key,1);released=[NSDate dateWithTimeIntervalSinceNow:duration];fprintf(stderr,"[pmttyd-selftest] tap key=%u duration=%.2f\n",key,duration);
  }];
 }
 runtime_front_run_base(callback);
}
