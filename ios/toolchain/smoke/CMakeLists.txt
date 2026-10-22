# The same smoke tool built with ios9-armv7.cmake instead of Theos, so the
# CMake toolchain file is checked too. smoke-test.sh drives both builds.
cmake_minimum_required(VERSION 3.16)
project(ck_smoke C)

add_executable(ck-smoke main.c)
target_compile_options(ck-smoke PRIVATE -std=c11 -Wall -Werror)
ck_ios_sign(ck-smoke)
