# Examples require Pico SDK to be initialized
if(NOT TARGET pico_stdlib)
    message(WARNING "Pico SDK not initialized, skipping examples")
    return()
endif()

# Build examples
add_subdirectory(basic)
add_subdirectory(debugger)
add_subdirectory(test_suite)
