#ifdef WF_HAS_OPENMP

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <wf/contracts.h>
#include <wf/primitives.h>
#include <wf/runners.h>

namespace {

std::filesystem::path unique_temp_path(std::string_view prefix) {
    const auto unique_id = ::testing::UnitTest::GetInstance()->random_seed();
    static std::uint64_t counter = 0;
    return std::filesystem::temp_directory_path() /
           (std::string(prefix) + "_" + std::to_string(unique_id) + "_" +
            std::to_string(++counter) + ".txt");
}

void write_text_file(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.is_open());
    output << contents;
    ASSERT_TRUE(output.good());
}

std::string read_output_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.is_open());
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

wf::run_summary
run_sequential_for_test(const std::filesystem::path& input_path, bool output_enabled = false,
                        const std::optional<std::filesystem::path>& output_path = std::nullopt,
                        bool benchmark_enabled = false) {
    wf::run_config config;
    config.selected_method = wf::execution_method::sequential;
    config.input_path = input_path;
    config.output_enabled = output_enabled;
    config.output_path = output_path;
    config.benchmark_enabled = benchmark_enabled;
    return wf::run_sequential(config);
}

wf::run_summary
run_openmp_for_test(const std::filesystem::path& input_path, std::uint64_t workers,
                    bool output_enabled = false,
                    const std::optional<std::filesystem::path>& output_path = std::nullopt,
                    bool benchmark_enabled = false) {
    wf::run_config config;
    config.selected_method = wf::execution_method::openmp;
    config.input_path = input_path;
    config.output_enabled = output_enabled;
    config.output_path = output_path;
    config.benchmark_enabled = benchmark_enabled;
    config.requested_worker_count = workers;
    return wf::run_openmp(config);
}

void expect_openmp_matches_sequential(std::string_view contents, std::uint64_t workers) {
    const auto input_path = unique_temp_path("wf_openmp_match_input");
    write_text_file(input_path, contents);

    const wf::run_summary sequential_summary = run_sequential_for_test(input_path);
    const wf::run_summary openmp_summary = run_openmp_for_test(input_path, workers);

    ASSERT_TRUE(sequential_summary.result.frequencies.has_value());
    ASSERT_TRUE(openmp_summary.result.frequencies.has_value());
    EXPECT_EQ(*openmp_summary.result.frequencies, *sequential_summary.result.frequencies);
    EXPECT_EQ(openmp_summary.result.total_word_count, sequential_summary.result.total_word_count);
    EXPECT_EQ(openmp_summary.result.unique_word_count, sequential_summary.result.unique_word_count);
    EXPECT_EQ(openmp_summary.result.input_size_bytes, sequential_summary.result.input_size_bytes);

    std::filesystem::remove(input_path);
}

} // namespace

TEST(OpenMPRunnerTest, OneWorkerOutputMatchesSequential) {
    expect_openmp_matches_sequential("Apple banana apple.\nMPI-2026 mpi\n", 1);
}

TEST(OpenMPRunnerTest, MultiWorkerOutputMatchesSequential) {
    expect_openmp_matches_sequential("Alpha,beta! GAMMA\t42...done alpha 42 beta\n", 4);
}

TEST(OpenMPRunnerTest, EmptyInputWorks) {
    expect_openmp_matches_sequential("", 4);
}

TEST(OpenMPRunnerTest, DelimiterOnlyInputWorks) {
    expect_openmp_matches_sequential("...\n\t--- !!!", 6);
}

TEST(OpenMPRunnerTest, InputSmallerThanWorkerCountWorks) {
    expect_openmp_matches_sequential("Go!", 8);
}

TEST(OpenMPRunnerTest, BoundarySensitiveInputCountsWordsExactlyOnce) {
    expect_openmp_matches_sequential("alpha betaGammaDelta epsilon zetaEtaTheta iota\n", 4);
}

TEST(OpenMPRunnerTest, LongWordCrossingChunkBoundariesWorks) {
    const std::string long_word(8192, 'A');
    const std::string contents = "prefix " + long_word + " suffix " + long_word + "\n";
    expect_openmp_matches_sequential(contents, 8);
}

TEST(OpenMPRunnerTest, NoOutputBenchmarkModeStillComputesCorrectResult) {
    const auto input_path = unique_temp_path("wf_openmp_benchmark_input");
    write_text_file(input_path, "One two TWO three three three\n");

    const wf::run_summary sequential_summary = run_sequential_for_test(input_path);
    const wf::run_summary openmp_summary =
        run_openmp_for_test(input_path, 4, false, std::nullopt, true);

    ASSERT_TRUE(sequential_summary.result.frequencies.has_value());
    ASSERT_TRUE(openmp_summary.result.frequencies.has_value());
    ASSERT_TRUE(openmp_summary.benchmark.has_value());
    EXPECT_EQ(*openmp_summary.result.frequencies, *sequential_summary.result.frequencies);
    EXPECT_EQ(openmp_summary.benchmark->method, wf::execution_method::openmp);
    EXPECT_EQ(openmp_summary.benchmark->worker_count, 4U);
    EXPECT_EQ(openmp_summary.benchmark->word_count, openmp_summary.result.total_word_count);
    EXPECT_EQ(openmp_summary.benchmark->unique_word_count, openmp_summary.result.unique_word_count);
    ASSERT_EQ(openmp_summary.benchmark->phases.size(), 5U);
    EXPECT_EQ(openmp_summary.benchmark->phases[0].name, "read");
    EXPECT_EQ(openmp_summary.benchmark->phases[1].name, "parallel_tokenize_count");
    EXPECT_EQ(openmp_summary.benchmark->phases[2].name, "tree_merge");
    EXPECT_EQ(openmp_summary.benchmark->phases[3].name, "canonicalize");
    EXPECT_EQ(openmp_summary.benchmark->phases[4].name, "total");

    std::filesystem::remove(input_path);
}

TEST(OpenMPRunnerTest, OutputFileModeWritesDeterministicOutput) {
    const auto input_path = unique_temp_path("wf_openmp_output_input");
    const auto sequential_output_path = unique_temp_path("wf_sequential_output");
    const auto openmp_output_path = unique_temp_path("wf_openmp_output");
    write_text_file(input_path, "pear apple pear Banana banana 2026\n");

    const wf::run_summary sequential_summary =
        run_sequential_for_test(input_path, true, sequential_output_path, true);
    const wf::run_summary openmp_summary =
        run_openmp_for_test(input_path, 4, true, openmp_output_path, true);

    ASSERT_TRUE(sequential_summary.result.frequencies.has_value());
    ASSERT_TRUE(openmp_summary.result.frequencies.has_value());
    EXPECT_EQ(*openmp_summary.result.frequencies, *sequential_summary.result.frequencies);
    EXPECT_EQ(read_output_file(openmp_output_path), read_output_file(sequential_output_path));
    ASSERT_TRUE(openmp_summary.benchmark.has_value());
    ASSERT_EQ(openmp_summary.benchmark->phases.size(), 6U);
    EXPECT_EQ(openmp_summary.benchmark->phases[4].name, "write");
    EXPECT_EQ(openmp_summary.benchmark->phases[5].name, "total");

    std::filesystem::remove(input_path);
    std::filesystem::remove(sequential_output_path);
    std::filesystem::remove(openmp_output_path);
}

#endif
