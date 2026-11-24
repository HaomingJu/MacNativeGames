#define main guest_loader_main
#include "../runtime/native_probe.c"
#undef main
#include <assert.h>
int main(void){
 static Record records[4];for(unsigned i=0;i<4;i++)modules[i].record=records+i;
 setenv("TZ","UTC",1);tzset();IpcSession session={.name="time-zone"};
 int64_t timestamp=0;IpcRequest request={.cmd=101,.data=(uint8_t *)&timestamp,.data_size=8};IpcResponse response={0};
 addon_manager(&session,&request,&response);assert(response.result==0&&response.data_size==32);
 int16_t year;memcpy(&year,response.data,2);assert(year==1970&&response.data[2]==1&&response.data[3]==1);
 int32_t weekday,year_day,offset;memcpy(&weekday,response.data+8,4);memcpy(&year_day,response.data+12,4);memcpy(&offset,response.data+28,4);assert(weekday==4&&year_day==0&&offset==0&&!response.data[24]);
 timestamp=1709164800;response=(IpcResponse){0};addon_manager(&session,&request,&response);memcpy(&year,response.data,2);assert(year==2024&&response.data[2]==2&&response.data[3]==29);
 setenv("TZ","EST5EDT,M3.2.0,M11.1.0",1);tzset();timestamp=1719792000;response=(IpcResponse){0};addon_manager(&session,&request,&response);memcpy(&offset,response.data+28,4);assert(response.data[2]==6&&response.data[3]==30&&response.data[4]==20&&response.data[24]==1&&offset==-14400);
 puts("日历 IPC 布局、Unix 纪元、闰日、跨日时区与夏令时测试通过");return 0;
}
