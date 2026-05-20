#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <wf/contracts.h>
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

} // namespace

TEST(SequentialRunnerTest, ProducesExpectedFrequenciesWithoutOutput) {
    const auto input_path = unique_temp_path("wf_sequential_input");
    write_text_file(input_path, "Apple banana apple.\nMPI-2026 mpi\n");

    wf::run_config config;
    config.selected_method = wf::method::sequential;
    config.input_path = input_path;
    config.output_enabled = false;

    const wf::run_result result = wf::run_sequential(config);

    ASSERT_TRUE(result.frequencies.has_value());
    const wf::frequency_map expected{{"2026", 1}, {"apple", 2}, {"banana", 1}, {"mpi", 2}};
    EXPECT_EQ(*result.frequencies, expected);
    EXPECT_EQ(result.total_word_count, 6U);
    EXPECT_EQ(result.unique_word_count, 4U);
    EXPECT_EQ(result.text_size, 33U);
    EXPECT_FALSE(result.benchmark.has_value());

    std::filesystem::remove(input_path);
}

TEST(SequentialRunnerTest, BenchmarkModeBuildsExpectedPhases) {
    const auto input_path = unique_temp_path("wf_sequential_benchmark_input");
    write_text_file(input_path, "one two two three\n");

    wf::run_config config;
    config.selected_method = wf::method::sequential;
    config.input_path = input_path;
    config.output_enabled = false;
    config.benchmark_enabled = true;
    config.requested_worker_count = 8;

    const wf::run_result result = wf::run_sequential(config);

    ASSERT_TRUE(result.benchmark.has_value());
    EXPECT_EQ(result.benchmark->worker_count, 1);
    EXPECT_EQ(result.total_word_count, 4U);
    EXPECT_EQ(result.unique_word_count, 3U);
    ASSERT_EQ(result.benchmark->phases.size(), 4U);
    EXPECT_EQ(result.benchmark->phases[0].name, "read");
    EXPECT_EQ(result.benchmark->phases[1].name, "count");
    EXPECT_EQ(result.benchmark->phases[2].name, "finalize");
    EXPECT_EQ(result.benchmark->phases[3].name, "write");

    std::filesystem::remove(input_path);
}

TEST(SequentialRunnerTest, OutputFileModeWritesExpectedContent) {
    const auto input_path = unique_temp_path("wf_sequential_output_input");
    const auto output_path = unique_temp_path("wf_sequential_output_result");
    write_text_file(input_path, "pear apple pear\n");

    wf::run_config config;
    config.selected_method = wf::method::sequential;
    config.input_path = input_path;
    config.output_enabled = true;
    config.output_path = output_path;
    config.benchmark_enabled = true;

    const wf::run_result result = wf::run_sequential(config);

    std::ifstream input(output_path, std::ios::binary);
    ASSERT_TRUE(input.is_open());
    std::stringstream buffer;
    buffer << input.rdbuf();
    EXPECT_EQ(buffer.str(), std::string("apple 1\npear 2\n"));
    ASSERT_TRUE(result.benchmark.has_value());
    ASSERT_EQ(result.benchmark->phases.size(), 4U);
    EXPECT_EQ(result.benchmark->phases[2].name, "finalize");
    EXPECT_EQ(result.benchmark->phases[3].name, "write");

    std::filesystem::remove(input_path);
    std::filesystem::remove(output_path);
}
