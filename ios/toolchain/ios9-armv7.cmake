# CMake toolchain file for the iPad mini 1: armv7, iOS 9.0, built on Linux with
# Theos' clang 11 and ld64 against the patched iOS 9.3 SDK (install-theos.sh).
#
#   cmake -S . -B build-ios -G Ninja -DCMAKE_TOOLCHAIN_FILE=ios/toolchain/ios9-armv7.cmake
#
# Variables, from -D or the environment:
#   THEOS          where install-theos.sh put Theos (default /opt/theos, as in
#                  the Docker image).
#   CK_IOS_PREFIX  optional prefix with libraries already built for armv7
#                  (build-deps.sh). find_library() and find_path() look there
#                  and in the SDK, never in the Linux host's /usr, and its
#                  include/ is on every compile's include path.
#
# Every target compiles with -Werror=unguarded-availability: calling an API
# newer than iOS 9.0 without an availability check is a build error. C code
# also gets -Werror=implicit-function-declaration.
#
# ck_ios_sign(<target>) signs an executable with ldid after linking, which
# iOS needs to run it even on a jailbroken device.

if(NOT THEOS)
	if(DEFINED ENV{THEOS})
		set(THEOS "$ENV{THEOS}")
	else()
		set(THEOS /opt/theos)
	endif()
endif()
if(NOT CK_IOS_PREFIX AND DEFINED ENV{CK_IOS_PREFIX})
	set(CK_IOS_PREFIX "$ENV{CK_IOS_PREFIX}")
endif()
# try_compile() projects read this file again; pass the variables along.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES THEOS CK_IOS_PREFIX)

set(CK_IOS_SDK "${THEOS}/sdks/iPhoneOS9.3.sdk")
set(CK_IOS_BIN "${THEOS}/toolchain/linux/iphone/bin")
if(NOT EXISTS "${CK_IOS_SDK}/.ck-sdk-fixes" OR NOT EXISTS "${CK_IOS_BIN}/clang")
	message(FATAL_ERROR "No iOS 9 toolchain in THEOS=${THEOS}. Run ios/toolchain/install-theos.sh or use the Docker image.")
endif()

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_VERSION 9.0)
set(CMAKE_SYSTEM_PROCESSOR armv7)
set(CMAKE_OSX_SYSROOT "${CK_IOS_SDK}" CACHE PATH "iOS SDK" FORCE)
set(CMAKE_OSX_ARCHITECTURES armv7 CACHE STRING "iOS architectures" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET 9.0 CACHE STRING "Minimum iOS version" FORCE)

set(CMAKE_C_COMPILER "${CK_IOS_BIN}/clang")
set(CMAKE_CXX_COMPILER "${CK_IOS_BIN}/clang++")
set(CMAKE_ASM_COMPILER "${CK_IOS_BIN}/clang")
set(CMAKE_C_COMPILER_TARGET armv7-apple-ios9.0)
set(CMAKE_CXX_COMPILER_TARGET armv7-apple-ios9.0)
set(CMAKE_ASM_COMPILER_TARGET armv7-apple-ios9.0)
# The Mach-O binutils. They live only here, never on PATH (see README.md).
# cctools ar calls a prefixed ranlib that is not shipped, so use llvm-ar,
# which writes the Darwin archive format for Mach-O objects.
set(CMAKE_AR "${CK_IOS_BIN}/llvm-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${CK_IOS_BIN}/ranlib" CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP "${CK_IOS_BIN}/strip" CACHE FILEPATH "" FORCE)
set(CMAKE_NM "${CK_IOS_BIN}/nm" CACHE FILEPATH "" FORCE)
set(CMAKE_OBJDUMP "${CK_IOS_BIN}/llvm-objdump" CACHE FILEPATH "" FORCE)
set(CMAKE_INSTALL_NAME_TOOL "${CK_IOS_BIN}/install_name_tool" CACHE FILEPATH "" FORCE)
set(CMAKE_LIPO "${CK_IOS_BIN}/lipo" CACHE FILEPATH "" FORCE)
set(CK_IOS_LDID "${CK_IOS_BIN}/ldid")

# An implicitly declared function returns int, and on armv7 a float result
# comes back in a core register, so the caller would read garbage: make it an
# error too.
set(CMAKE_C_FLAGS_INIT "-Werror=unguarded-availability -Werror=implicit-function-declaration")
set(CMAKE_CXX_FLAGS_INIT "-Werror=unguarded-availability")
# Plain executables, not .app bundles; the app itself is built by Theos.
set(CMAKE_MACOSX_BUNDLE OFF)

# Look for libraries and headers only in the SDK and CK_IOS_PREFIX, and for
# programs (python3, protoc) only on the host.
set(CMAKE_FIND_ROOT_PATH "${CK_IOS_SDK}")
if(CK_IOS_PREFIX)
	list(APPEND CMAKE_FIND_ROOT_PATH "${CK_IOS_PREFIX}")
	set(ENV{PKG_CONFIG_LIBDIR} "${CK_IOS_PREFIX}/lib/pkgconfig")
	# The core includes <mbedtls/...> without asking CMake where it lives.
	foreach(lang C CXX OBJC OBJCXX)
		set(CMAKE_${lang}_STANDARD_INCLUDE_DIRECTORIES "${CK_IOS_PREFIX}/include")
	endforeach()
else()
	set(ENV{PKG_CONFIG_LIBDIR} "")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

function(ck_ios_sign target)
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND "${CK_IOS_LDID}" -S "$<TARGET_FILE:${target}>"
		COMMENT "Signing ${target} with ldid"
		VERBATIM)
endfunction()
