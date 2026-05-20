#include <wf/shared.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace wf {

bool is_ascii_alphanumeric(char value) noexcept {
    const unsigned char byte = static_cast<unsigned char>(value);
    return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= 'a' && byte <= 'z');
}

char ascii_to_lower(char value) noexcept {
    const unsigned char byte = static_cast<unsigned char>(value);
    if (byte >= 'A' && byte <= 'Z') {
        return static_cast<char>(byte - 'A' + 'a');
    }

    return value;
}

std::vector<range> build_even_ranges(std::size_t size, int count) {
    if (count <= 0) {
        throw std::runtime_error("worker count must be positive");
    }

    std::vector<range> ranges(static_cast<std::size_t>(count));
    const std::size_t count_size = static_cast<std::size_t>(count);
    const std::size_t initial_range_size = size / count_size;
    const std::size_t remainder = size % count_size;

    std::size_t begin = 0;
    for (int index = 0; index < count; ++index) {
        const std::size_t extra_byte = static_cast<std::size_t>(index) < remainder ? 1U : 0U;
        const std::size_t range_size = initial_range_size + extra_byte;
        const std::size_t end = begin + range_size;
        ranges[static_cast<std::size_t>(index)] = {begin, end};
        begin = end;
    }

    return ranges;
}

std::string read_file(const std::filesystem::path& input_path) {
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

    if (file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("input file is too large to fit in memory: " + input_path_text);
    }

    const std::size_t buffer_size = static_cast<std::size_t>(file_size);
    std::string contents(buffer_size, '\0');

    if (!contents.empty()) {
        if (static_cast<std::uintmax_t>(contents.size()) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
            throw std::runtime_error("input file is too large to read on this platform: " +
                                     input_path_text);
        }

        const std::streamsize read_size = static_cast<std::streamsize>(contents.size());
        input.read(contents.data(), read_size);
        if (!input) {
            throw std::runtime_error("failed to read input file: " + input_path_text);
        }
    }

    return contents;
}

frequency_map materialize_frequency_map(unordered_frequency_map frequencies) {
    frequency_map result;
    result.reserve(frequencies.size());
    while (!frequencies.empty()) {
        auto node = frequencies.extract(frequencies.begin());
        result.push_back({std::move(node.key()), node.mapped()});
    }

    std::sort(result.begin(), result.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.word < rhs.word; });
    return result;
}

void write_frequency_map(std::ostream& output, const frequency_map& frequencies) {
    for (const auto& entry : frequencies) {
        output << entry.word << ' ' << entry.count << '\n';
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

void print_benchmark_report(std::ostream& output, method selected_method,
                            const run_result& result) {
    if (!result.benchmark.has_value()) {
        throw std::runtime_error("benchmark data is not available");
    }

    const auto& benchmark = *result.benchmark;
    const auto previous_flags = output.flags();
    const auto previous_precision = output.precision();

    output << std::fixed << std::setprecision(6);
    output << "method: " << to_string(selected_method) << '\n';
    output << "worker_count: " << benchmark.worker_count << '\n';
    output << "text_size: " << result.text_size << '\n';
    output << "word_count: " << result.total_word_count << '\n';
    output << "unique_word_count: " << result.unique_word_count << '\n';
    output << "total_duration: " << benchmark.total_duration << '\n';
    output << "phases:\n";
    for (const auto& phase : benchmark.phases) {
        output << "- " << phase.name << ' ' << phase.duration << '\n';
    }

    output.flags(previous_flags);
    output.precision(previous_precision);

    if (!output) {
        throw std::runtime_error("failed to write benchmark report");
    }
}

} // namespace wf
