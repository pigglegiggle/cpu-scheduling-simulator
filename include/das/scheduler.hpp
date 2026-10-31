#pragma once
#include "das/indexed_heap.hpp"
#include <string>

namespace das {
using Tick = std::int64_t;
constexpr Tick max_input_time = 1000000000000LL;
enum class Policy {
    EDF,
    FixedPriority,
    FIFO
};
enum class ActionKind {
    Cancel,
    Deadline,
    Priority
};
enum class State {
    Future,
    Ready,
    Completed,
    Cancelled
};
struct Job {
    std::string id;
    Tick release = 0, execution = 1, deadline = 1;
    std::int64_t priority = 0;
};
struct Action {
    Tick time;
    std::string job;
    ActionKind kind;
    std::int64_t value = 0;
};
struct JobResult {
    Job job;
    State state = State::Future;
    Tick remaining = 0, executed = 0, first_start = -1, finish = -1, cancelled_at = -1;
    bool missed = false;
};
struct Segment {
    Tick start, end;
    std::int64_t job;
};
struct Trace {
    Tick time;
    std::string type;
    std::int64_t job;
    std::size_t ready;
    std::int64_t next;
};
struct Metrics {
    Tick elapsed = 0, busy = 0, idle = 0;
    std::size_t completed = 0, cancelled = 0, misses = 0, preemptions = 0, dispatches = 0,
                deadline_met = 0;
    double utilization = 0, mean_response = 0, mean_wait = 0, mean_turnaround = 0, max_lateness = 0;
};
struct Result {
    Policy policy;
    std::vector<JobResult> jobs;
    std::vector<Segment> timeline;
    std::vector<Trace> trace;
    Metrics metrics;
    HeapStats ready_heap, deadline_heap;
};
Policy parse_policy(const std::string& name);
std::string policy_name(Policy policy);
std::string state_name(State state);
void validate(const std::vector<Job>& jobs, const std::vector<Action>& actions);
Result simulate(const std::vector<Job>& jobs, const std::vector<Action>& actions, Policy policy,
                bool trace = true);
} // namespace das
