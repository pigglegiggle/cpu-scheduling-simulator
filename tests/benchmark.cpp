#include "das/scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t workload_seed = 20261101;
constexpr std::size_t mutation_count = 1000;
constexpr int repetition_count = 7;
volatile std::uint64_t checksum = 0;

struct Mutation {
    std::size_t id;
    das::Key key;
    bool replace;
};

struct HeapWorkload {
    std::vector<das::Entry> initial_entries;
    std::vector<Mutation> mutations;
};

struct SchedulerMeasurement {
    double wall_ms;
    das::Metrics metrics;
    std::size_t ready_peak;
};

double median(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[static_cast<std::size_t>(std::ceil(0.5 * samples.size())) - 1];
}

HeapWorkload make_heap_workload(std::size_t entry_count) {
    std::mt19937_64 random(workload_seed);
    HeapWorkload workload;
    for (std::size_t id = 0; id < entry_count; ++id) {
        workload.initial_entries.push_back(
            {id, {static_cast<std::int64_t>(random() % 1000000000), id}});
    }

    // Prepare minimum-targeting mutations with an independent ordered-set oracle.
    std::set<std::pair<std::int64_t, std::size_t>> minimum_oracle;
    auto current_entries = workload.initial_entries;
    for (const auto& entry : current_entries) {
        minimum_oracle.insert({entry.key.rank, entry.id});
    }
    for (std::size_t index = 0; index < mutation_count; ++index) {
        const bool target_minimum = index % 4 == 0;
        const auto id = target_minimum ? minimum_oracle.begin()->second : random() % entry_count;
        const auto rank = target_minimum ? static_cast<std::int64_t>(1000000000LL + index)
                                         : static_cast<std::int64_t>(random() % 1000000000);
        workload.mutations.push_back({id, {rank, id}, random() % 3 == 0});
        minimum_oracle.erase({current_entries[id].key.rank, id});
        current_entries[id].key.rank = rank;
        minimum_oracle.insert({rank, id});
    }
    return workload;
}

double measure_indexed_heap(const HeapWorkload& workload, std::vector<std::size_t>& minimum_ids) {
    das::IndexedMinHeap heap(workload.initial_entries.size());
    for (const auto& entry : workload.initial_entries) {
        heap.insert(entry.id, entry.key);
    }

    const auto started = Clock::now();
    for (std::size_t index = 0; index < workload.mutations.size(); ++index) {
        const auto& mutation = workload.mutations[index];
        if (mutation.replace) {
            heap.erase(mutation.id);
            heap.insert(mutation.id, mutation.key);
        } else {
            heap.update(mutation.id, mutation.key);
        }
        minimum_ids[index] = heap.top().id;
    }
    const auto microseconds =
        std::chrono::duration<double, std::micro>(Clock::now() - started).count();
    if (!heap.valid()) {
        throw std::runtime_error("heap invariant failure");
    }
    return microseconds / workload.mutations.size();
}

double measure_linear_scan(const HeapWorkload& workload, std::vector<std::size_t>& minimum_ids) {
    auto entries = workload.initial_entries;
    const auto started = Clock::now();
    for (std::size_t index = 0; index < workload.mutations.size(); ++index) {
        const auto& mutation = workload.mutations[index];
        entries[mutation.id].key = mutation.key;
        const auto minimum = std::min_element(entries.begin(), entries.end(),
                                              [](const auto& left, const auto& right) {
                                                  return das::before(left, right);
                                              });
        minimum_ids[index] = minimum->id;
    }
    return std::chrono::duration<double, std::micro>(Clock::now() - started).count() /
           workload.mutations.size();
}

SchedulerMeasurement measure_scheduler(std::size_t job_count) {
    std::vector<das::Job> jobs;
    std::vector<das::Action> actions;
    for (std::size_t index = 0; index < job_count; ++index) {
        const auto release = static_cast<das::Tick>(index % 100);
        jobs.push_back({"job-" + std::to_string(index), release, 1,
                        release + 100 + static_cast<das::Tick>(index % 1000),
                        static_cast<std::int64_t>(index % 16)});
        if (index % 10 == 0) {
            actions.push_back({50, jobs.back().id, das::ActionKind::Cancel});
        }
    }

    const auto started = Clock::now();
    const auto result = das::simulate(jobs, actions, das::Policy::EDF, false);
    const auto milliseconds =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    if (result.metrics.completed + result.metrics.cancelled != job_count) {
        throw std::runtime_error("scheduler conservation failure");
    }
    return {milliseconds, result.metrics, result.ready_heap.peak};
}

void write_samples(const std::vector<double>& samples) {
    for (std::size_t index = 0; index < samples.size(); ++index) {
        std::cout << (index ? "," : "") << samples[index];
    }
}

void benchmark_size(std::size_t entry_count) {
    const auto workload = make_heap_workload(entry_count);
    std::vector<std::size_t> heap_minimums(workload.mutations.size());
    std::vector<std::size_t> scan_minimums(workload.mutations.size());

    measure_indexed_heap(workload, heap_minimums);
    measure_linear_scan(workload, scan_minimums);
    if (heap_minimums != scan_minimums) {
        throw std::runtime_error("minimum results disagree");
    }

    std::vector<double> heap_times;
    std::vector<double> scan_times;
    for (int repetition = 0; repetition < repetition_count; ++repetition) {
        if (repetition % 2) {
            scan_times.push_back(measure_linear_scan(workload, scan_minimums));
            heap_times.push_back(measure_indexed_heap(workload, heap_minimums));
        } else {
            heap_times.push_back(measure_indexed_heap(workload, heap_minimums));
            scan_times.push_back(measure_linear_scan(workload, scan_minimums));
        }
        if (heap_minimums != scan_minimums) {
            throw std::runtime_error("timed results disagree");
        }
        for (auto minimum_id : heap_minimums) {
            checksum += minimum_id;
        }
    }

    const auto scheduler = measure_scheduler(entry_count);
    std::cout << "{\"jobs\":" << entry_count << ",\"heap_us_per_operation_samples\":[";
    write_samples(heap_times);
    std::cout << "],\"scan_us_per_operation_samples\":[";
    write_samples(scan_times);
    std::cout << "],\"heap_median_us\":" << median(heap_times)
              << ",\"scan_median_us\":" << median(scan_times)
              << ",\"results_verified\":true,\"scheduler_wall_ms\":" << scheduler.wall_ms
              << ",\"completed\":" << scheduler.metrics.completed
              << ",\"cancelled\":" << scheduler.metrics.cancelled
              << ",\"ready_peak\":" << scheduler.ready_peak << "}";
}
} // namespace

int main() {
    try {
        std::cout << "{\"seed\":" << workload_seed << ",\"operations\":" << mutation_count
                  << ",\"repetitions\":" << repetition_count << ",\"sizes\":[";
        bool needs_comma = false;
        for (std::size_t entry_count : {1000, 10000, 100000}) {
            if (needs_comma) {
                std::cout << ',';
            }
            needs_comma = true;
            benchmark_size(entry_count);
        }
        std::cout << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
