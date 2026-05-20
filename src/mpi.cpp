#ifdef WF_HAS_MPI
#include <mpi.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wf/runners.h>
#include <wf/shared.h>

namespace wf {

namespace {

constexpr std::size_t k_fnv_offset_basis = 14695981039346656037ULL;
constexpr std::size_t k_fnv_prime = 1099511628211ULL;

struct chunk {
    std::string text;
    range buffer_range;
};

struct entry {
    std::string word;
    std::size_t count{0};
};

struct entry_header {
    std::size_t word_size{0};
    std::size_t count{0};
};

struct send_buffer {
    std::vector<std::byte> bytes;
    std::vector<int> counts;
};

[[nodiscard]] double max_duration(double local_duration, int rank) {
    double reduced_duration = 0.0;
    MPI_Reduce(&local_duration, &reduced_duration, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    return rank == 0 ? reduced_duration : 0.0;
}

void read_file_at_offset(MPI_File file, std::size_t file_offset, std::span<char> buffer) {
    std::size_t bytes_read = 0;
    while (bytes_read < buffer.size()) {
        const int chunk_size =
            std::min(buffer.size() - bytes_read, std::size_t{std::numeric_limits<int>::max()});
        MPI_File_read_at(file, file_offset + bytes_read, buffer.data() + bytes_read, chunk_size,
                         MPI_CHAR, MPI_STATUS_IGNORE);
        bytes_read += chunk_size;
    }
}

[[nodiscard]] chunk read_file_chunk(MPI_File file, range local_range, std::size_t file_size) {
    const std::size_t local_size = local_range.end - local_range.begin;
    if (local_size == 0) {
        return {{}, {0, 0}};
    }

    const std::size_t overlap_size = local_range.begin > 0 ? 1U : 0U;
    const std::size_t read_begin = local_range.begin - overlap_size;
    const std::size_t read_size = local_size + overlap_size;

    std::string buffer(read_size, '\0');
    read_file_at_offset(file, read_begin, std::span<char>(buffer.data(), buffer.size()));

    const range buffer_range{overlap_size, overlap_size + local_size};
    if (!is_ascii_alphanumeric(buffer[buffer_range.end - 1]) || local_range.end >= file_size) {
        return {std::move(buffer), buffer_range};
    }

    constexpr std::size_t extension_block_size = 64U * 1024U;
    std::size_t extension_offset = local_range.end;
    while (extension_offset < file_size) {
        const std::size_t block_size = std::min(file_size - extension_offset, extension_block_size);
        std::string block(block_size, '\0');
        read_file_at_offset(file, extension_offset, std::span<char>(block.data(), block.size()));

        std::size_t word_size = 0;
        while (word_size < block.size() && is_ascii_alphanumeric(block[word_size])) {
            ++word_size;
        }

        buffer.append(block.data(), word_size);
        if (word_size != block.size()) {
            break;
        }
        extension_offset += block_size;
    }

    return {std::move(buffer), buffer_range};
}

[[nodiscard]] std::size_t stable_word_hash(std::string_view word) noexcept {
    std::size_t hash = k_fnv_offset_basis;
    for (const unsigned char value : word) {
        hash ^= value;
        hash *= k_fnv_prime;
    }
    return hash;
}

[[nodiscard]] std::size_t owner(std::string_view word, int process_count) noexcept {
    return stable_word_hash(word) % process_count;
}

[[nodiscard]] std::vector<std::vector<entry>>
bucketize(const unordered_frequency_map& local_frequencies, int process_count) {
    std::vector<std::vector<entry>> buckets(process_count);
    for (const auto& [word, count] : local_frequencies) {
        buckets[owner(word, process_count)].push_back({word, count});
    }
    return buckets;
}

void pack_entry(std::vector<std::byte>& bytes, std::string_view word, std::size_t count) {
    const entry_header header{word.size(), count};
    const auto header_bytes = std::as_bytes(std::span{&header, 1});
    bytes.insert(bytes.end(), header_bytes.begin(), header_bytes.end());

    const auto word_bytes = std::as_bytes(std::span{word.data(), word.size()});
    bytes.insert(bytes.end(), word_bytes.begin(), word_bytes.end());
}

[[nodiscard]] send_buffer pack_buckets(const std::vector<std::vector<entry>>& buckets) {
    send_buffer send;
    send.counts.reserve(buckets.size());

    for (const auto& bucket : buckets) {
        const std::size_t begin = send.bytes.size();
        for (const auto& entry : bucket) {
            pack_entry(send.bytes, entry.word, entry.count);
        }
        send.counts.push_back(send.bytes.size() - begin);
    }

    return send;
}

[[nodiscard]] std::vector<std::byte> pack_frequencies(const unordered_frequency_map& frequencies) {
    std::vector<std::byte> bytes;
    for (const auto& [word, count] : frequencies) {
        pack_entry(bytes, word, count);
    }
    return bytes;
}

[[nodiscard]] std::vector<entry> unpack_entries(std::span<const std::byte> bytes) {
    std::vector<entry> entries;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        entry_header header;
        std::memcpy(&header, bytes.data() + offset, sizeof(header));
        offset += sizeof(header);

        std::string word(header.word_size, '\0');
        std::memcpy(word.data(), bytes.data() + offset, header.word_size);

        entries.push_back({std::move(word), header.count});
        offset += header.word_size;
    }

    return entries;
}

[[nodiscard]] std::vector<int> build_displacements(std::span<const int> counts) {
    std::vector<int> result(counts.size(), 0);
    int offset = 0;
    for (std::size_t index = 0; index < counts.size(); ++index) {
        result[index] = offset;
        offset += counts[index];
    }
    return result;
}

[[nodiscard]] std::size_t sum_counts(std::span<const int> counts) {
    return std::accumulate(counts.begin(), counts.end(), std::size_t{0});
}

[[nodiscard]] std::span<const std::byte> packet(std::span<const std::byte> bytes,
                                                std::span<const int> counts,
                                                std::span<const int> offsets, int index) {
    return bytes.subspan(offsets[index], counts[index]);
}

} // namespace

[[nodiscard]] run_result run_mpi(const run_config& config) {
    int argc = 0;
    char** argv = nullptr;
    MPI_Init(&argc, &argv);

    int rank = 0;
    int process_count = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &process_count);

    try {
        if (config.output_enabled && !config.finalize_enabled) {
            throw std::runtime_error("MPI output requires deterministic finalization");
        }

        const double total_start = MPI_Wtime();

        const double read_start = MPI_Wtime();
        MPI_File file = MPI_FILE_NULL;
        MPI_File_open(MPI_COMM_WORLD, config.input_path.c_str(), MPI_MODE_RDONLY, MPI_INFO_NULL,
                      &file);

        MPI_Offset file_size = 0;
        MPI_File_get_size(file, &file_size);

        const double partition_start = MPI_Wtime();
        const auto ranges = build_even_ranges(file_size, process_count);
        const range local_range = ranges[rank];
        const double partition_end = MPI_Wtime();

        const chunk local_chunk = read_file_chunk(file, local_range, file_size);

        MPI_File_close(&file);
        const double read_end = MPI_Wtime();

        const double count_start = MPI_Wtime();
        unordered_frequency_map local_frequencies;
        for_each_word(local_chunk.text, local_chunk.buffer_range,
                      [&](std::string word) { count_word(local_frequencies, std::move(word)); });
        const double count_end = MPI_Wtime();

        const double bucketize_start = MPI_Wtime();
        const auto buckets = bucketize(local_frequencies, process_count);
        const send_buffer send = pack_buckets(buckets);
        const std::vector<int> send_displacements = build_displacements(send.counts);
        const double bucketize_end = MPI_Wtime();

        const double alltoall_sizes_start = MPI_Wtime();
        std::vector<int> recv_counts(process_count, 0);
        MPI_Alltoall(send.counts.data(), 1, MPI_INT, recv_counts.data(), 1, MPI_INT,
                     MPI_COMM_WORLD);
        const double alltoall_sizes_end = MPI_Wtime();

        const std::vector<int> recv_displacements = build_displacements(recv_counts);
        std::vector<std::byte> recv_bytes(sum_counts(recv_counts));

        const double alltoall_data_start = MPI_Wtime();
        MPI_Alltoallv(send.bytes.empty() ? nullptr : send.bytes.data(), send.counts.data(),
                      send_displacements.data(), MPI_CHAR,
                      recv_bytes.empty() ? nullptr : recv_bytes.data(), recv_counts.data(),
                      recv_displacements.data(), MPI_CHAR, MPI_COMM_WORLD);
        const double alltoall_data_end = MPI_Wtime();

        const double merge_start = MPI_Wtime();
        unordered_frequency_map owned_frequencies;
        for (int source = 0; source < process_count; ++source) {
            const std::vector<entry> owned_entries =
                unpack_entries(packet(recv_bytes, recv_counts, recv_displacements, source));
            for (const auto& entry : owned_entries) {
                owned_frequencies[entry.word] += entry.count;
            }
        }
        const double merge_end = MPI_Wtime();

        const std::vector<std::byte> owned_bytes = pack_frequencies(owned_frequencies);
        const int owned_byte_count = owned_bytes.size();

        const double gather_start = MPI_Wtime();
        std::vector<int> gathered_counts(rank == 0 ? process_count : 0, 0);
        MPI_Gather(&owned_byte_count, 1, MPI_INT,
                   gathered_counts.empty() ? nullptr : gathered_counts.data(), 1, MPI_INT, 0,
                   MPI_COMM_WORLD);

        std::vector<int> gathered_displacements;
        std::vector<std::byte> gathered_bytes;
        if (rank == 0) {
            gathered_displacements = build_displacements(gathered_counts);
            gathered_bytes.resize(sum_counts(gathered_counts));
        }

        MPI_Gatherv(owned_bytes.empty() ? nullptr : owned_bytes.data(), owned_byte_count, MPI_CHAR,
                    gathered_bytes.empty() ? nullptr : gathered_bytes.data(),
                    gathered_counts.empty() ? nullptr : gathered_counts.data(),
                    gathered_displacements.empty() ? nullptr : gathered_displacements.data(),
                    MPI_CHAR, 0, MPI_COMM_WORLD);
        const double gather_end = MPI_Wtime();

        unordered_frequency_map gathered_frequencies;
        std::optional<frequency_map> frequencies;
        std::size_t total_word_count = 0;
        std::size_t unique_word_count = 0;
        double finalize_start = 0.0;
        double finalize_end = 0.0;
        if (rank == 0) {
            finalize_start = MPI_Wtime();
            for (int source = 0; source < process_count; ++source) {
                const std::vector<entry> entries = unpack_entries(
                    packet(gathered_bytes, gathered_counts, gathered_displacements, source));
                for (const auto& entry : entries) {
                    gathered_frequencies.emplace(entry.word, entry.count);
                    total_word_count += entry.count;
                }
            }
            unique_word_count = gathered_frequencies.size();
            if (config.finalize_enabled) {
                frequencies = materialize_frequency_map(std::move(gathered_frequencies));
            }
            finalize_end = MPI_Wtime();
        }

        double write_start = 0.0;
        double write_end = 0.0;
        if (rank == 0 && config.output_enabled) {
            write_start = MPI_Wtime();
            if (config.output_path.has_value()) {
                write_frequency_map(*config.output_path, *frequencies);
            } else {
                write_frequency_map(std::cout, *frequencies);
            }
            write_end = MPI_Wtime();
        }

        const double total_end = MPI_Wtime();

        run_result result;
        result.text_size = file_size;

        if (rank == 0) {
            result.frequencies = std::move(frequencies);
            result.total_word_count = total_word_count;
            result.unique_word_count = unique_word_count;
        }

        if (config.benchmark_enabled) {
            const double total_duration = max_duration(total_end - total_start, rank);
            const double read_duration = max_duration(read_end - read_start, rank);
            const double partition_duration = max_duration(partition_end - partition_start, rank);
            const double count_duration = max_duration(count_end - count_start, rank);
            const double bucketize_duration = max_duration(bucketize_end - bucketize_start, rank);
            const double alltoall_sizes_duration =
                max_duration(alltoall_sizes_end - alltoall_sizes_start, rank);
            const double alltoall_data_duration =
                max_duration(alltoall_data_end - alltoall_data_start, rank);
            const double merge_duration = max_duration(merge_end - merge_start, rank);
            const double gather_duration = max_duration(gather_end - gather_start, rank);

            if (rank == 0) {
                benchmark_data benchmark;
                benchmark.worker_count = process_count;
                benchmark.total_duration = total_duration;
                benchmark.phases.push_back({"read", read_duration});
                benchmark.phases.push_back({"partition", partition_duration});
                benchmark.phases.push_back({"count", count_duration});
                benchmark.phases.push_back({"bucketize", bucketize_duration});
                benchmark.phases.push_back({"alltoall_sizes", alltoall_sizes_duration});
                benchmark.phases.push_back({"alltoall_data", alltoall_data_duration});
                benchmark.phases.push_back({"merge", merge_duration});
                benchmark.phases.push_back({"gather", gather_duration});
                benchmark.phases.push_back({"finalize", finalize_end - finalize_start});
                if (config.output_enabled) {
                    benchmark.phases.push_back({"write", write_end - write_start});
                }
                result.benchmark = std::move(benchmark);
            }
        }

        MPI_Finalize();
        return result;
    } catch (...) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        std::terminate();
    }
}

} // namespace wf
#endif
