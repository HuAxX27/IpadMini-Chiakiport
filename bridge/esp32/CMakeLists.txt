# ESP-IDF project of the ESP32 bridge. Needs ESP-IDF 5.3 and ./deps.sh run once.
cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_STANDARD 11)

set(BLUEPAD32_ROOT ${CMAKE_CURRENT_LIST_DIR}/deps/bluepad32)
if(NOT EXISTS ${BLUEPAD32_ROOT}/src/components/btstack)
	message(FATAL_ERROR "Bluepad32 and BTstack are missing: run ./deps.sh in bridge/esp32 first")
endif()
set(EXTRA_COMPONENT_DIRS ${BLUEPAD32_ROOT}/src/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ck_bridge)
