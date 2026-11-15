// what the launcher does on macOS with AppKit

#include "macos.h"

#import <AppKit/AppKit.h>

void activate_application(const char *bundle_id)
{
    NSString *name = [NSString stringWithUTF8String:bundle_id];
    NSRunningApplication *app =
        [NSRunningApplication runningApplicationsWithBundleIdentifier:name]
            .firstObject;
    if (app == nil)
        return;
    // without NSApplicationActivateAllWindows: the main and key window
    // only; since macOS 14, the active program lets the other activate
    if (@available(macOS 14, *)) {
        [NSApp yieldActivationToApplication:app];
        [app activateWithOptions:0];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [app activateWithOptions:NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
    }
}
