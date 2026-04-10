# ESPStateMachine - https://github.com/ESPToolKit/esp-state-machine
# Copyright zekageri 2026
# MIT License

cmake_minimum_required(VERSION 3.12)
project(ESPStateMachine)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(${COVERAGE})
	set(CMAKE_CXX_FLAGS "-fprofile-arcs -ftest-coverage -g -O0")
endif()

add_library(esp_state_machine INTERFACE)
add_library(ESPStateMachine::esp_state_machine ALIAS esp_state_machine)

target_include_directories(esp_state_machine
	INTERFACE
		${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_features(esp_state_machine INTERFACE cxx_std_17)

if(BUILD_TESTING)
	add_subdirectory(test)
endif()
