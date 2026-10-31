#include "das/scheduler.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace das {
Policy parse_policy(const std::string& name) {
    if (name == "edf") {
        return Policy::EDF;
    }
    if (name == "fixed") {
        return Policy::FixedPriority;
    }
    if (name == "fifo") {
        return Policy::FIFO;
    }
    throw std::invalid_argument("policy must be edf, fixed, or fifo");
}
std::string policy_name(Policy policy) {
    if (policy == Policy::EDF) {
        return "edf";
    }
    if (policy == Policy::FixedPriority) {
        return "fixed";
    }
    return "fifo";
}
std::string state_name(State state) {
    switch (state) {
    case State::Future:
        return "future";
    case State::Ready:
        return "ready";
    case State::Completed:
        return "completed";
    case State::Cancelled:
        return "cancelled";
    }
    throw std::logic_error("invalid state");
}
void validate(const std::vector<Job>& jobs, const std::vector<Action>& actions) {
    if (jobs.empty() || jobs.size() > 100000) {
        throw std::invalid_argument("jobs must contain 1..100000 entries");
    }
    if (actions.size() > 100000) {
        throw std::invalid_argument("at most 100000 actions");
    }
    std::unordered_map<std::string, std::size_t> job_ids;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const auto& job = jobs[i];
        if (job.id.empty() || job.id.size() > 80 ||
            std::any_of(job.id.begin(), job.id.end(), [](unsigned char c) {
                return c < 33 || c > 126;
            })) {
            throw std::invalid_argument(
                "job IDs must be 1..80 printable ASCII characters without spaces");
        }
        if (!job_ids.emplace(job.id, i).second) {
            throw std::invalid_argument("duplicate job ID: " + job.id);
        }
        if (job.release < 0 || job.release > max_input_time || job.execution < 1 ||
            job.execution > 1000000000LL || job.deadline < job.release ||
            job.deadline > max_input_time || job.priority < 0 || job.priority > 1000000) {
            throw std::invalid_argument("invalid time, execution, or priority for job: " + job.id);
        }
    }
    for (const auto& action : actions) {
        auto found = job_ids.find(action.job);
        if (found == job_ids.end()) {
            throw std::invalid_argument("action references unknown job: " + action.job);
        }
        if (action.time < 0 || action.time > max_input_time) {
            throw std::invalid_argument("action time out of range");
        }
        if (action.kind == ActionKind::Deadline &&
            (action.value < jobs[found->second].release || action.value > max_input_time)) {
            throw std::invalid_argument(
                "updated deadline must be >= release and within time limit");
        }
        if (action.kind == ActionKind::Priority && (action.value < 0 || action.value > 1000000)) {
            throw std::invalid_argument("priority out of range");
        }
    }
}
Result simulate(const std::vector<Job>& jobs, const std::vector<Action>& actions, Policy policy,
                bool with_trace) {
    validate(jobs, actions);
    Result result;
    result.policy = policy;
    std::unordered_map<std::string, std::size_t> job_ids;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        result.jobs.push_back({jobs[i], State::Future, jobs[i].execution});
        job_ids.emplace(jobs[i].id, i);
    }
    std::vector<std::size_t> release_order(jobs.size()), action_order(actions.size());
    std::iota(release_order.begin(), release_order.end(), 0);
    std::iota(action_order.begin(), action_order.end(), 0);
    std::stable_sort(release_order.begin(), release_order.end(), [&](auto left, auto right) {
        return jobs[left].release < jobs[right].release;
    });
    std::stable_sort(action_order.begin(), action_order.end(), [&](auto left, auto right) {
        return actions[left].time < actions[right].time;
    });
    IndexedMinHeap ready_jobs(jobs.size()), pending_deadlines(jobs.size());
    std::vector<std::uint64_t> enqueue_order(jobs.size());
    std::uint64_t sequence = 0;
    auto ready_key = [&](std::size_t job_index) {
        const auto& job = result.jobs[job_index].job;
        std::int64_t rank = 0;
        if (policy == Policy::EDF) {
            rank = job.deadline;
        } else if (policy == Policy::FixedPriority) {
            rank = job.priority;
        }
        return Key{rank, enqueue_order[job_index]};
    };
    auto emit = [&](Tick t, const std::string& type, std::int64_t job) {
        if (!with_trace) {
            return;
        }
        if (result.trace.size() >= 1000000) {
            throw std::runtime_error("trace limit exceeded");
        }
        result.trace.push_back(
            {t, type, job, ready_jobs.size(),
             ready_jobs.empty() ? -1 : static_cast<std::int64_t>(ready_jobs.top().id)});
    };
    std::size_t next_release = 0, next_action = 0;
    Tick current_time = 0;
    std::int64_t previous_job = -1;
    const Tick infinity = std::numeric_limits<Tick>::max();
    while (next_release < release_order.size() || next_action < action_order.size() ||
           !ready_jobs.empty()) {
        // Completion at a deadline succeeds before controls, releases, deadline checks and
        // dispatch.
        if (previous_job >= 0 && result.jobs[previous_job].state == State::Ready &&
            result.jobs[previous_job].remaining == 0) {
            auto& job_state = result.jobs[previous_job];
            job_state.state = State::Completed;
            job_state.finish = current_time;
            ready_jobs.erase(previous_job);
            pending_deadlines.erase(previous_job);
            emit(current_time, "complete", previous_job);
        }
        while (next_action < action_order.size() &&
               actions[action_order[next_action]].time == current_time) {
            const auto& action = actions[action_order[next_action++]];
            auto id = job_ids.at(action.job);
            auto& job_state = result.jobs[id];
            if (job_state.state == State::Completed || job_state.state == State::Cancelled) {
                emit(current_time, "ignored_action", id);
                continue;
            }
            if (action.kind == ActionKind::Cancel) {
                ready_jobs.erase(id);
                pending_deadlines.erase(id);
                job_state.state = State::Cancelled;
                job_state.cancelled_at = current_time;
                emit(current_time, "cancel", id);
            } else if (action.kind == ActionKind::Deadline) {
                job_state.job.deadline = action.value;
                if (ready_jobs.contains(id)) {
                    ready_jobs.update(id, ready_key(id));
                }
                if (pending_deadlines.contains(id)) {
                    pending_deadlines.update(id, {action.value, enqueue_order[id]});
                }
                emit(current_time, "deadline_update", id);
            } else {
                job_state.job.priority = action.value;
                if (ready_jobs.contains(id)) {
                    ready_jobs.update(id, ready_key(id));
                }
                emit(current_time, "priority_update", id);
            }
        }
        while (next_release < release_order.size() &&
               jobs[release_order[next_release]].release == current_time) {
            auto id = release_order[next_release++];
            auto& job_state = result.jobs[id];
            if (job_state.state == State::Cancelled) {
                continue;
            }
            enqueue_order[id] = sequence++;
            job_state.state = State::Ready;
            ready_jobs.insert(id, ready_key(id));
            pending_deadlines.insert(id, {job_state.job.deadline, enqueue_order[id]});
            emit(current_time, "release", id);
        }
        while (!pending_deadlines.empty() && pending_deadlines.top().key.rank <= current_time) {
            auto id = pending_deadlines.pop().id;
            result.jobs[id].missed = true;
            emit(current_time, "deadline_miss", id);
        }
        auto selected_job =
            ready_jobs.empty() ? -1 : static_cast<std::int64_t>(ready_jobs.top().id);
        if (selected_job != previous_job && previous_job >= 0 &&
            result.jobs[previous_job].state == State::Ready &&
            result.jobs[previous_job].remaining > 0) {
            ++result.metrics.preemptions;
            emit(current_time, "preempt", previous_job);
        }
        if (selected_job >= 0 && selected_job != previous_job) {
            ++result.metrics.dispatches;
            emit(current_time, "dispatch", selected_job);
        }
        Tick next_time = infinity;
        if (next_release < release_order.size()) {
            next_time = std::min(next_time, jobs[release_order[next_release]].release);
        }
        if (next_action < action_order.size()) {
            next_time = std::min(next_time, actions[action_order[next_action]].time);
        }
        if (!pending_deadlines.empty()) {
            next_time = std::min(next_time, pending_deadlines.top().key.rank);
        }
        if (selected_job >= 0) {
            next_time = std::min(next_time, current_time + result.jobs[selected_job].remaining);
        }
        if (next_time == infinity) {
            break;
        }
        if (next_time <= current_time) {
            throw std::logic_error("simulation failed to advance");
        }
        if (selected_job >= 0) {
            auto& job_state = result.jobs[selected_job];
            if (job_state.first_start < 0) {
                job_state.first_start = current_time;
            }
            job_state.remaining -= next_time - current_time;
            job_state.executed += next_time - current_time;
            result.metrics.busy += next_time - current_time;
        }
        if (!result.timeline.empty() && result.timeline.back().job == selected_job &&
            result.timeline.back().end == current_time) {
            result.timeline.back().end = next_time;
        } else {
            result.timeline.push_back({current_time, next_time, selected_job});
        }
        previous_job = selected_job;
        current_time = next_time;
    }
    auto& metrics = result.metrics;
    metrics.elapsed = current_time;
    metrics.idle = current_time - metrics.busy;
    metrics.utilization = current_time ? static_cast<double>(metrics.busy) / current_time : 0;
    std::size_t started_jobs = 0;
    for (const auto& job_state : result.jobs) {
        if (job_state.missed) {
            ++metrics.misses;
        }
        if (job_state.first_start >= 0) {
            ++started_jobs;
            metrics.mean_response += job_state.first_start - job_state.job.release;
        }
        if (job_state.state == State::Cancelled) {
            ++metrics.cancelled;
        }
        if (job_state.state == State::Completed) {
            ++metrics.completed;
            if (!job_state.missed) {
                ++metrics.deadline_met;
            }
            metrics.mean_turnaround += job_state.finish - job_state.job.release;
            metrics.mean_wait += job_state.finish - job_state.job.release - job_state.job.execution;
            metrics.max_lateness = std::max(
                metrics.max_lateness,
                static_cast<double>(std::max<Tick>(0, job_state.finish - job_state.job.deadline)));
        }
    }
    if (started_jobs) {
        metrics.mean_response /= started_jobs;
    }
    if (metrics.completed) {
        metrics.mean_wait /= metrics.completed;
        metrics.mean_turnaround /= metrics.completed;
    }
    result.ready_heap = ready_jobs.stats();
    result.deadline_heap = pending_deadlines.stats();
    return result;
}
} // namespace das
