#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wf/contracts.h>

namespace wf {

namespace detail {

[[nodiscard]] bool is_ascii_alphanumeric(unsigned char value) noexcept;

[[nodiscard]] char ascii_to_lower(unsigned char value) noexcept;

[[nodiscard]] count_type checked_add(count_type lhs, count_type rhs);

[[nodiscard]] count_type checked_word_count_size(std::size_t word_count,
                                                 std::string_view input_path);

[[nodiscard]] file_size_type checked_input_size_bytes(std::size_t input_size_bytes,
                                                      std::string_view input_path);

} // namespace detail

[[nodiscard]] std::string read_text_file(const std::filesystem::path& input_path);

[[nodiscard]] std::vector<word_type> extract_words(std::string_view text);

[[nodiscard]] frequency_map count_words(const std::vector<word_type>& words);

[[nodiscard]] frequency_map count_word_range(const std::vector<word_type>& words,
                                             std::size_t begin_index, std::size_t end_index);

void merge_frequency_maps(frequency_map& destination, const frequency_map& source);

void write_frequency_map(std::ostream& output, const frequency_map& frequencies);

void write_frequency_map(const std::filesystem::path& output_path,
                         const frequency_map& frequencies);

[[nodiscard]] std::vector<std::byte> serialize_frequency_map(const frequency_map& frequencies);

[[nodiscard]] frequency_map deserialize_frequency_map(std::span<const std::byte> bytes);

[[nodiscard]] std::uint64_t stable_word_hash(std::string_view normalized_word) noexcept;

void print_benchmark_report(std::ostream& output, const benchmark_report& report);

} // namespace wf
