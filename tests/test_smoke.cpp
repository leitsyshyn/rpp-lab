#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string_view>
#include <type_traits>

#include <wf/contracts.h>
#include <wf/utils.h>
#include <wf/runners.h>

TEST(SmokeTest, CompilesAndLinks) {
    EXPECT_TRUE(true);
}

TEST(SmokeTest, ExecutionMethodConversions) {
    EXPECT_EQ(wf::to_string(wf::method::sequential), std::string_view("sequential"));
    EXPECT_EQ(wf::to_string(wf::method::openmp), std::string_view("openmp"));
    EXPECT_EQ(wf::to_string(wf::method::mpi), std::string_view("mpi"));

    const auto sequential = wf::parse_execution_method("sequential");
    const auto openmp = wf::parse_execution_method("openmp");
    const auto mpi = wf::parse_execution_method("mpi");
    const auto obsolete = wf::parse_execution_method("sequential_2");
    const auto missing = wf::parse_execution_method("missing");

    ASSERT_TRUE(sequential.has_value());
    ASSERT_TRUE(openmp.has_value());
    ASSERT_TRUE(mpi.has_value());
    EXPECT_EQ(*sequential, wf::method::sequential);
    EXPECT_EQ(*openmp, wf::method::openmp);
    EXPECT_EQ(*mpi, wf::method::mpi);
    EXPECT_FALSE(obsolete.has_value());
    EXPECT_FALSE(missing.has_value());
}

TEST(SmokeTest, ContractTypesAreConstructible) {
    wf::frequency_map frequencies;
    frequencies.emplace("alpha", 2);
    frequencies.emplace("beta", 1);

    wf::run_config config;
    config.selected_method = wf::method::mpi;
    config.input_path = "input.txt";
    config.output_path = "output.txt";
    config.output_enabled = true;
    config.benchmark_enabled = true;
    config.requested_worker_count = 8;

    wf::run_result result;
    result.frequencies = frequencies;
    result.total_word_count = 3;
    result.unique_word_count = 2;
    result.text_size = 1024;

    wf::benchmark_phase phase;
    phase.name = "count";
    phase.duration = 1.25;

    wf::benchmark_data benchmark;
    benchmark.worker_count = 4;
    benchmark.total_duration = 2.5;
    benchmark.phases.push_back(phase);

    result.benchmark = benchmark;

    ASSERT_TRUE(result.frequencies.has_value());
    EXPECT_EQ(result.frequencies->at("alpha"), 2U);
    EXPECT_EQ(result.total_word_count, 3U);
    EXPECT_EQ(result.unique_word_count, 2U);
    EXPECT_EQ(result.text_size, 1024U);
    ASSERT_TRUE(result.benchmark.has_value());
    EXPECT_EQ(result.benchmark->worker_count, 4);
    EXPECT_EQ(result.benchmark->phases.front().duration, 1.25);
}

TEST(SmokeTest, PrimitiveAndRunnerDeclarationsAreVisible) {
    static_assert(std::is_same_v<decltype(&wf::read_file),
                                 std::string (*)(const std::filesystem::path&)>);
    static_assert(
        std::is_same_v<decltype(static_cast<void (*)(std::ostream&, const wf::frequency_map&)>(
                           &wf::write_frequency_map)),
                       void (*)(std::ostream&, const wf::frequency_map&)>);
    static_assert(
        std::is_same_v<
            decltype(static_cast<void (*)(const std::filesystem::path&, const wf::frequency_map&)>(
                &wf::write_frequency_map)),
            void (*)(const std::filesystem::path&, const wf::frequency_map&)>);
    static_assert(
        std::is_same_v<decltype(&wf::print_benchmark_report),
                       void (*)(std::ostream&, wf::method, const wf::run_result&)>);
    static_assert(
        std::is_same_v<decltype(&wf::run_sequential), wf::run_result (*)(const wf::run_config&)>);
    static_assert(
        std::is_same_v<decltype(&wf::run_openmp), wf::run_result (*)(const wf::run_config&)>);
    static_assert(
        std::is_same_v<decltype(&wf::run_mpi), wf::run_result (*)(const wf::run_config&)>);

    SUCCEED();
}
