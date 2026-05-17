#include <wf/primitives.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace wf {

namespace detail {

std::size_t checked_file_size_to_buffer_size(std::uintmax_t file_size,
                                             std::string_view input_path) {
    if (file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("input file is too large to fit in memory: " +
                                 std::string(input_path));
    }

    return static_cast<std::size_t>(file_size);
}

std::streamsize checked_buffer_size_to_read_size(std::size_t buffer_size,
                                                 std::string_view input_path) {
    if (static_cast<std::uintmax_t>(buffer_size) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("input file is too large to read on this platform: " +
                                 std::string(input_path));
    }

    return static_cast<std::streamsize>(buffer_size);
}

count_type checked_word_count_size(std::size_t word_count, std::string_view input_path) {
    if (word_count > static_cast<std::size_t>(std::numeric_limits<count_type>::max())) {
        throw std::overflow_error("word count exceeds supported range for input: " +
                                  std::string(input_path));
    }

    return static_cast<count_type>(word_count);
}

file_size_type checked_input_size_bytes(std::size_t input_size_bytes, std::string_view input_path) {
    if (input_size_bytes > static_cast<std::size_t>(std::numeric_limits<file_size_type>::max())) {
        throw std::overflow_error("input size exceeds supported range for input: " +
                                  std::string(input_path));
    }

    return static_cast<file_size_type>(input_size_bytes);
}

bool is_ascii_alphanumeric(unsigned char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

char ascii_to_lower(unsigned char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }

    return static_cast<char>(value);
}

count_type checked_add(count_type lhs, count_type rhs) {
    if (rhs > std::numeric_limits<count_type>::max() - lhs) {
        throw std::overflow_error("word frequency count overflow");
    }

    return lhs + rhs;
}

} // namespace detail

namespace {

constexpr std::uint64_t k_fnv_offset_basis = 14695981039346656037ULL;
constexpr std::uint64_t k_fnv_prime = 1099511628211ULL;

[[nodiscard]] const char* phase_scope_to_string(phase_scope scope) noexcept {
    switch (scope) {
    case phase_scope::local:
        return "local";
    case phase_scope::root_only:
        return "root_only";
    case phase_scope::distributed:
        return "distributed";
    }

    return "unknown";
}

void validate_serialized_word(std::string_view word) {
    if (word.empty()) {
        throw std::runtime_error("failed to deserialize frequency map: empty word");
    }

    for (const unsigned char value : word) {
        if (!detail::is_ascii_alphanumeric(value)) {
            throw std::runtime_error(
                "failed to deserialize frequency map: word contains non-alphanumeric characters");
        }
    }
}

} // namespace

std::string read_text_file(const std::filesystem::path& input_path) {
    const std::string input_path_text = input_path.string();

    std::error_code file_size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(input_path, file_size_error);
    if (file_size_error) {
        throw std::runtime_error("failed to measure input file size: " + input_path_text);
    }

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open input file: " + input_path_text);
    }

    const std::size_t buffer_size =
        detail::checked_file_size_to_buffer_size(file_size, input_path_text);
    std::string contents(buffer_size, '\0');

    if (!contents.empty()) {
        const std::streamsize read_size =
            detail::checked_buffer_size_to_read_size(contents.size(), input_path_text);
        input.read(contents.data(), read_size);
        if (!input) {
            throw std::runtime_error("failed to read input file: " + input_path_text);
        }
    }

    return contents;
}

std::vector<word_type> extract_words(std::string_view text) {
    std::vector<word_type> words;
    word_type current_word;

    for (const unsigned char value : text) {
        if (detail::is_ascii_alphanumeric(value)) {
            current_word.push_back(detail::ascii_to_lower(value));
            continue;
        }

        if (!current_word.empty()) {
            words.push_back(std::move(current_word));
            current_word.clear();
        }
    }

    if (!current_word.empty()) {
        words.push_back(std::move(current_word));
    }

    return words;
}

frequency_map count_words(const std::vector<word_type>& words) {
    return count_word_range(words, 0, words.size());
}

frequency_map count_word_range(const std::vector<word_type>& words, std::size_t begin_index,
                               std::size_t end_index) {
    if (begin_index > end_index) {
        throw std::out_of_range("word range begin index exceeds end index");
    }
    if (end_index > words.size()) {
        throw std::out_of_range("word range end index exceeds word count");
    }

    frequency_map frequencies;
    for (std::size_t index = begin_index; index < end_index; ++index) {
        const auto [it, inserted] = frequencies.try_emplace(words[index], 0);
        it->second = detail::checked_add(it->second, 1);
        static_cast<void>(inserted);
    }

    return frequencies;
}

void merge_frequency_maps(frequency_map& destination, const frequency_map& source) {
    for (const auto& [word, count] : source) {
        const auto [it, inserted] = destination.try_emplace(word, 0);
        it->second = detail::checked_add(it->second, count);
        static_cast<void>(inserted);
    }
}

void write_frequency_map(std::ostream& output, const frequency_map& frequencies) {
    for (const auto& [word, count] : frequencies) {
        output << word << ' ' << count << '\n';
        if (!output) {
            throw std::runtime_error("failed to write frequency map");
        }
    }
}

void write_frequency_map(const std::filesystem::path& output_path,
                         const frequency_map& frequencies) {
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open output file: " + output_path.string());
    }

    write_frequency_map(output, frequencies);
    output.flush();
    if (!output) {
        throw std::runtime_error("failed to write output file: " + output_path.string());
    }
}

std::vector<std::byte> serialize_frequency_map(const frequency_map& frequencies) {
    std::ostringstream buffer;
    write_frequency_map(buffer, frequencies);

    const std::string serialized = buffer.str();
    std::vector<std::byte> bytes(serialized.size());
    for (std::size_t index = 0; index < serialized.size(); ++index) {
        bytes[index] = static_cast<std::byte>(serialized[index]);
    }

    return bytes;
}

frequency_map deserialize_frequency_map(std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return {};
    }

    const auto* raw_bytes = reinterpret_cast<const char*>(bytes.data());
    const std::string_view text(raw_bytes, bytes.size());

    frequency_map frequencies;
    std::size_t line_begin = 0;
    while (line_begin < text.size()) {
        const std::size_t newline_index = text.find('\n', line_begin);
        const std::size_t line_end =
            newline_index == std::string_view::npos ? text.size() : newline_index;
        const std::string_view line = text.substr(line_begin, line_end - line_begin);

        if (line.empty()) {
            throw std::runtime_error("failed to deserialize frequency map: empty line");
        }

        const std::size_t separator_index = line.find(' ');
        if (separator_index == std::string_view::npos || separator_index == 0 ||
            separator_index + 1 >= line.size()) {
            throw std::runtime_error(
                "failed to deserialize frequency map: malformed line separator");
        }
        if (line.find(' ', separator_index + 1) != std::string_view::npos) {
            throw std::runtime_error("failed to deserialize frequency map: malformed count field");
        }

        const std::string_view word = line.substr(0, separator_index);
        const std::string_view count_text = line.substr(separator_index + 1);
        validate_serialized_word(word);

        count_type count = 0;
        const auto* begin = count_text.data();
        const auto* end = count_text.data() + count_text.size();
        const auto [ptr, error_code] = std::from_chars(begin, end, count);
        if (error_code != std::errc{} || ptr != end) {
            throw std::runtime_error("failed to deserialize frequency map: invalid count");
        }
        if (count == 0) {
            throw std::runtime_error("failed to deserialize frequency map: zero count");
        }

        const auto [it, inserted] = frequencies.try_emplace(std::string(word), count);
        if (!inserted) {
            throw std::runtime_error("failed to deserialize frequency map: duplicate word entry");
        }
        static_cast<void>(it);

        if (newline_index == std::string_view::npos) {
            break;
        }

        line_begin = newline_index + 1;
    }

    return frequencies;
}

std::uint64_t stable_word_hash(std::string_view normalized_word) noexcept {
    std::uint64_t hash = k_fnv_offset_basis;
    for (const unsigned char value : normalized_word) {
        hash ^= value;
        hash *= k_fnv_prime;
    }

    return hash;
}

void print_benchmark_report(std::ostream& output, const benchmark_report& report) {
    const auto previous_flags = output.flags();
    const auto previous_precision = output.precision();

    output << std::fixed << std::setprecision(6);
    output << "method: " << to_string(report.method) << '\n';
    output << "worker_count: " << report.worker_count << '\n';
    if (report.input_size_bytes.has_value()) {
        output << "input_size_bytes: " << *report.input_size_bytes << '\n';
    } else {
        output << "input_size_bytes: unknown\n";
    }
    output << "word_count: " << report.word_count << '\n';
    output << "unique_word_count: " << report.unique_word_count << '\n';
    output << "total_seconds: " << report.total_seconds << '\n';
    output << "phases:\n";
    for (const auto& phase : report.phases) {
        output << "- " << phase.name << ' ' << phase.duration_seconds << ' '
               << phase_scope_to_string(phase.scope) << '\n';
    }

    output.flags(previous_flags);
    output.precision(previous_precision);

    if (!output) {
        throw std::runtime_error("failed to write benchmark report");
    }
}

} // namespace wf
