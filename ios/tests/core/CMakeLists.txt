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

# A2.4: Bridge/CKConsoles.h and ckctl against a fake PS5 on 127.0.0.1 (consoles.c). The fake binds
# the ports a PS5 uses, so the test runs on its own and the suites above never touch the network.
set(CK_IOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_library(ck-consoles STATIC "${CK_IOS_DIR}/Bridge/ck_consoles.c" "${CK_IOS_DIR}/Bridge/ck_registry.c"
	"${CK_IOS_DIR}/cli/ckctl.c")
target_include_directories(ck-consoles PUBLIC "${CK_IOS_DIR}")
target_link_libraries(ck-consoles PUBLIC chiaki-lib)
target_compile_options(ck-consoles PRIVATE -Wall -Wextra -Werror)

add_executable(ck-consoles-tests consoles.c "${CK_IOS_DIR}/tests/fakes/fake_ps5.c")
target_include_directories(ck-consoles-tests PRIVATE "${CK_IOS_DIR}/tests/fakes")
target_compile_options(ck-consoles-tests PRIVATE -Wall -Wextra -Werror)
find_package(Threads REQUIRED)
target_link_libraries(ck-consoles-tests ck-consoles munit Threads::Threads)
add_test(NAME ck_consoles COMMAND ck-consoles-tests)
set_tests_properties(ck_consoles PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# ckctl itself, as the iPad runs it. The test only checks it starts and prints its usage.
add_executable(ckctl "${CK_IOS_DIR}/cli/main.c")
target_link_libraries(ckctl ck-consoles)
add_test(NAME ckctl_help COMMAND ckctl --help)

# A2.5: the saved registrations (Bridge/ck_registry.h). registry.c works on files in the temporary
# folder; check_registry_plist.py reads and writes the same files with Python's plistlib. Python
# runs on the host, by path: as a target, ctest would run it under the armv7 emulator too.
add_executable(ck-registry-tests registry.c)
target_compile_options(ck-registry-tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ck-registry-tests ck-consoles munit)
add_test(NAME ck_registry COMMAND ck-registry-tests)

add_executable(ck-registry-tool registry_tool.c)
target_compile_options(ck-registry-tool PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ck-registry-tool ck-consoles)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ck_registry_plistlib
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_registry_plist.py"
		${CMAKE_CROSSCOMPILING_EMULATOR} "$<TARGET_FILE:ck-registry-tool>")
