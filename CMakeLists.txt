cmake_minimum_required(VERSION 3.20)
project(wf-benchmark VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(WF_BUILD_TESTS     "Build unit tests"           ON)
option(WF_BUILD_OPENMP    "Build OpenMP code paths"    ON)
option(WF_BUILD_MPI       "Build MPI code paths"       ON)

# ---------------------------------------------------------------------------
# Warnings (avoid -Werror for cross-platform safety)
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4)
endif()

# ---------------------------------------------------------------------------
# OpenMP detection
# ---------------------------------------------------------------------------
if(WF_BUILD_OPENMP)
    if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
        # AppleClang requires -Xpreprocessor -fopenmp + Homebrew libomp.
        # CMake's built-in FindOpenMP does not handle this well, so we
        # detect Homebrew libomp manually and create the target ourselves.
        find_path(OpenMP_CXX_INCLUDE_DIR
            NAMES omp.h
            HINTS
                /opt/homebrew/opt/libomp/include
                /usr/local/opt/libomp/include
        )
        find_library(OpenMP_CXX_LIBRARY
            NAMES omp libomp
            HINTS
                /opt/homebrew/opt/libomp/lib
                /usr/local/opt/libomp/lib
        )
        if(OpenMP_CXX_INCLUDE_DIR AND OpenMP_CXX_LIBRARY)
            set(OpenMP_FOUND TRUE)
            add_library(OpenMP::OpenMP_CXX INTERFACE IMPORTED)
            set_target_properties(OpenMP::OpenMP_CXX PROPERTIES
                INTERFACE_COMPILE_OPTIONS "-Xpreprocessor;-fopenmp"
                INTERFACE_INCLUDE_DIRECTORIES "${OpenMP_CXX_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES "${OpenMP_CXX_LIBRARY}"
            )
            message(STATUS "OpenMP: found  (AppleClang, ${OpenMP_CXX_LIBRARY})")
        else()
            message(STATUS "OpenMP: not found — OpenMP stubs will be excluded")
        endif()
    else()
        # Standard platforms — use CMake built-in module
        find_package(OpenMP QUIET)
        if(OpenMP_FOUND)
            message(STATUS "OpenMP: found  (${OpenMP_CXX_VERSION})")
        else()
            message(STATUS "OpenMP: not found — OpenMP stubs will be excluded")
        endif()
    endif()
else()
    message(STATUS "OpenMP: disabled by WF_BUILD_OPENMP=OFF")
endif()

# ---------------------------------------------------------------------------
# MPI detection
# ---------------------------------------------------------------------------
if(WF_BUILD_MPI)
    find_package(MPI QUIET COMPONENTS CXX)
    if(MPI_FOUND)
        message(STATUS "MPI:    found  (${MPI_CXX_VERSION})")
    else()
        message(STATUS "MPI:    not found — MPI stubs will be excluded")
    endif()
else()
    message(STATUS "MPI:    disabled by WF_BUILD_MPI=OFF")
endif()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(WF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Formatting targets (only when clang-format is available)
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCES
        *.cpp *.hpp *.h *.cxx *.hxx
    )
    set(ALL_FORMAT_FILES ${ALL_SOURCES})
    list(FILTER ALL_FORMAT_FILES EXCLUDE REGEX "build/")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i -style=file ${ALL_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting sources with clang-format"
    )
    add_custom_target(check-format
        COMMAND ${CLANG_FORMAT} --dry-run -Werror -style=file ${ALL_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking formatting with clang-format"
    )
    message(STATUS "clang-format: found  (${CLANG_FORMAT})")
else()
    message(STATUS "clang-format: not found — format targets not available")
endif()
