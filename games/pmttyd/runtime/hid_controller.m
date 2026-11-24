/* GameController 未接管的实体设备从 IOKit HID 读取，不抢占设备。 */
#import <Foundation/Foundation.h>
#import <IOKit/hid/IOHIDManager.h>
#import <IOKit/hid/IOHIDKeys.h>
#import <IOKit/IOKitLib.h>
#import <GameController/GameController.h>
#include <mach/mach_time.h>
#include <math.h>
#include "input_bridge.h"
#include "gamepad_layout.h"
typedef struct {IOHIDDeviceRef device;uint8_t buttons[16];float x,y,z,rz,rx,ry;int hat,order,right_rxry;uint64_t active;} Pad;
static Pad pads[8];
static IOHIDManagerRef manager;
static float axis(IOHIDElementRef e,CFIndex value){CFIndex min=IOHIDElementGetLogicalMin(e),max=IOHIDElementGetLogicalMax(e);if(max<=min)return 0;float v=(float)(value-min)/(float)(max-min)*2-1;return fmaxf(-1,fminf(1,v));}
static void value_changed(void *ctx,IOReturn result,void *sender,IOHIDValueRef value){
 (void)ctx;(void)sender;if(result!=kIOReturnSuccess)return;IOHIDElementRef e=IOHIDValueGetElement(value);IOHIDDeviceRef device=IOHIDElementGetDevice(e);Pad *p=NULL;for(unsigned i=0;i<8;i++)if(pads[i].device==device)p=&pads[i];if(!p)return;
 unsigned page=IOHIDElementGetUsagePage(e),usage=IOHIDElementGetUsage(e);CFIndex v=IOHIDValueGetIntegerValue(value);int changed=0;
 if(page==9&&usage>0&&usage<16){changed=p->buttons[usage]!=(v!=0);p->buttons[usage]=v!=0;}
 else if(page==1){float *out=NULL;switch(usage){case 0x30:out=&p->x;break;case 0x31:out=&p->y;break;case 0x32:out=&p->z;break;case 0x35:out=&p->rz;break;case 0x33:out=&p->rx;break;case 0x34:out=&p->ry;break;case 0x39:{CFIndex lo=IOHIDElementGetLogicalMin(e),hi=IOHIDElementGetLogicalMax(e),n=hi-lo+1;int hat=v<lo||v>hi?8:(int)(v-lo)*(n==4?2:1);changed=p->hat!=hat;p->hat=hat;break;}}
  if(out){float next=axis(e,v);changed=fabsf(next-*out)>0.04f;*out=next;}
 }
 if(changed)p->active=mach_absolute_time();
}
static void matched(void *ctx,IOReturn result,void *sender,IOHIDDeviceRef device){
 (void)ctx;(void)sender;if(result!=kIOReturnSuccess)return;unsigned i;for(i=0;i<8;i++)if(!pads[i].device)break;if(i==8)return;
 IOReturn opened=IOHIDDeviceOpen(device,kIOHIDOptionsTypeNone);if(opened!=kIOReturnSuccess){fprintf(stderr,"[pmttyd] HID 打开失败：%#x\n",opened);return;}
 NSString *product=(__bridge NSString *)IOHIDDeviceGetProperty(device,CFSTR(kIOHIDProductKey));pads[i]=(Pad){.device=(IOHIDDeviceRef)CFRetain(device),.hat=8,.order=gamepad_hid_order(product.UTF8String)};
 NSArray *elements=CFBridgingRelease(IOHIDDeviceCopyMatchingElements(device,NULL,kIOHIDOptionsTypeNone));int has_z=0,has_rx=0;for(id entry in elements){IOHIDElementRef e=(__bridge IOHIDElementRef)entry;if(IOHIDElementGetUsagePage(e)==1){unsigned u=IOHIDElementGetUsage(e);if(u==0x32)has_z=1;if(u==0x33)has_rx=1;}}pads[i].right_rxry=has_rx&&!has_z;
 IOHIDDeviceRegisterInputValueCallback(device,value_changed,NULL);IOHIDDeviceScheduleWithRunLoop(device,CFRunLoopGetMain(),kCFRunLoopDefaultMode);fprintf(stderr,"[pmttyd] HID 手柄已接入：%s\n",product.UTF8String?:"未知设备");
}
static void removed(void *ctx,IOReturn result,void *sender,IOHIDDeviceRef device){(void)ctx;(void)result;(void)sender;for(unsigned i=0;i<8;i++)if(pads[i].device==device){IOHIDDeviceUnscheduleFromRunLoop(device,CFRunLoopGetMain(),kCFRunLoopDefaultMode);IOHIDDeviceClose(device,kIOHIDOptionsTypeNone);CFRelease(pads[i].device);pads[i]=(Pad){0};}}
void pm_hid_start(void){manager=IOHIDManagerCreate(kCFAllocatorDefault,kIOHIDOptionsTypeNone);NSDictionary *match=@{@kIOHIDDeviceUsagePageKey:@1,@kIOHIDDeviceUsageKey:@5};NSDictionary *joystick=@{@kIOHIDDeviceUsagePageKey:@1,@kIOHIDDeviceUsageKey:@4};NSDictionary *xbox=@{@kIOHIDVendorIDKey:@0x045e,@kIOHIDProductIDKey:@0x028e};NSDictionary *pro=@{@kIOHIDVendorIDKey:@0x057e,@kIOHIDProductIDKey:@0x2009};IOHIDManagerSetDeviceMatchingMultiple(manager,(__bridge CFArrayRef)@[match,joystick,xbox,pro]);IOHIDManagerRegisterDeviceMatchingCallback(manager,matched,NULL);IOHIDManagerRegisterDeviceRemovalCallback(manager,removed,NULL);IOHIDManagerScheduleWithRunLoop(manager,CFRunLoopGetMain(),kCFRunLoopDefaultMode);IOReturn status=IOHIDManagerOpen(manager,kIOHIDOptionsTypeNone);CFSetRef devices=IOHIDManagerCopyDevices(manager);fprintf(stderr,"[pmttyd] HID manager 打开结果=%#x，匹配设备=%ld\n",status,devices?(long)CFSetGetCount(devices):0L);if(devices)CFRelease(devices);
 io_iterator_t iterator=0;if(IORegistryCreateIterator(kIOMainPortDefault,kIOServicePlane,kIORegistryIterateRecursively,&iterator)==KERN_SUCCESS){io_service_t service;
  while((service=IOIteratorNext(iterator))){CFMutableDictionaryRef properties=NULL;IORegistryEntryCreateCFProperties(service,&properties,kCFAllocatorDefault,0);NSDictionary *info=CFBridgingRelease(properties);int page=[info[@"PrimaryUsagePage"] intValue],usage=[info[@"PrimaryUsage"] intValue];
   if(page==1&&(usage==4||usage==5)){IOHIDDeviceRef device=IOHIDDeviceCreate(kCFAllocatorDefault,service);fprintf(stderr,"[pmttyd] HID registry 创建：%s，设备=%s\n",[info[@"Product"] UTF8String],device?"可用":"不可用");if(device){matched(NULL,kIOReturnSuccess,NULL,device);CFRelease(device);}}
   IOObjectRelease(service);
  }IOObjectRelease(iterator);
 }
}
void pm_hid_poll(void){Pad *p=NULL;for(unsigned i=0;i<8;i++)if(pads[i].device&&(!p||pads[i].active>p->active))p=&pads[i];if(!p){pm_input_controller(0,0,0,0,0);return;}uint64_t buttons=gamepad_hid_buttons(p->buttons,p->order);if(p->hat>=0&&p->hat<8){static const uint64_t hats[]={1ULL<<13,(1ULL<<13)|(1ULL<<14),1ULL<<14,(1ULL<<14)|(1ULL<<15),1ULL<<15,(1ULL<<15)|(1ULL<<12),1ULL<<12,(1ULL<<12)|(1ULL<<13)};buttons|=hats[p->hat];}pm_input_controller(buttons,(int32_t)(p->x*32767),(int32_t)(-p->y*32767),(int32_t)((p->right_rxry?p->rx:p->z)*32767),(int32_t)(-(p->right_rxry?p->ry:p->rz)*32767));}
