#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <wf/contracts.h>
#include <wf/primitives.h>

namespace wf::detail {

std::size_t checked_file_size_to_buffer_size(std::uintmax_t file_size, std::string_view input_path);
std::streamsize checked_buffer_size_to_read_size(std::size_t buffer_size,
                                                 std::string_view input_path);

} // namespace wf::detail

namespace {

std::filesystem::path unique_temp_path(std::string_view prefix) {
    const auto unique_id = ::testing::UnitTest::GetInstance()->random_seed();
    static std::uint64_t counter = 0;
    return std::filesystem::temp_directory_path() /
           (std::string(prefix) + "_" + std::to_string(unique_id) + "_" +
            std::to_string(++counter) + ".txt");
}

std::vector<std::byte> bytes_from_string(std::string_view text) {
    std::vector<std::byte> bytes(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        bytes[index] = static_cast<std::byte>(text[index]);
    }

    return bytes;
}

} // namespace

TEST(PrimitiveTest, ReadTextFileSupportsEmptyFiles) {
    const auto path = unique_temp_path("wf_empty_input");
    {
        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output.is_open());
    }

    EXPECT_EQ(wf::read_text_file(path), std::string());

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

    EXPECT_EQ(wf::read_text_file(path), std::string("Alpha\nBeta 42\n"));

    std::filesystem::remove(path);
}

TEST(PrimitiveTest, ReadTextFileThrowsForMissingPath) {
    const auto path = unique_temp_path("wf_missing_input");

    EXPECT_THROW(static_cast<void>(wf::read_text_file(path)), std::runtime_error);
}

TEST(PrimitiveTest, FileSizeConversionGuardRejectsOversizedValues) {
    if (std::numeric_limits<std::uintmax_t>::max() == std::numeric_limits<std::size_t>::max()) {
        GTEST_SKIP() << "this platform cannot represent a file_size value larger than size_t";
    }

    const auto oversized_file_size =
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) + 1U;

    EXPECT_THROW(static_cast<void>(wf::detail::checked_file_size_to_buffer_size(oversized_file_size,
                                                                                "huge-input.txt")),
                 std::runtime_error);
}

TEST(PrimitiveTest, ReadSizeConversionGuardRejectsOversizedValues) {
    const auto oversized_buffer_size = static_cast<std::size_t>(
        static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) + 1U);

    EXPECT_THROW(static_cast<void>(wf::detail::checked_buffer_size_to_read_size(
                     oversized_buffer_size, "huge-input.txt")),
                 std::runtime_error);
}

TEST(PrimitiveTest, ExtractWordsHandlesEmptyInput) {
    EXPECT_TRUE(wf::extract_words("").empty());
}

TEST(PrimitiveTest, ExtractWordsUsesCanonicalAsciiNormalization) {
    const auto words = wf::extract_words("Apple banana apple.\nMPI-2026 mpi");

    const std::vector<wf::word_type> expected{"apple", "banana", "apple", "mpi", "2026", "mpi"};
    EXPECT_EQ(words, expected);
}

TEST(PrimitiveTest, ExtractWordsTreatsPunctuationAsDelimiters) {
    const auto words = wf::extract_words("Alpha,beta! GAMMA\t42...done");

    const std::vector<wf::word_type> expected{"alpha", "beta", "gamma", "42", "done"};
    EXPECT_EQ(words, expected);
}

TEST(PrimitiveTest, CountWordsCountsRepeatedWords) {
    const std::vector<wf::word_type> words{"apple", "banana", "apple", "7", "7", "7"};

    const wf::frequency_map frequencies = wf::count_words(words);

    EXPECT_EQ(frequencies.at("apple"), 2U);
    EXPECT_EQ(frequencies.at("banana"), 1U);
    EXPECT_EQ(frequencies.at("7"), 3U);
}

TEST(PrimitiveTest, CountWordRangeUsesHalfOpenRange) {
    const std::vector<wf::word_type> words{"zero", "one", "one", "two", "two", "two"};

    const wf::frequency_map frequencies = wf::count_word_range(words, 1, 5);

    EXPECT_EQ(frequencies.size(), 2U);
    EXPECT_EQ(frequencies.at("one"), 2U);
    EXPECT_EQ(frequencies.at("two"), 2U);
}

TEST(PrimitiveTest, CountWordRangeRejectsInvalidRanges) {
    const std::vector<wf::word_type> words{"alpha", "beta"};

    EXPECT_THROW(static_cast<void>(wf::count_word_range(words, 2, 1)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(wf::count_word_range(words, 0, 3)), std::out_of_range);
}

TEST(PrimitiveTest, MergeFrequencyMapsAddsCountsWithoutChangingSource) {
    wf::frequency_map destination{{"apple", 2}, {"banana", 1}};
    const wf::frequency_map source{{"apple", 3}, {"cherry", 4}};

    wf::merge_frequency_maps(destination, source);

    EXPECT_EQ(destination.at("apple"), 5U);
    EXPECT_EQ(destination.at("banana"), 1U);
    EXPECT_EQ(destination.at("cherry"), 4U);
    EXPECT_EQ(source.at("apple"), 3U);
    EXPECT_EQ(source.at("cherry"), 4U);
}

TEST(PrimitiveTest, SerializeAndDeserializeFrequencyMapRoundTrip) {
    const wf::frequency_map frequencies{{"2026", 1}, {"apple", 3}, {"banana", 2}};

    const auto bytes = wf::serialize_frequency_map(frequencies);
    const wf::frequency_map restored = wf::deserialize_frequency_map(bytes);

    EXPECT_EQ(restored, frequencies);
}

TEST(PrimitiveTest, DeserializeRejectsMalformedInput) {
    EXPECT_THROW(static_cast<void>(wf::deserialize_frequency_map(bytes_from_string("apple\n"))),
                 std::runtime_error);
    EXPECT_THROW(static_cast<void>(wf::deserialize_frequency_map(bytes_from_string("apple 0\n"))),
                 std::runtime_error);
    EXPECT_THROW(
        static_cast<void>(wf::deserialize_frequency_map(bytes_from_string("apple 1\napple 2\n"))),
        std::runtime_error);
}

TEST(PrimitiveTest, StableWordHashIsDeterministic) {
    EXPECT_EQ(wf::stable_word_hash("apple"), 17819163333647859135ULL);
    EXPECT_EQ(wf::stable_word_hash("apple"), wf::stable_word_hash("apple"));
    EXPECT_NE(wf::stable_word_hash("apple"), wf::stable_word_hash("mpi"));
}

TEST(PrimitiveTest, WriteFrequencyMapProducesDeterministicSortedOutput) {
    const wf::frequency_map frequencies{{"banana", 1}, {"apple", 3}, {"2026", 2}};

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
    wf::benchmark_report report;
    report.method = wf::execution_method::sequential;
    report.worker_count = 1;
    report.input_size_bytes = 17;
    report.word_count = 4;
    report.unique_word_count = 3;
    report.total_seconds = 0.5;
    report.phases.push_back({"read", 0.1, wf::phase_scope::local});
    report.phases.push_back({"count", 0.2, wf::phase_scope::local});

    std::ostringstream output;
    wf::print_benchmark_report(output, report);

    EXPECT_EQ(output.str(), std::string("method: sequential\n"
                                        "worker_count: 1\n"
                                        "input_size_bytes: 17\n"
                                        "word_count: 4\n"
                                        "unique_word_count: 3\n"
                                        "total_seconds: 0.500000\n"
                                        "phases:\n"
                                        "- read 0.100000 local\n"
                                        "- count 0.200000 local\n"));
}
