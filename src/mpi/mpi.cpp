#ifdef WF_HAS_MPI
#include <mpi.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wf/internal/chunk_ranges.h>
#include <wf/primitives.h>
#include <wf/runners.h>

namespace wf {

namespace {

using local_frequency_map = std::unordered_map<word_type, count_type>;

struct displacement_buffer {
    std::vector<int> displacements;
    std::size_t total_size{0};
};

[[nodiscard]] bool mpi_abort_is_available() noexcept {
    int initialized = 0;
    if (MPI_Initialized(&initialized) != MPI_SUCCESS || initialized == 0) {
        return false;
    }

    int finalized = 0;
    if (MPI_Finalized(&finalized) != MPI_SUCCESS || finalized != 0) {
        return false;
    }

    return true;
}

[[noreturn]] void abort_if_mpi_active() noexcept {
    if (mpi_abort_is_available()) {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    std::terminate();
}

class mpi_runtime {
public:
    mpi_runtime() {
        try {
            int initialized = 0;
            check_mpi(MPI_Initialized(&initialized), "MPI_Initialized");

            if (initialized == 0) {
                int argc = 0;
                char** argv = nullptr;
                check_mpi(MPI_Init(&argc, &argv), "MPI_Init");
                owns_initialization_ = true;
            }

            int finalized = 0;
            check_mpi(MPI_Finalized(&finalized), "MPI_Finalized");
            if (finalized != 0) {
                throw std::runtime_error("MPI has already been finalized");
            }

            check_mpi(MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN),
                      "MPI_Comm_set_errhandler");
            check_mpi(MPI_Comm_rank(MPI_COMM_WORLD, &rank_), "MPI_Comm_rank");
            check_mpi(MPI_Comm_size(MPI_COMM_WORLD, &size_), "MPI_Comm_size");
            if (size_ <= 0) {
                throw std::runtime_error("MPI reported an invalid process count");
            }
        } catch (...) {
            if (mpi_abort_is_available()) {
                abort_if_mpi_active();
            }

            throw;
        }
    }

    mpi_runtime(const mpi_runtime&) = delete;
    mpi_runtime& operator=(const mpi_runtime&) = delete;

    ~mpi_runtime() {
        int finalized = 0;
        if (MPI_Finalized(&finalized) == MPI_SUCCESS && finalized == 0 && owns_initialization_) {
            MPI_Finalize();
        }
    }

    [[nodiscard]] int rank() const noexcept {
        return rank_;
    }
    [[nodiscard]] int size() const noexcept {
        return size_;
    }

    [[noreturn]] void abort_due_to_failure() const noexcept {
        abort_if_mpi_active();
    }

private:
    static void check_mpi(int code, std::string_view context) {
        if (code == MPI_SUCCESS) {
            return;
        }

        char error_buffer[MPI_MAX_ERROR_STRING];
        int error_length = 0;
        MPI_Error_string(code, error_buffer, &error_length);
        throw std::runtime_error(std::string(context) + ": " +
                                 std::string(error_buffer, static_cast<std::size_t>(error_length)));
    }

    bool owns_initialization_{false};
    int rank_{0};
    int size_{1};
};

void check_mpi(int code, std::string_view context) {
    if (code == MPI_SUCCESS) {
        return;
    }

    char error_buffer[MPI_MAX_ERROR_STRING];
    int error_length = 0;
    MPI_Error_string(code, error_buffer, &error_length);
    throw std::runtime_error(std::string(context) + ": " +
                             std::string(error_buffer, static_cast<std::size_t>(error_length)));
}

[[nodiscard]] std::size_t checked_positive_rank_count_to_size_t(int rank_count) {
    if (rank_count <= 0) {
        throw std::runtime_error("MPI reported an invalid process count");
    }

    return static_cast<std::size_t>(rank_count);
}

[[nodiscard]] std::uint64_t checked_positive_rank_count_to_uint64(int rank_count) {
    return static_cast<std::uint64_t>(checked_positive_rank_count_to_size_t(rank_count));
}

[[nodiscard]] std::size_t checked_file_size_to_size_t(file_size_type value,
                                                      std::string_view context) {
    if (value > static_cast<file_size_type>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error(std::string(context) + " exceeds supported in-memory range");
    }

    return static_cast<std::size_t>(value);
}

[[nodiscard]] int checked_size_to_mpi_int(std::size_t value, std::string_view context) {
    if (value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error(std::string(context) + " exceeds MPI int count range");
    }

    return static_cast<int>(value);
}

[[nodiscard]] MPI_Offset checked_file_size_to_mpi_offset(file_size_type value,
                                                         std::string_view context) {
    if (value > static_cast<file_size_type>(std::numeric_limits<MPI_Offset>::max())) {
        throw std::runtime_error(std::string(context) + " exceeds MPI_Offset range");
    }

    return static_cast<MPI_Offset>(value);
}

[[nodiscard]] file_size_type checked_mpi_offset_to_file_size(MPI_Offset value,
                                                             std::string_view context) {
    if (value < 0) {
        throw std::runtime_error(std::string(context) + " reported a negative value");
    }
    if (static_cast<unsigned long long>(value) >
        static_cast<unsigned long long>(std::numeric_limits<file_size_type>::max())) {
        throw std::runtime_error(std::string(context) + " exceeds supported file-size range");
    }

    return static_cast<file_size_type>(value);
}

[[nodiscard]] file_size_type checked_add_file_size(file_size_type lhs, file_size_type rhs,
                                                   std::string_view context) {
    if (rhs > std::numeric_limits<file_size_type>::max() - lhs) {
        throw std::runtime_error(std::string(context) + " overflowed");
    }

    return lhs + rhs;
}

[[nodiscard]] std::size_t checked_add_size_t(std::size_t lhs, std::size_t rhs,
                                             std::string_view context) {
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
        throw std::runtime_error(std::string(context) + " overflowed");
    }

    return lhs + rhs;
}

[[nodiscard]] displacement_buffer build_displacements(const std::vector<int>& counts,
                                                      std::string_view context) {
    displacement_buffer result;
    result.displacements.resize(counts.size(), 0);

    std::size_t running_total = 0;
    for (std::size_t index = 0; index < counts.size(); ++index) {
        if (counts[index] < 0) {
            throw std::runtime_error(std::string(context) + " contains a negative count");
        }

        result.displacements[index] = checked_size_to_mpi_int(running_total, context);
        running_total =
            checked_add_size_t(running_total, static_cast<std::size_t>(counts[index]), context);
    }

    static_cast<void>(checked_size_to_mpi_int(running_total, context));
    result.total_size = running_total;
    return result;
}

[[nodiscard]] double reduce_max_duration(double local_duration, int rank) {
    double reduced_duration = 0.0;
    check_mpi(
        MPI_Reduce(&local_duration, &reduced_duration, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD),
        "MPI_Reduce");
    return rank == 0 ? reduced_duration : 0.0;
}

[[nodiscard]] std::size_t sum_frequency_counts(const frequency_map& frequencies) {
    count_type total_word_count = 0;
    for (const auto& [word, count] : frequencies) {
        static_cast<void>(word);
        total_word_count = detail::checked_add(total_word_count, count);
    }

    return checked_file_size_to_size_t(static_cast<file_size_type>(total_word_count),
                                       "word count total");
}

void read_file_at_offset(MPI_File file, file_size_type file_offset, std::span<char> buffer,
                         std::string_view context) {
    std::size_t bytes_read = 0;
    while (bytes_read < buffer.size()) {
        const std::size_t remaining = buffer.size() - bytes_read;
        const int chunk_size = checked_size_to_mpi_int(
            std::min<std::size_t>(remaining,
                                  static_cast<std::size_t>(std::numeric_limits<int>::max())),
            context);

        MPI_Status status;
        const file_size_type chunk_offset =
            checked_add_file_size(file_offset, static_cast<file_size_type>(bytes_read), context);
        check_mpi(MPI_File_read_at(file, checked_file_size_to_mpi_offset(chunk_offset, context),
                                   buffer.data() + bytes_read, chunk_size, MPI_CHAR, &status),
                  "MPI_File_read_at");

        int actual_count = 0;
        check_mpi(MPI_Get_count(&status, MPI_CHAR, &actual_count), "MPI_Get_count");
        if (actual_count != chunk_size) {
            throw std::runtime_error(std::string(context) +
                                     " did not read the expected byte count");
        }

        bytes_read =
            checked_add_size_t(bytes_read, static_cast<std::size_t>(actual_count), context);
    }
}

[[nodiscard]] std::string read_nominal_and_boundary_bytes(MPI_File file,
                                                          internal::byte_range nominal_range,
                                                          file_size_type file_size) {
    const file_size_type nominal_size = nominal_range.end - nominal_range.begin;
    if (nominal_size == 0) {
        return {};
    }

    std::string buffer(checked_file_size_to_size_t(nominal_size, "MPI nominal read size"), '\0');
    read_file_at_offset(file, nominal_range.begin, std::span<char>(buffer.data(), buffer.size()),
                        "MPI nominal file read");

    const unsigned char last_nominal_byte = static_cast<unsigned char>(buffer[buffer.size() - 1]);
    if (!detail::is_ascii_alphanumeric(last_nominal_byte) || nominal_range.end >= file_size) {
        return buffer;
    }

    constexpr std::size_t extension_block_size = 64U * 1024U;
    file_size_type extension_offset = nominal_range.end;
    while (extension_offset < file_size) {
        const file_size_type remaining = file_size - extension_offset;
        const file_size_type chunk_size_value =
            std::min<file_size_type>(remaining, extension_block_size);
        const std::size_t chunk_size =
            checked_file_size_to_size_t(chunk_size_value, "MPI boundary extension read size");

        std::string extension_block(chunk_size, '\0');
        read_file_at_offset(file, extension_offset,
                            std::span<char>(extension_block.data(), extension_block.size()),
                            "MPI boundary extension read");

        std::size_t alphanumeric_prefix_size = 0;
        while (alphanumeric_prefix_size < extension_block.size() &&
               detail::is_ascii_alphanumeric(
                   static_cast<unsigned char>(extension_block[alphanumeric_prefix_size]))) {
            ++alphanumeric_prefix_size;
        }

        if (alphanumeric_prefix_size > 0) {
            static_cast<void>(checked_add_size_t(buffer.size(), alphanumeric_prefix_size,
                                                 "MPI boundary extension buffer size"));
            buffer.append(extension_block.data(), alphanumeric_prefix_size);
        }
        if (alphanumeric_prefix_size != extension_block.size()) {
            break;
        }

        extension_offset = checked_add_file_size(extension_offset, chunk_size_value,
                                                 "MPI boundary extension offset");
    }

    return buffer;
}

[[nodiscard]] bool starts_inside_word(MPI_File file, internal::byte_range nominal_range) {
    if (nominal_range.begin == 0 || nominal_range.begin == nominal_range.end) {
        return false;
    }

    char previous_byte = '\0';
    read_file_at_offset(file, nominal_range.begin - 1,
                        std::span<char>(&previous_byte, static_cast<std::size_t>(1)),
                        "MPI previous-byte read");
    return detail::is_ascii_alphanumeric(static_cast<unsigned char>(previous_byte));
}

void add_local_word(local_frequency_map& frequencies, count_type& total_word_count,
                    word_type word) {
    const auto [it, inserted] = frequencies.try_emplace(std::move(word), 0);
    it->second = detail::checked_add(it->second, 1);
    total_word_count = detail::checked_add(total_word_count, 1);
    static_cast<void>(inserted);
}

[[nodiscard]] std::vector<std::byte>
flatten_serialized_buckets(const std::vector<std::vector<std::byte>>& serialized_buckets,
                           const std::vector<int>& displacements, std::size_t total_size) {
    std::vector<std::byte> buffer(total_size);
    for (std::size_t index = 0; index < serialized_buckets.size(); ++index) {
        const auto& bucket = serialized_buckets[index];
        if (bucket.empty()) {
            continue;
        }

        std::copy(bucket.begin(), bucket.end(),
                  buffer.begin() + static_cast<std::size_t>(displacements[index]));
    }

    return buffer;
}

[[nodiscard]] run_summary run_mpi_impl(const run_config& config, const mpi_runtime& runtime) {
    if (config.input_path.empty()) {
        throw std::runtime_error("MPI runner requires an input path");
    }

    const std::string input_path_text = config.input_path.string();
    const int rank = runtime.rank();
    const int process_count = runtime.size();
    const std::size_t process_count_size = checked_positive_rank_count_to_size_t(process_count);
    const std::uint64_t process_count_u64 = checked_positive_rank_count_to_uint64(process_count);

    benchmark_report report;
    report.method = execution_method::mpi;
    report.worker_count = process_count_u64;

    const double total_start = MPI_Wtime();

    const double open_size_start = MPI_Wtime();
    MPI_File file = MPI_FILE_NULL;
    check_mpi(MPI_File_open(MPI_COMM_WORLD, input_path_text.c_str(), MPI_MODE_RDONLY, MPI_INFO_NULL,
                            &file),
              "MPI_File_open");
    check_mpi(MPI_File_set_errhandler(file, MPI_ERRORS_RETURN), "MPI_File_set_errhandler");

    MPI_Offset mpi_file_size = 0;
    check_mpi(MPI_File_get_size(file, &mpi_file_size), "MPI_File_get_size");
    const file_size_type file_size =
        checked_mpi_offset_to_file_size(mpi_file_size, "MPI file size");
    const double open_size_end = MPI_Wtime();

    const double range_compute_start = MPI_Wtime();
    const auto ranges = internal::build_even_byte_ranges(file_size, process_count_size);
    const internal::byte_range nominal_range = ranges[static_cast<std::size_t>(rank)];
    const double range_compute_end = MPI_Wtime();

    const double read_start = MPI_Wtime();
    const bool chunk_starts_inside_word = starts_inside_word(file, nominal_range);
    const std::string local_text = read_nominal_and_boundary_bytes(file, nominal_range, file_size);
    const double read_end = MPI_Wtime();

    check_mpi(MPI_File_close(&file), "MPI_File_close");

    const double tokenize_count_start = MPI_Wtime();
    local_frequency_map local_frequencies;
    count_type local_total_word_count = 0;
    internal::for_each_owned_word(local_text, {0, nominal_range.end - nominal_range.begin},
                                  chunk_starts_inside_word, [&](word_type word) {
                                      add_local_word(local_frequencies, local_total_word_count,
                                                     std::move(word));
                                  });
    const double tokenize_count_end = MPI_Wtime();

    const double bucketize_start = MPI_Wtime();
    std::vector<frequency_map> owner_buckets(process_count_size);
    for (const auto& [word, count] : local_frequencies) {
        const std::size_t owner_index =
            static_cast<std::size_t>(stable_word_hash(word) % process_count_u64);
        owner_buckets[owner_index].emplace(word, count);
    }

    std::vector<std::vector<std::byte>> serialized_buckets(process_count_size);
    std::vector<int> send_counts(process_count_size, 0);
    for (std::size_t index = 0; index < process_count_size; ++index) {
        serialized_buckets[index] = serialize_frequency_map(owner_buckets[index]);
        send_counts[index] = checked_size_to_mpi_int(serialized_buckets[index].size(),
                                                     "MPI all-to-all send bucket size");
    }

    const displacement_buffer send_layout =
        build_displacements(send_counts, "MPI all-to-all send displacements");
    std::vector<std::byte> send_buffer = flatten_serialized_buckets(
        serialized_buckets, send_layout.displacements, send_layout.total_size);
    const double bucketize_end = MPI_Wtime();

    const double alltoall_sizes_start = MPI_Wtime();
    std::vector<int> receive_counts(process_count_size, 0);
    check_mpi(MPI_Alltoall(send_counts.data(), 1, MPI_INT, receive_counts.data(), 1, MPI_INT,
                           MPI_COMM_WORLD),
              "MPI_Alltoall");
    const double alltoall_sizes_end = MPI_Wtime();

    const displacement_buffer receive_layout =
        build_displacements(receive_counts, "MPI all-to-all receive displacements");
    std::vector<std::byte> receive_buffer(receive_layout.total_size);

    const double alltoall_data_start = MPI_Wtime();
    check_mpi(MPI_Alltoallv(send_buffer.empty() ? nullptr : send_buffer.data(), send_counts.data(),
                            send_layout.displacements.data(), MPI_CHAR,
                            receive_buffer.empty() ? nullptr : receive_buffer.data(),
                            receive_counts.data(), receive_layout.displacements.data(), MPI_CHAR,
                            MPI_COMM_WORLD),
              "MPI_Alltoallv");
    const double alltoall_data_end = MPI_Wtime();

    const double owner_merge_start = MPI_Wtime();
    frequency_map owned_frequencies;
    for (std::size_t source_index = 0; source_index < process_count_size; ++source_index) {
        if (receive_counts[source_index] == 0) {
            continue;
        }

        const std::size_t offset =
            static_cast<std::size_t>(receive_layout.displacements[source_index]);
        const std::size_t count = static_cast<std::size_t>(receive_counts[source_index]);
        const std::span<const std::byte> segment(receive_buffer.data() + offset, count);
        merge_frequency_maps(owned_frequencies, deserialize_frequency_map(segment));
    }
    const double owner_merge_end = MPI_Wtime();

    std::vector<std::byte> serialized_owned_frequencies =
        serialize_frequency_map(owned_frequencies);
    const int serialized_owned_size =
        checked_size_to_mpi_int(serialized_owned_frequencies.size(), "MPI final gather send size");

    const double final_gather_start = MPI_Wtime();
    std::vector<int> gathered_sizes(rank == 0 ? process_count_size : 0, 0);
    check_mpi(MPI_Gather(&serialized_owned_size, 1, MPI_INT,
                         gathered_sizes.empty() ? nullptr : gathered_sizes.data(), 1, MPI_INT, 0,
                         MPI_COMM_WORLD),
              "MPI_Gather");

    displacement_buffer gathered_layout;
    std::vector<std::byte> gathered_bytes;
    if (rank == 0) {
        gathered_layout = build_displacements(gathered_sizes, "MPI final gather displacements");
        gathered_bytes.resize(gathered_layout.total_size);
    }

    check_mpi(
        MPI_Gatherv(
            serialized_owned_frequencies.empty() ? nullptr : serialized_owned_frequencies.data(),
            serialized_owned_size, MPI_CHAR,
            gathered_bytes.empty() ? nullptr : gathered_bytes.data(),
            gathered_sizes.empty() ? nullptr : gathered_sizes.data(),
            gathered_layout.displacements.empty() ? nullptr : gathered_layout.displacements.data(),
            MPI_CHAR, 0, MPI_COMM_WORLD),
        "MPI_Gatherv");
    const double final_gather_end = MPI_Wtime();

    frequency_map final_frequencies;
    double root_final_merge_duration = 0.0;
    if (rank == 0) {
        const double root_final_merge_start = MPI_Wtime();
        for (std::size_t source_index = 0; source_index < process_count_size; ++source_index) {
            if (gathered_sizes[source_index] == 0) {
                continue;
            }

            const std::size_t offset =
                static_cast<std::size_t>(gathered_layout.displacements[source_index]);
            const std::size_t count = static_cast<std::size_t>(gathered_sizes[source_index]);
            const std::span<const std::byte> segment(gathered_bytes.data() + offset, count);
            merge_frequency_maps(final_frequencies, deserialize_frequency_map(segment));
        }
        const double root_final_merge_end = MPI_Wtime();
        root_final_merge_duration = root_final_merge_end - root_final_merge_start;
    }

    double root_write_duration = 0.0;
    if (rank == 0 && config.output_enabled) {
        const double root_write_start = MPI_Wtime();
        if (config.output_path.has_value()) {
            write_frequency_map(*config.output_path, final_frequencies);
        } else {
            write_frequency_map(std::cout, final_frequencies);
        }
        const double root_write_end = MPI_Wtime();
        root_write_duration = root_write_end - root_write_start;
    }

    const double total_end = MPI_Wtime();

    run_summary summary;
    summary.result.input_size_bytes = file_size;

    if (rank == 0) {
        const count_type total_word_count = detail::checked_word_count_size(
            sum_frequency_counts(final_frequencies), input_path_text);
        summary.result.frequencies = std::move(final_frequencies);
        summary.result.total_word_count = total_word_count;
        summary.result.unique_word_count =
            detail::checked_word_count_size(summary.result.frequencies->size(), input_path_text);
    }

    if (config.benchmark_enabled) {
        if (rank == 0) {
            report.input_size_bytes = file_size;
            report.word_count = summary.result.total_word_count;
            report.unique_word_count = summary.result.unique_word_count;
        }

        const double open_size_duration =
            reduce_max_duration(open_size_end - open_size_start, rank);
        const double range_compute_duration =
            reduce_max_duration(range_compute_end - range_compute_start, rank);
        const double read_duration = reduce_max_duration(read_end - read_start, rank);
        const double tokenize_count_duration =
            reduce_max_duration(tokenize_count_end - tokenize_count_start, rank);
        const double bucketize_duration =
            reduce_max_duration(bucketize_end - bucketize_start, rank);
        const double alltoall_sizes_duration =
            reduce_max_duration(alltoall_sizes_end - alltoall_sizes_start, rank);
        const double alltoall_data_duration =
            reduce_max_duration(alltoall_data_end - alltoall_data_start, rank);
        const double owner_merge_duration =
            reduce_max_duration(owner_merge_end - owner_merge_start, rank);
        const double final_gather_duration =
            reduce_max_duration(final_gather_end - final_gather_start, rank);
        const double total_duration = reduce_max_duration(total_end - total_start, rank);

        if (rank == 0) {
            report.total_seconds = total_duration;
            report.phases.push_back(
                {"mpi_file_open_size", open_size_duration, phase_scope::distributed});
            report.phases.push_back(
                {"range_compute", range_compute_duration, phase_scope::distributed});
            report.phases.push_back({"mpi_file_read", read_duration, phase_scope::distributed});
            report.phases.push_back(
                {"boundary_tokenize_count", tokenize_count_duration, phase_scope::distributed});
            report.phases.push_back(
                {"hash_bucketize", bucketize_duration, phase_scope::distributed});
            report.phases.push_back(
                {"alltoall_sizes", alltoall_sizes_duration, phase_scope::distributed});
            report.phases.push_back(
                {"alltoall_data", alltoall_data_duration, phase_scope::distributed});
            report.phases.push_back(
                {"owner_merge", owner_merge_duration, phase_scope::distributed});
            report.phases.push_back(
                {"final_gather", final_gather_duration, phase_scope::distributed});
            report.phases.push_back(
                {"root_final_merge", root_final_merge_duration, phase_scope::root_only});
            if (config.output_enabled) {
                report.phases.push_back(
                    {"root_write", root_write_duration, phase_scope::root_only});
            }
            report.phases.push_back({"total", report.total_seconds, phase_scope::distributed});
            summary.benchmark = std::move(report);
        }
    }

    return summary;
}

} // namespace

run_summary run_mpi(const run_config& config) {
    mpi_runtime runtime;

    try {
        return run_mpi_impl(config, runtime);
    } catch (...) {
        runtime.abort_due_to_failure();
    }
}

} // namespace wf
#endif
