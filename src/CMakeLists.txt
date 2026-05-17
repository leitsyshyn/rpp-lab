# Shared core library — compiled, not header-only
add_library(wf_core STATIC
    core/version.cpp
    core/primitives.cpp
)
target_include_directories(wf_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_features(wf_core PUBLIC cxx_std_20)

# Single benchmark executable
add_executable(wf-benchmark
    main.cpp
    sequential/sequential.cpp
)
set_target_properties(wf-benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_link_libraries(wf-benchmark PRIVATE wf_core)

# OpenMP support — conditional
if(OpenMP_FOUND)
    target_sources(wf-benchmark PRIVATE openmp/openmp.cpp)
    target_compile_definitions(wf-benchmark PRIVATE WF_HAS_OPENMP)
    target_link_libraries(wf-benchmark PRIVATE OpenMP::OpenMP_CXX)
endif()

# MPI support — conditional
if(MPI_FOUND)
    target_sources(wf-benchmark PRIVATE mpi/mpi.cpp)
    target_compile_definitions(wf-benchmark PRIVATE WF_HAS_MPI)
    target_link_libraries(wf-benchmark PRIVATE MPI::MPI_CXX)
endif()
