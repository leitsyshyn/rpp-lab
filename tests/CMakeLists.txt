include(FetchContent)
find_package(Python3 QUIET COMPONENTS Interpreter)
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

if(OpenMP_FOUND)
    target_sources(wf_tests PRIVATE
        test_openmp.cpp
        ${CMAKE_SOURCE_DIR}/src/openmp/openmp.cpp
    )
    target_compile_definitions(wf_tests PRIVATE WF_HAS_OPENMP)
    target_link_libraries(wf_tests PRIVATE OpenMP::OpenMP_CXX)
endif()

if(MPI_FOUND)
    add_test(
        NAME wf_mpi_compare_suite
        COMMAND
            ${CMAKE_COMMAND}
            -DWF_TEST_EXECUTABLE=$<TARGET_FILE:wf-benchmark>
            -DWF_TEST_MPIEXEC=${MPIEXEC_EXECUTABLE}
            -DWF_TEST_MPIEXEC_NUMPROC_FLAG=${MPIEXEC_NUMPROC_FLAG}
            -DWF_TEST_CASE=compare_suite
            -DWF_TEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/mpi_compare_suite
            -P ${CMAKE_CURRENT_SOURCE_DIR}/mpi_integration.cmake
    )

    add_test(
        NAME wf_mpi_benchmark_suite
        COMMAND
            ${CMAKE_COMMAND}
            -DWF_TEST_EXECUTABLE=$<TARGET_FILE:wf-benchmark>
            -DWF_TEST_MPIEXEC=${MPIEXEC_EXECUTABLE}
            -DWF_TEST_MPIEXEC_NUMPROC_FLAG=${MPIEXEC_NUMPROC_FLAG}
            -DWF_TEST_CASE=benchmark_suite
            -DWF_TEST_TEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/mpi_benchmark_suite
            -P ${CMAKE_CURRENT_SOURCE_DIR}/mpi_integration.cmake
    )
endif()

if(Python3_Interpreter_FOUND)
    add_test(
        NAME wf_benchmark_script_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_benchmark_scripts.py
    )
endif()

include(GoogleTest)
gtest_discover_tests(wf_tests)
