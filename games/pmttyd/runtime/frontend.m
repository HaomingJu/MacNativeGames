/* Cocoa 主线程运行窗口；游戏代码在独立原生线程执行。 */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <GameController/GameController.h>
#include <pthread.h>
#include "input_bridge.h"
#include <stdatomic.h>
static NSWindow *window;
static CAMetalLayer *layer;
static _Atomic uint64_t presented;
static _Atomic int display_hz=60;
int runtime_front_display_hz(void){return atomic_load(&display_hz);}
extern void runtime_audio_hold(int on);
extern void pm_hid_start(void);
extern void pm_hid_poll(void);
static void (*game_start)(void);
void *front_metal_layer(void){return (__bridge void *)layer;}
uint64_t front_game_res(int docked){(void)docked;return (720ULL<<32)|1280;}
void front_frame_presented(void){uint64_t n=atomic_fetch_add(&presented,1)+1;if(n==1){runtime_audio_hold(0);fprintf(stderr,"[pmttyd] 首个 Metal 帧已呈现，解除启动音频保持\n");}}
static void *game_thread(void *arg){(void)arg;@autoreleasepool{game_start();}return NULL;}
@interface GameView : NSView
@end
@implementation GameView
- (BOOL)acceptsFirstResponder{return YES;}
- (void)keyDown:(NSEvent *)event{pm_input_key(event.keyCode,1);}
- (void)keyUp:(NSEvent *)event{pm_input_key(event.keyCode,0);}
@end
@interface GameDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end
@implementation GameDelegate
- (void)applicationDidBecomeActive:(NSNotification *)notification{(void)notification;[window deminiaturize:nil];[window makeKeyAndOrderFront:nil];}
- (void)windowDidResignKey:(NSNotification *)notification{(void)notification;pm_input_clear();}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender{(void)sender;return YES;}
@end
static void poll_controller(void){
 GCExtendedGamepad *pad=nil;for(GCController *controller in GCController.controllers)if(controller.extendedGamepad){pad=controller.extendedGamepad;break;}
 if(!pad){pm_hid_poll();return;}
 uint64_t buttons=0;
 if(pad.buttonA.isPressed)buttons|=1ULL<<0;if(pad.buttonB.isPressed)buttons|=1ULL<<1;if(pad.buttonX.isPressed)buttons|=1ULL<<2;if(pad.buttonY.isPressed)buttons|=1ULL<<3;
 if(pad.leftShoulder.isPressed)buttons|=1ULL<<6;if(pad.rightShoulder.isPressed)buttons|=1ULL<<7;if(pad.leftTrigger.isPressed)buttons|=1ULL<<8;if(pad.rightTrigger.isPressed)buttons|=1ULL<<9;
 if(pad.buttonMenu.isPressed)buttons|=1ULL<<10;if(pad.buttonOptions.isPressed)buttons|=1ULL<<11;
 if(pad.dpad.left.isPressed)buttons|=1ULL<<12;if(pad.dpad.up.isPressed)buttons|=1ULL<<13;if(pad.dpad.right.isPressed)buttons|=1ULL<<14;if(pad.dpad.down.isPressed)buttons|=1ULL<<15;
 pm_input_controller(buttons,(int32_t)(pad.leftThumbstick.xAxis.value*32767),(int32_t)(pad.leftThumbstick.yAxis.value*32767),(int32_t)(pad.rightThumbstick.xAxis.value*32767),(int32_t)(pad.rightThumbstick.yAxis.value*32767));
}
void runtime_front_run(void (*callback)(void)){
 @autoreleasepool{
  game_start=callback;display_hz=(int)NSScreen.mainScreen.maximumFramesPerSecond;NSApplication *application=[NSApplication sharedApplication];[application setActivationPolicy:NSApplicationActivationPolicyRegular];
  GameDelegate *delegate=[GameDelegate new];application.delegate=delegate;
  window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1280,720) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
  window.title=@"Paper Mario: The Thousand-Year Door";window.delegate=delegate;window.contentView=[[GameView alloc] initWithFrame:NSMakeRect(0,0,1280,720)];window.contentView.wantsLayer=YES;
  layer=[CAMetalLayer layer];layer.device=MTLCreateSystemDefaultDevice();layer.pixelFormat=MTLPixelFormatBGRA8Unorm;layer.framebufferOnly=NO;layer.frame=window.contentView.bounds;layer.contentsScale=window.backingScaleFactor;layer.drawableSize=CGSizeMake(1280,720);window.contentView.layer=layer;
  [window center];[window makeKeyAndOrderFront:nil];[window makeFirstResponder:window.contentView];[application activateIgnoringOtherApps:YES];
  pm_hid_start();
  [NSTimer scheduledTimerWithTimeInterval:1.0/120.0 repeats:YES block:^(NSTimer *timer){(void)timer;poll_controller();}];
  fprintf(stderr,"[pmttyd] GameController 当前连接数：%lu\n",(unsigned long)GCController.controllers.count);
  pthread_t thread;if(pthread_create(&thread,NULL,game_thread,NULL)){fprintf(stderr,"无法启动原生游戏线程\n");exit(1);}
  [application run];
 }
}

/* 打包后的可执行文件无 CLI 参数时使用 App 自带的已校验数据。 */
void runtime_app_arguments(int *argc,char ***argv){
 if(*argc!=1)return;
 @autoreleasepool{
  NSBundle *bundle=NSBundle.mainBundle;NSString *resources=bundle.resourcePath;
  if(!resources){fprintf(stderr,"App 资源目录不可用\n");exit(1);}
  char **args=calloc(6,sizeof *args);args[0]=(*argv)[0];args[1]=strdup([resources stringByAppendingPathComponent:@"runtime-tables.bin"].fileSystemRepresentation);args[2]=strdup([resources stringByAppendingPathComponent:@"rewritten"].fileSystemRepresentation);args[3]=strdup("boot");args[4]=strdup([resources stringByAppendingPathComponent:@"romfs.bin"].fileSystemRepresentation);
  NSString *work=[bundle objectForInfoDictionaryKey:@"MNGWorkFolder"];
  if(work.length){NSString *cache=[work stringByAppendingPathComponent:@"build/shader-cache/app-cache"];setenv("TOTK_SHADER_CACHE",cache.fileSystemRepresentation,1);}
  *argc=5;*argv=args;
 }
}
