add_executable(esp_state_machine_tests
	test_esp_state_machine/test_esp_state_machine.cpp
)

target_include_directories(esp_state_machine_tests
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

target_link_libraries(esp_state_machine_tests
	PRIVATE
		ESPStateMachine::esp_state_machine
)

target_compile_features(esp_state_machine_tests PRIVATE cxx_std_17)

add_test(NAME esp_state_machine_tests COMMAND esp_state_machine_tests)
