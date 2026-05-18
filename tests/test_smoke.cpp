#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <vector>

#include <wf/contracts.h>
#include <wf/primitives.h>
#include <wf/runners.h>
#include <wf/version.h>

TEST(SmokeTest, VersionDefines) {
    EXPECT_EQ(WF_VERSION_MAJOR, 0);
    EXPECT_EQ(WF_VERSION_MINOR, 1);
    EXPECT_EQ(WF_VERSION_PATCH, 0);
    EXPECT_STREQ(WF_VERSION, "0.1.0");
}

TEST(SmokeTest, VersionLinkage) {
    auto ver = wf::version_string();
    EXPECT_NE(ver, nullptr);
    EXPECT_STREQ(ver, WF_VERSION);
}

TEST(SmokeTest, CompilesAndLinks) {
    EXPECT_TRUE(true);
}

TEST(SmokeTest, ExecutionMethodConversions) {
    EXPECT_EQ(wf::to_string(wf::execution_method::sequential), std::string_view("sequential"));
    EXPECT_EQ(wf::to_string(wf::execution_method::sequential_2),
              std::string_view("sequential_2"));
    EXPECT_EQ(wf::to_string(wf::execution_method::openmp), std::string_view("openmp"));
    EXPECT_EQ(wf::to_string(wf::execution_method::mpi), std::string_view("mpi"));

    const auto sequential = wf::parse_execution_method("sequential");
    const auto sequential_2 = wf::parse_execution_method("sequential_2");
    const auto openmp = wf::parse_execution_method("openmp");
    const auto mpi = wf::parse_execution_method("mpi");
    const auto missing = wf::parse_execution_method("missing");

    ASSERT_TRUE(sequential.has_value());
    ASSERT_TRUE(sequential_2.has_value());
    ASSERT_TRUE(openmp.has_value());
    ASSERT_TRUE(mpi.has_value());
    EXPECT_EQ(*sequential, wf::execution_method::sequential);
    EXPECT_EQ(*sequential_2, wf::execution_method::sequential_2);
    EXPECT_EQ(*openmp, wf::execution_method::openmp);
    EXPECT_EQ(*mpi, wf::execution_method::mpi);
    EXPECT_FALSE(missing.has_value());
}

TEST(SmokeTest, ContractTypesAreConstructible) {
    wf::frequency_map frequencies;
    frequencies.emplace("alpha", 2);
    frequencies.emplace("beta", 1);

    wf::run_config config;
    config.selected_method = wf::execution_method::mpi;
    config.input_path = "input.txt";
    config.output_path = "output.txt";
    config.output_enabled = true;
    config.benchmark_enabled = true;
    config.requested_worker_count = 8;

    wf::word_frequency_result result;
    result.frequencies = frequencies;
    result.total_word_count = 3;
    result.unique_word_count = 2;
    result.input_size_bytes = 1024;

    wf::benchmark_phase phase;
    phase.name = "count";
    phase.duration_seconds = 1.25;
    phase.scope = wf::phase_scope::distributed;

    wf::benchmark_report report;
    report.method = wf::execution_method::openmp;
    report.worker_count = 4;
    report.input_size_bytes = 1024;
    report.word_count = 3;
    report.unique_word_count = 2;
    report.total_seconds = 2.5;
    report.phases.push_back(phase);

    wf::run_summary summary;
    summary.result = result;
    summary.benchmark = report;

    ASSERT_TRUE(summary.result.frequencies.has_value());
    EXPECT_EQ(summary.result.frequencies->at("alpha"), 2U);
    EXPECT_EQ(summary.result.total_word_count, 3U);
    EXPECT_EQ(summary.result.unique_word_count, 2U);
    ASSERT_TRUE(summary.result.input_size_bytes.has_value());
    EXPECT_EQ(*summary.result.input_size_bytes, 1024U);
    ASSERT_TRUE(summary.benchmark.has_value());
    EXPECT_EQ(summary.benchmark->method, wf::execution_method::openmp);
    EXPECT_EQ(summary.benchmark->worker_count, 4U);
    EXPECT_EQ(summary.benchmark->phases.front().scope, wf::phase_scope::distributed);
}

TEST(SmokeTest, PrimitiveAndRunnerDeclarationsAreVisible) {
    static_assert(std::is_same_v<decltype(&wf::read_text_file),
                                 std::string (*)(const std::filesystem::path&)>);
    static_assert(std::is_same_v<decltype(&wf::extract_words),
                                 std::vector<wf::word_type> (*)(std::string_view)>);
    static_assert(std::is_same_v<decltype(&wf::count_words),
                                 wf::frequency_map (*)(const std::vector<wf::word_type>&)>);
    static_assert(std::is_same_v<decltype(&wf::count_word_range),
                                 wf::frequency_map (*)(const std::vector<wf::word_type>&,
                                                       std::size_t, std::size_t)>);
    static_assert(std::is_same_v<decltype(&wf::merge_frequency_maps),
                                 void (*)(wf::frequency_map&, const wf::frequency_map&)>);
    static_assert(
        std::is_same_v<decltype(static_cast<void (*)(std::ostream&, const wf::frequency_map&)>(
                           &wf::write_frequency_map)),
                       void (*)(std::ostream&, const wf::frequency_map&)>);
    static_assert(
        std::is_same_v<
            decltype(static_cast<void (*)(const std::filesystem::path&, const wf::frequency_map&)>(
                &wf::write_frequency_map)),
            void (*)(const std::filesystem::path&, const wf::frequency_map&)>);
    static_assert(std::is_same_v<decltype(&wf::serialize_frequency_map),
                                 std::vector<std::byte> (*)(const wf::frequency_map&)>);
    static_assert(std::is_same_v<decltype(&wf::deserialize_frequency_map),
                                 wf::frequency_map (*)(std::span<const std::byte>)>);
    static_assert(std::is_same_v<decltype(&wf::stable_word_hash),
                                 std::uint64_t (*)(std::string_view) noexcept>);
    static_assert(std::is_same_v<decltype(&wf::print_benchmark_report),
                                 void (*)(std::ostream&, const wf::benchmark_report&)>);
    static_assert(
        std::is_same_v<decltype(&wf::run_sequential), wf::run_summary (*)(const wf::run_config&)>);
    static_assert(
        std::is_same_v<decltype(&wf::run_openmp), wf::run_summary (*)(const wf::run_config&)>);
    static_assert(
        std::is_same_v<decltype(&wf::run_mpi), wf::run_summary (*)(const wf::run_config&)>);

    SUCCEED();
}
