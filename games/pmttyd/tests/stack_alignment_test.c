#include <stdint.h>
#include <assert.h>
#include <stdio.h>
int main(void){
 uintptr_t original=0x100000;for(uint64_t bytes=0;bytes<1024;bytes++){
  uint64_t capacity=(bytes+15)&~15ULL;uintptr_t call_sp=original-capacity;
  assert((call_sp&15)==0&&capacity>=bytes&&capacity-bytes<16&&call_sp+capacity==original);
 }puts("脚本调用参数区容量、调用栈对齐及 SP 恢复测试通过");}
