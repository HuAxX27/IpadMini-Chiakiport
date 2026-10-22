# Builds the real core (chiaki-lib, from the repo root) the way the iPad uses it, LAN only, and runs
# Chiaki's unit tests against it on Linux. Usage: ios/tests/core/run.sh
# The tests come from chiaki-ng's test/ folder (see README.md); munit/ is vendored.
cmake_minimum_required(VERSION 3.24)
project(ck_core_tests C)
enable_testing()

set(CK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

# What the iPad build turns off. The crypto backend (OpenSSL or mbedTLS) is chosen by run.sh.
set(CHIAKI_ENABLE_HOLEPUNCH OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_GUI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_CLI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_FFMPEG_DECODER OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_PI_DECODER OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_SETSU OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_SPEEX OFF CACHE STRING "" FORCE)
add_subdirectory("${CK_REPO_ROOT}" chiaki)

# The root adds this define only for its own directories, and ChiakiECDH (inside ChiakiSession)
# changes layout with it, so code that includes the core headers must see it too.
if(CHIAKI_LIB_ENABLE_MBEDTLS)
	add_definitions(-DCHIAKI_LIB_ENABLE_MBEDTLS)
endif()

add_library(munit STATIC munit/munit.c)
target_include_directories(munit PUBLIC munit)

add_executable(chiaki-unit
	main.c
	http.c
	rpcrypt.c
	gkcrypt.c
	takion.c
	seqnum.c
	keystate.c
	reorderqueue.c
	fec.c
	test_log.c
	bitstream.c
	regist.c
	ecdh.c)

target_link_libraries(chiaki-unit chiaki-lib munit)
add_test(NAME chiaki_core_unit COMMAND chiaki-unit)

# Whole archive: every object of the core must resolve, not only the ones a test calls. This is
# what proves the LAN-only core links without curl, json-c and miniupnpc. It stays apart from
# chiaki-unit because takion.c compiles its own copy of takionsendbuffer.c.
add_executable(core-link-check link_check.c)
# chiaki-lib does not link libm although orientation.c calls sqrt(). On iOS libm is part of
# libSystem, so only Linux needs it here.
target_link_libraries(core-link-check "$<LINK_LIBRARY:WHOLE_ARCHIVE,chiaki-lib>" m)
add_test(NAME chiaki_core_link_check COMMAND core-link-check)
