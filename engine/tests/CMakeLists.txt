# Nova Engine — unit tests (doctest).

add_executable(nova_tests
    test_main.cpp
    test_core.cpp
    test_memory.cpp
    test_math.cpp
    test_input.cpp
    test_console.cpp
    test_vfs.cpp
    test_renderer.cpp
)

target_include_directories(nova_tests SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/doctest
)

target_link_libraries(nova_tests PRIVATE nova_engine)

# Tests themselves compile with exceptions (doctest requires them) even
# though the engine core is -fno-exceptions.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nova_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME nova_tests COMMAND nova_tests)
