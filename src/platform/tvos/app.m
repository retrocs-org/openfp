/* SPDX-License-Identifier: MIT
 * SDL2's supported app-delegate subclass hook, with a tvOS scene lifecycle. */
#import <UIKit/UIKit.h>
#include <SDL.h>
#include <SDL_syswm.h>

/* Public subclassing hook implemented by SDL2's UIKit backend. No private ivars are accessed. */
@interface SDLUIKitDelegate : NSObject <UIApplicationDelegate>
+ (NSString *)getAppDelegateClassName;
- (void)postFinishLaunch;
@end
static UIWindowScene *game_scene;
@interface OFPSceneDelegate : NSObject <UIWindowSceneDelegate>
@end
@interface OFPAppDelegate : SDLUIKitDelegate
@end
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-protocol-method-implementation"
@implementation SDLUIKitDelegate (OFPDelegateClass)
+ (NSString *)getAppDelegateClassName { return @"OFPAppDelegate"; }
@end
#pragma clang diagnostic pop
@implementation OFPAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options
{
    BOOL result = [super application:application didFinishLaunchingWithOptions:options];
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(postFinishLaunch) object:nil];
    return result;
}
@end
@implementation OFPSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options
{
    (void)session; (void)options;
    if (![scene isKindOfClass:[UIWindowScene class]]) return;
    game_scene = (UIWindowScene *)scene;
    [(OFPAppDelegate *)UIApplication.sharedApplication.delegate performSelector:@selector(postFinishLaunch) withObject:nil afterDelay:0];
}
- (void)sceneDidDisconnect:(UIScene *)scene { if (scene == game_scene) game_scene = nil; }
@end

void ofp_tvos_attach_window(SDL_Window *window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (game_scene && SDL_GetWindowWMInfo(window, &info)) {
        UIWindow *native = info.info.uikit.window;
        native.windowScene = game_scene;
        native.frame = game_scene.coordinateSpace.bounds;

    }
}
