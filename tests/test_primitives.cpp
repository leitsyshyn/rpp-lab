#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <wf/contracts.h>
#include <wf/utils.h>

namespace {

std::filesystem::path unique_temp_path(std::string_view prefix) {
    const auto unique_id = ::testing::UnitTest::GetInstance()->random_seed();
    static std::uint64_t counter = 0;
    return std::filesystem::temp_directory_path() /
           (std::string(prefix) + "_" + std::to_string(unique_id) + "_" +
            std::to_string(++counter) + ".txt");
}

} // namespace

TEST(PrimitiveTest, ReadTextFileSupportsEmptyFiles) {
    const auto path = unique_temp_path("wf_empty_input");
    {
        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output.is_open());
    }

    EXPECT_EQ(wf::read_file(path), std::string());

    std::filesystem::remove(path);
}

TEST(PrimitiveTest, ReadTextFileReadsNormalFilesExactly) {
    const auto path = unique_temp_path("wf_small_input");
    {
        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output.is_open());
        output << "Alpha\nBeta 42\n";
        ASSERT_TRUE(output.good());
    }

    EXPECT_EQ(wf::read_file(path), std::string("Alpha\nBeta 42\n"));

    std::filesystem::remove(path);
}

TEST(PrimitiveTest, ReadTextFileThrowsForMissingPath) {
    const auto path = unique_temp_path("wf_missing_input");

    EXPECT_THROW(static_cast<void>(wf::read_file(path)), std::runtime_error);
}

TEST(PrimitiveTest, WriteFrequencyMapProducesDeterministicSortedOutput) {
    const wf::unordered_frequency_map unordered_frequencies{{"banana", 1}, {"apple", 3}, {"2026", 2}};
    const wf::frequency_map frequencies = wf::materialize_frequency_map(unordered_frequencies);

    std::ostringstream output;
    wf::write_frequency_map(output, frequencies);

    EXPECT_EQ(output.str(), std::string("2026 2\napple 3\nbanana 1\n"));
}

TEST(PrimitiveTest, WriteFrequencyMapWritesToFile) {
    const auto path = unique_temp_path("wf_frequency_output");
    const wf::frequency_map frequencies{{"apple", 1}, {"banana", 2}};

    wf::write_frequency_map(path, frequencies);

    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input.is_open());
    std::stringstream buffer;
    buffer << input.rdbuf();
    EXPECT_EQ(buffer.str(), std::string("apple 1\nbanana 2\n"));

    std::filesystem::remove(path);
}

TEST(PrimitiveTest, BenchmarkReportPrintingIsStable) {
    wf::run_result result;
    result.total_word_count = 4;
    result.unique_word_count = 3;
    result.text_size = 17;

    wf::benchmark_data benchmark;
    benchmark.worker_count = 1;
    benchmark.total_duration = 0.5;
    benchmark.phases.push_back({"read", 0.1});
    benchmark.phases.push_back({"count", 0.2});
    result.benchmark = benchmark;

    std::ostringstream output;
    wf::print_benchmark_report(output, wf::method::sequential, result);

    EXPECT_EQ(output.str(), std::string("method: sequential\n"
                                        "worker_count: 1\n"
                                        "text_size: 17\n"
                                        "word_count: 4\n"
                                        "unique_word_count: 3\n"
                                        "total_duration: 0.500000\n"
                                        "phases:\n"
                                        "- read 0.100000\n"
                                        "- count 0.200000\n"));
}
