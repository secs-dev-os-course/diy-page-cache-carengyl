include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(block_cache_tests
    block_cache_tests.cpp
)

target_link_libraries(block_cache_tests
    PRIVATE
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(block_cache_tests)
