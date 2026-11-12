# Build an x86_64 Linux client on an ARM64 Debian/Ubuntu build machine.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-linux-gnu-gcc)
set(SDL2_DIR /usr/lib/x86_64-linux-gnu/cmake/SDL2 CACHE PATH "SDL2 for the target")
set(CURL_LIBRARY /usr/lib/x86_64-linux-gnu/libcurl.so.4 CACHE FILEPATH "Target curl runtime")
set(CURL_INCLUDE_DIR /opt/ofp-sdk/curl/usr/include/x86_64-linux-gnu CACHE PATH "Architecture-neutral 64-bit curl API headers")
set(SQLite3_LIBRARY /usr/lib/x86_64-linux-gnu/libsqlite3.so CACHE FILEPATH "Target SQLite")
