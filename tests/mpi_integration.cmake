if(NOT DEFINED WF_TEST_EXECUTABLE)
    message(FATAL_ERROR "WF_TEST_EXECUTABLE is required")
endif()
if(NOT DEFINED WF_TEST_MPIEXEC)
    message(FATAL_ERROR "WF_TEST_MPIEXEC is required")
endif()
if(NOT DEFINED WF_TEST_MPIEXEC_NUMPROC_FLAG)
    message(FATAL_ERROR "WF_TEST_MPIEXEC_NUMPROC_FLAG is required")
endif()
if(NOT DEFINED WF_TEST_CASE)
    message(FATAL_ERROR "WF_TEST_CASE is required")
endif()
if(NOT DEFINED WF_TEST_TEMP_DIR)
    message(FATAL_ERROR "WF_TEST_TEMP_DIR is required")
endif()

file(MAKE_DIRECTORY "${WF_TEST_TEMP_DIR}")

function(wf_run_checked)
    set(options)
    set(one_value_args RESULT OUTPUT ERROR)
    set(multi_value_args COMMAND)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "${options}" "${one_value_args}" "${multi_value_args}")

    execute_process(
        COMMAND ${ARG_COMMAND}
        RESULT_VARIABLE wf_result
        OUTPUT_VARIABLE wf_output
        ERROR_VARIABLE wf_error
    )

    if(NOT wf_result EQUAL 0)
        message(FATAL_ERROR
            "command failed (${wf_result})\n"
            "command: ${ARG_COMMAND}\n"
            "stdout:\n${wf_output}\n"
            "stderr:\n${wf_error}\n")
    endif()

    set(${ARG_RESULT} "${wf_result}" PARENT_SCOPE)
    set(${ARG_OUTPUT} "${wf_output}" PARENT_SCOPE)
    set(${ARG_ERROR} "${wf_error}" PARENT_SCOPE)
endfunction()

function(wf_compare_sequential_and_mpi case_name input_text process_count)
    set(input_path "${WF_TEST_TEMP_DIR}/${case_name}.txt")
    set(sequential_output_path "${WF_TEST_TEMP_DIR}/${case_name}_sequential.txt")
    set(mpi_output_path "${WF_TEST_TEMP_DIR}/${case_name}_mpi.txt")

    file(WRITE "${input_path}" "${input_text}")

    wf_run_checked(
        COMMAND "${WF_TEST_EXECUTABLE}" --mode sequential "${input_path}" --output "${sequential_output_path}"
        RESULT sequential_result
        OUTPUT sequential_stdout
        ERROR sequential_stderr
    )
    wf_run_checked(
        COMMAND "${WF_TEST_MPIEXEC}" "${WF_TEST_MPIEXEC_NUMPROC_FLAG}" "${process_count}" "${WF_TEST_EXECUTABLE}" --mode mpi "${input_path}" --output "${mpi_output_path}"
        RESULT mpi_result
        OUTPUT mpi_stdout
        ERROR mpi_stderr
    )

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${sequential_output_path}" "${mpi_output_path}"
        RESULT_VARIABLE compare_result
    )
    if(NOT compare_result EQUAL 0)
        file(READ "${sequential_output_path}" sequential_contents)
        file(READ "${mpi_output_path}" mpi_contents)
        message(FATAL_ERROR
            "sequential and MPI outputs differ for ${case_name}\n"
            "sequential:\n${sequential_contents}\n"
            "mpi:\n${mpi_contents}\n")
    endif()
endfunction()

function(wf_generate_many_unique_words output_variable)
    set(generated_text "")
    foreach(index RANGE 1 512)
        string(APPEND generated_text "word${index} ")
    endforeach()
    foreach(index RANGE 129 384)
        string(APPEND generated_text "word${index} ")
    endforeach()
    string(APPEND generated_text "\n")

    set(${output_variable} "${generated_text}" PARENT_SCOPE)
endfunction()

if(WF_TEST_CASE STREQUAL "compare_suite")
    wf_compare_sequential_and_mpi("basic_np1" "Apple banana apple.\nMPI-2026 mpi\n" 1)
    wf_compare_sequential_and_mpi("basic_np4" "Apple banana apple.\nMPI-2026 mpi\n" 4)
    wf_compare_sequential_and_mpi("empty_np4" "" 4)
    wf_compare_sequential_and_mpi("delimiters_only_np4" "... !!! --- ___     \n\t\n" 4)
    wf_compare_sequential_and_mpi("small_np8" "Go!" 8)
    wf_compare_sequential_and_mpi("boundary_np4" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa word2 WORD2\n" 4)
    wf_compare_sequential_and_mpi(
        "mixed_np4"
        "Alpha,beta! GAMMA\t42...done alpha 42 beta\n"
        4)
    wf_generate_many_unique_words(many_unique_words_input)
    wf_compare_sequential_and_mpi("many_unique_words_np4" "${many_unique_words_input}" 4)
    string(REPEAT "A" 8192 long_word)
    wf_compare_sequential_and_mpi(
        "long_word_np8"
        "prefix ${long_word} suffix ${long_word}\n"
        8)
    return()
endif()

if(WF_TEST_CASE STREQUAL "benchmark_suite")
    set(input_path "${WF_TEST_TEMP_DIR}/benchmark_input.txt")
    file(WRITE "${input_path}" "One two TWO three three three\n")

    wf_run_checked(
        COMMAND "${WF_TEST_MPIEXEC}" "${WF_TEST_MPIEXEC_NUMPROC_FLAG}" 4 "${WF_TEST_EXECUTABLE}" --mode mpi "${input_path}" --no-output --benchmark
        RESULT mpi_result
        OUTPUT mpi_stdout
        ERROR mpi_stderr
    )

    if(NOT mpi_stdout STREQUAL "")
        message(FATAL_ERROR "expected no stdout for --no-output benchmark run, got:\n${mpi_stdout}")
    endif()

    foreach(required_fragment
            "method: mpi"
            "worker_count: 4"
            "read"
            "partition"
            "count"
            "bucketize"
            "alltoall_sizes"
            "alltoall_data"
            "merge"
            "gather"
            "finalize"
            "total")
        string(FIND "${mpi_stderr}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "missing benchmark fragment '${required_fragment}'\n"
                "stderr:\n${mpi_stderr}\n")
        endif()
    endforeach()

    string(REGEX MATCHALL "method: mpi" method_matches "${mpi_stderr}")
    list(LENGTH method_matches method_match_count)
    if(NOT method_match_count EQUAL 1)
        message(FATAL_ERROR
            "expected exactly one MPI benchmark report, saw ${method_match_count}\n"
            "stderr:\n${mpi_stderr}\n")
    endif()

    return()
endif()

message(FATAL_ERROR "unknown WF_TEST_CASE: ${WF_TEST_CASE}")
