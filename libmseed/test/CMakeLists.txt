# Test suite for libmseed

# Test programs, discovered like the Makefile does, excluding the TAU runner
file(GLOB TEST_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS test-*.c)
list(REMOVE_ITEM TEST_SOURCES test-runner.c)
list(SORT TEST_SOURCES)

# Example-based tests (symlinked in test directory)
file(GLOB EXAMPLE_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS lm_*.c)
list(SORT EXAMPLE_SOURCES)

# Determine which library target to use
# Prefer static library to avoid shared library path issues during testing
if(TARGET mseed_static)
    set(MSEED_TEST_LIBRARY mseed_static)
elseif(TARGET mseed_shared)
    set(MSEED_TEST_LIBRARY mseed_shared)
else()
    message(FATAL_ERROR "No libmseed library target available for tests")
endif()

# TAU testing framework
add_library(tau STATIC
    tau/tau.h
    test-runner.c
)

target_include_directories(tau PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# Link tau with the library to access libmseed.h
target_link_libraries(tau PUBLIC ${MSEED_TEST_LIBRARY})

# Build test programs
foreach(source ${TEST_SOURCES})
    get_filename_component(test ${source} NAME_WE)

    add_executable(${test} ${source})

    # Link against the library and TAU
    target_link_libraries(${test} PRIVATE ${MSEED_TEST_LIBRARY} tau)

    # Set output directory
    set_target_properties(${test} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    # Add as a CTest
    add_test(NAME ${test} COMMAND ${test}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

# Build example-based tests
foreach(source ${EXAMPLE_SOURCES})
    get_filename_component(test ${source} NAME_WE)

    add_executable(${test}_test ${source})

    # Link against the library
    target_link_libraries(${test}_test PRIVATE ${MSEED_TEST_LIBRARY})

    # Set output directory
    set_target_properties(${test}_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    # These are demonstration programs, not unit tests
    # Uncomment if you want to add them as tests:
    # add_test(NAME ${test} COMMAND ${test}_test)
endforeach()
