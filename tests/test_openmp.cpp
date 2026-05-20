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
#include <wf/utils.h>
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

wf::run_result
run_sequential_for_test(const std::filesystem::path& input_path, bool output_enabled = false,
                        const std::optional<std::filesystem::path>& output_path = std::nullopt,
                        bool benchmark_enabled = false) {
    wf::run_config config;
    config.selected_method = wf::method::sequential;
    config.input_path = input_path;
    config.output_enabled = output_enabled;
    config.output_path = output_path;
    config.benchmark_enabled = benchmark_enabled;
    return wf::run_sequential(config);
}

wf::run_result
run_openmp_for_test(const std::filesystem::path& input_path, int workers,
                    bool output_enabled = false,
                    const std::optional<std::filesystem::path>& output_path = std::nullopt,
                    bool benchmark_enabled = false) {
    wf::run_config config;
    config.selected_method = wf::method::openmp;
    config.input_path = input_path;
    config.output_enabled = output_enabled;
    config.output_path = output_path;
    config.benchmark_enabled = benchmark_enabled;
    config.requested_worker_count = workers;
    return wf::run_openmp(config);
}

void expect_openmp_matches_sequential(std::string_view contents, int workers) {
    const auto input_path = unique_temp_path("wf_openmp_match_input");
    write_text_file(input_path, contents);

    const wf::run_result sequential_result = run_sequential_for_test(input_path);
    const wf::run_result openmp_result = run_openmp_for_test(input_path, workers);

    ASSERT_TRUE(sequential_result.frequencies.has_value());
    ASSERT_TRUE(openmp_result.frequencies.has_value());
    EXPECT_EQ(*openmp_result.frequencies, *sequential_result.frequencies);
    EXPECT_EQ(openmp_result.total_word_count, sequential_result.total_word_count);
    EXPECT_EQ(openmp_result.unique_word_count, sequential_result.unique_word_count);
    EXPECT_EQ(openmp_result.text_size, sequential_result.text_size);

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

    const wf::run_result sequential_result = run_sequential_for_test(input_path);
    const wf::run_result openmp_result =
        run_openmp_for_test(input_path, 4, false, std::nullopt, true);

    ASSERT_TRUE(sequential_result.frequencies.has_value());
    ASSERT_TRUE(openmp_result.frequencies.has_value());
    ASSERT_TRUE(openmp_result.benchmark.has_value());
    EXPECT_EQ(*openmp_result.frequencies, *sequential_result.frequencies);
    EXPECT_EQ(openmp_result.benchmark->worker_count, 4);
    EXPECT_EQ(openmp_result.total_word_count, 6U);
    EXPECT_EQ(openmp_result.unique_word_count, 3U);
    ASSERT_EQ(openmp_result.benchmark->phases.size(), 6U);
    EXPECT_EQ(openmp_result.benchmark->phases[0].name, "read");
    EXPECT_EQ(openmp_result.benchmark->phases[1].name, "partition");
    EXPECT_EQ(openmp_result.benchmark->phases[2].name, "count");
    EXPECT_EQ(openmp_result.benchmark->phases[3].name, "merge");
    EXPECT_EQ(openmp_result.benchmark->phases[4].name, "finalize");
    EXPECT_EQ(openmp_result.benchmark->phases[5].name, "write");

    std::filesystem::remove(input_path);
}

TEST(OpenMPRunnerTest, OutputFileModeWritesDeterministicOutput) {
    const auto input_path = unique_temp_path("wf_openmp_output_input");
    const auto sequential_output_path = unique_temp_path("wf_sequential_output");
    const auto openmp_output_path = unique_temp_path("wf_openmp_output");
    write_text_file(input_path, "pear apple pear Banana banana 2026\n");

    const wf::run_result sequential_result =
        run_sequential_for_test(input_path, true, sequential_output_path, true);
    const wf::run_result openmp_result =
        run_openmp_for_test(input_path, 4, true, openmp_output_path, true);

    ASSERT_TRUE(sequential_result.frequencies.has_value());
    ASSERT_TRUE(openmp_result.frequencies.has_value());
    EXPECT_EQ(*openmp_result.frequencies, *sequential_result.frequencies);
    EXPECT_EQ(read_output_file(openmp_output_path), read_output_file(sequential_output_path));
    ASSERT_TRUE(openmp_result.benchmark.has_value());
    ASSERT_EQ(openmp_result.benchmark->phases.size(), 6U);
    EXPECT_EQ(openmp_result.benchmark->phases[5].name, "write");

    std::filesystem::remove(input_path);
    std::filesystem::remove(sequential_output_path);
    std::filesystem::remove(openmp_output_path);
}

#endif
