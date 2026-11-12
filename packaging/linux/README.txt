OpenFP for Linux

Extract the client tar.gz and run ./OpenFirePower/Play.
Preferred Linux platform: openSUSE Leap 16.
Install runtime libraries with:
  sudo zypper install libSDL2-2_0-0 libcurl4 ca-certificates

Use the archive matching your architecture. The binaries use their build host's
system libraries; build from source if your distribution is not compatible.
See https://openfp.retrocs.org/source/.

The dedicated server is distributed separately as openfp-server-linux-ARCH-0.1.tar.gz.
Optional local DEB packages can be built with tools/package_linux.sh --deb.
