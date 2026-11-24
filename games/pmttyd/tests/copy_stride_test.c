#include <assert.h>
#include <stdio.h>
#include "../third_party/d2r_gfx/gfx/nvn/nvn_copy_layout.h"
int main(void){size_t image,bytes;
 assert(nvn_copy_layout(32,4,2,0,&image,&bytes)&&image==128&&bytes==256);
 assert(nvn_copy_layout(32,4,2,512,&image,&bytes)&&image==512&&bytes==640);
 assert(!nvn_copy_layout(32,4,2,96,&image,&bytes));assert(!nvn_copy_layout(32,4,2,-1,&image,&bytes));
 assert(!nvn_copy_layout(SIZE_MAX,4,2,0,&image,&bytes));puts("图像步幅、紧排、层间填充和范围溢出测试通过");}
