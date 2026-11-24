#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../runtime/input_bridge.c"
_Noreturn void runtime_fail(const char *reason){fprintf(stderr,"%s\n",reason);abort();}
int main(void){
 uint32_t id=0;NpadState state;assert(pad_style(&id)==1);full_state(&state,&id);assert(state.attributes==1&&state.buttons==0);
 pm_input_key(6,1);pm_input_key(13,1);full_state(&state,&id);assert(state.buttons&1);assert(state.ly==32767);
 pm_input_key(6,0);pm_input_key(13,0);full_state(&state,&id);assert(state.buttons==0&&state.ly==0);
 pm_input_key(0,1);pm_input_key(2,1);full_state(&state,&id);assert(state.lx==0);pm_input_clear();full_state(&state,&id);assert(state.buttons==0&&state.lx==0);
 pm_input_controller(1,12345,-12345,23456,-23456);full_state(&state,&id);assert((state.buttons&1)&&state.lx==12345&&state.ly==-12345&&state.rx==23456);pm_input_controller(0,0,0,0,0);
 id=1;assert(pad_style(&id)==0);full_state(&state,&id);assert(state.attributes==0);puts("键盘手柄 ABI、按下释放、轴值与断开状态测试通过");
}
