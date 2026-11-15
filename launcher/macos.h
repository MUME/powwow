// what the launcher does on macOS with AppKit, in macos.mm

#ifndef MACOS_H
#define MACOS_H

// bring the running program 'bundle_id', such as com.apple.Terminal, to
// the front with only its main and key window, not all its windows
void activate_application(const char *bundle_id);

#endif
