include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(wf_tests
    test_smoke.cpp
    test_primitives.cpp
    test_sequential.cpp
    ${CMAKE_SOURCE_DIR}/src/sequential/sequential.cpp
)
target_link_libraries(wf_tests PRIVATE wf_core GTest::gtest_main)
target_compile_features(wf_tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(wf_tests)
