#import <Foundation/Foundation.h>
#include <assert.h>
#include "../third_party/d2r_gfx/gfx/nvn/nvn_refl.h"
int main(int argc,const char **argv){@autoreleasepool{assert(argc==2);NSData *data=[NSData dataWithContentsOfFile:@(argv[1])];assert(data);NSDictionary *json=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];nvn_refl reflection;char error[512];if(nvn_refl_from_json(json,&reflection,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
 unsigned seen=0;for(int i=0;i<reflection.nb;i++){nvn_rbind binding=reflection.b[i];if(binding.aid==2){assert(binding.kind==NVN_RB_TEXI&&binding.reg==0);seen++;}if(binding.aid==3){assert(binding.kind==NVN_RB_SAMPI&&binding.reg==0);seen++;}if(binding.aid==6){assert(binding.kind==NVN_RB_TEXI&&binding.reg==1);seen++;}if(binding.aid==8){assert(binding.kind==NVN_RB_TEXI&&binding.reg==2);seen++;}if(binding.aid==4){assert(binding.kind==NVN_RB_TEX&&binding.reg==0);seen++;}}assert(seen==5);puts("计算图像表和普通纹理槽的独立反射映射测试通过");}}
