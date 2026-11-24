#pragma once
#include <stdint.h>
#include <stddef.h>
/* 层间步幅只改变层起点；最后一层只需其实际行数据，不能要求额外尾部填充。 */
static inline int nvn_copy_layout(size_t row,size_t rows,size_t depth,int64_t requested,size_t *image,size_t *bytes){
 if(!depth||requested<0||(rows&&row>SIZE_MAX/rows))return 0;
 size_t tight=row*rows,step=requested?(size_t)requested:tight;
 if(step<tight||(step&&depth-1>(SIZE_MAX-tight)/step))return 0;
 *image=step;*bytes=(depth-1)*step+tight;return 1;
}
