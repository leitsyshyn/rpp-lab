#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

#include <wf/contracts.h>
#include <wf/runners.h>

namespace wf::detail {

count_type checked_word_count_size(std::size_t word_count, std::string_view input_path);
file_size_type checked_input_size_bytes(std::size_t input_size_bytes, std::string_view input_path);

} // namespace wf::detail

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
    config.selected_method = wf::execution_method::sequential;
    config.input_path = input_path;
    config.output_enabled = false;

    const wf::run_summary summary = wf::run_sequential(config);

    ASSERT_TRUE(summary.result.frequencies.has_value());
    const wf::frequency_map expected{{"2026", 1}, {"apple", 2}, {"banana", 1}, {"mpi", 2}};
    EXPECT_EQ(*summary.result.frequencies, expected);
    EXPECT_EQ(summary.result.total_word_count, 6U);
    EXPECT_EQ(summary.result.unique_word_count, 4U);
    ASSERT_TRUE(summary.result.input_size_bytes.has_value());
    EXPECT_EQ(*summary.result.input_size_bytes, 33U);
    EXPECT_FALSE(summary.benchmark.has_value());

    std::filesystem::remove(input_path);
}

TEST(SequentialRunnerTest, SequentialSizeConversionGuardsRejectOversizedValues) {
    if (std::numeric_limits<std::size_t>::max() >
        static_cast<std::size_t>(std::numeric_limits<wf::count_type>::max())) {
        EXPECT_THROW(static_cast<void>(wf::detail::checked_word_count_size(
                         static_cast<std::size_t>(std::numeric_limits<wf::count_type>::max()) + 1U,
                         "huge-input.txt")),
                     std::overflow_error);
    }

    if (std::numeric_limits<std::size_t>::max() >
        static_cast<std::size_t>(std::numeric_limits<wf::file_size_type>::max())) {
        EXPECT_THROW(
            static_cast<void>(wf::detail::checked_input_size_bytes(
                static_cast<std::size_t>(std::numeric_limits<wf::file_size_type>::max()) + 1U,
                "huge-input.txt")),
            std::overflow_error);
    }
}

TEST(SequentialRunnerTest, BenchmarkModeBuildsExpectedPhases) {
    const auto input_path = unique_temp_path("wf_sequential_benchmark_input");
    write_text_file(input_path, "one two two three\n");

    wf::run_config config;
    config.selected_method = wf::execution_method::sequential;
    config.input_path = input_path;
    config.output_enabled = false;
    config.benchmark_enabled = true;
    config.requested_worker_count = 8;

    const wf::run_summary summary = wf::run_sequential(config);

    ASSERT_TRUE(summary.benchmark.has_value());
    EXPECT_EQ(summary.benchmark->method, wf::execution_method::sequential);
    EXPECT_EQ(summary.benchmark->worker_count, 1U);
    EXPECT_EQ(summary.benchmark->word_count, 4U);
    EXPECT_EQ(summary.benchmark->unique_word_count, 3U);
    ASSERT_EQ(summary.benchmark->phases.size(), 4U);
    EXPECT_EQ(summary.benchmark->phases[0].name, "read");
    EXPECT_EQ(summary.benchmark->phases[1].name, "tokenize_count");
    EXPECT_EQ(summary.benchmark->phases[2].name, "canonicalize");
    EXPECT_EQ(summary.benchmark->phases[3].name, "total");

    std::filesystem::remove(input_path);
}

TEST(SequentialRunnerTest, OutputFileModeWritesExpectedContent) {
    const auto input_path = unique_temp_path("wf_sequential_output_input");
    const auto output_path = unique_temp_path("wf_sequential_output_result");
    write_text_file(input_path, "pear apple pear\n");

    wf::run_config config;
    config.selected_method = wf::execution_method::sequential;
    config.input_path = input_path;
    config.output_enabled = true;
    config.output_path = output_path;
    config.benchmark_enabled = true;

    const wf::run_summary summary = wf::run_sequential(config);

    std::ifstream input(output_path, std::ios::binary);
    ASSERT_TRUE(input.is_open());
    std::stringstream buffer;
    buffer << input.rdbuf();
    EXPECT_EQ(buffer.str(), std::string("apple 1\npear 2\n"));
    ASSERT_TRUE(summary.benchmark.has_value());
    ASSERT_EQ(summary.benchmark->phases.size(), 5U);
    EXPECT_EQ(summary.benchmark->phases[2].name, "canonicalize");
    EXPECT_EQ(summary.benchmark->phases[3].name, "write");
    EXPECT_EQ(summary.benchmark->phases[4].name, "total");

    std::filesystem::remove(input_path);
    std::filesystem::remove(output_path);
}
