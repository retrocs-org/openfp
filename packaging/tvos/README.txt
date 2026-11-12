OpenFP — Apple TV preview

Requires tvOS 15 or later and an extended game controller (Xbox, PlayStation, etc.).
This port shares the desktop game's worlds, networking, editor and replays.
The on-screen keyboard lets you create profiles and enter server addresses.

SIMULATOR
Install OpenFirePower.app using Xcode's simulator tools. This ARM64 simulator
build runs on Apple Silicon Macs. It cannot be installed on a real Apple TV.

DEVICE
The device app in builds/tvOS/device is unsigned. Open the supplied Xcode
project from the build directory, choose your Apple development team in
Signing & Capabilities, select an Apple TV and Run. Use Product > Archive
for a signed distribution. An unsigned .app is not a publishable installer.

Profiles, settings and saved maps currently use tvOS's local cache. The system
may remove this storage. Upload maps to the gallery to keep a copy. Cloud save
and App Store artwork are follow-up work before a public tvOS release.
