# Test-only toolchain: builds the core tests for 32-bit ARM Linux (armv7-a, Cortex-A9 with NEON,
# like the iPad's A5) and runs them under qemu-arm, to catch 32-bit pointer and size_t bugs that an
# x86_64 build hides. This is not the iOS toolchain (that one is A1's, in ios/toolchain/).
# Used by run.sh when arm-linux-gnueabihf-gcc and qemu-arm are installed.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7-a)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_LIBRARY_ARCHITECTURE arm-linux-gnueabihf)
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard")
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-arm -L /usr/arm-linux-gnueabihf)
set(PKG_CONFIG_EXECUTABLE arm-linux-gnueabihf-pkg-config CACHE FILEPATH "")
# Find the armhf copies of OpenSSL, mbedTLS and Opus (apt install libssl-dev:armhf ...), never the
# host's x86_64 ones.
set(CMAKE_LIBRARY_PATH /usr/lib/arm-linux-gnueabihf)
set(CMAKE_IGNORE_PATH /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu)
