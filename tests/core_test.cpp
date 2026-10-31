#include "das/scheduler.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>

namespace {
std::size_t checks = 0;
void check(bool pass, const char* message) {
    ++checks;
    if (!pass) {
        throw std::runtime_error(message);
    }
}
template <class F> void rejects(F f) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "expected rejection");
}

// Independent tick-by-tick reference: no production heap, event loop or key helper.
std::vector<das::JobResult> reference(const std::vector<das::Job>& jobs,
                                      const std::vector<das::Action>& actions, das::Policy policy) {
    std::vector<das::JobResult> state;
    std::vector<std::size_t> order(jobs.size());
    std::size_t seq = 0;
    for (const auto& j : jobs) {
        state.push_back({j, das::State::Future, j.execution});
    }
    std::int64_t previous = -1;
    for (das::Tick t = 0; t < 10000; ++t) {
        if (previous >= 0 && state[previous].state == das::State::Ready &&
            state[previous].remaining == 0) {
            state[previous].state = das::State::Completed;
            state[previous].finish = t;
        }
        for (const auto& a : actions) {
            if (a.time == t) {
                auto index = std::find_if(jobs.begin(), jobs.end(),
                                          [&](const auto& j) {
                                              return j.id == a.job;
                                          }) -
                             jobs.begin();
                auto& s = state[index];
                if (s.state == das::State::Completed || s.state == das::State::Cancelled) {
                    continue;
                }
                if (a.kind == das::ActionKind::Cancel) {
                    s.state = das::State::Cancelled;
                    s.cancelled_at = t;
                }
                if (a.kind == das::ActionKind::Deadline) {
                    s.job.deadline = a.value;
                }
                if (a.kind == das::ActionKind::Priority) {
                    s.job.priority = a.value;
                }
            }
        }
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (jobs[i].release == t && state[i].state == das::State::Future) {
                state[i].state = das::State::Ready;
                order[i] = seq++;
            }
        }
        for (auto& s : state) {
            if (s.state == das::State::Ready && s.remaining > 0 && s.job.deadline <= t) {
                s.missed = true;
            }
        }
        std::int64_t best = -1;
        for (std::size_t i = 0; i < state.size(); ++i) {
            if (state[i].state == das::State::Ready) {
                auto rank = [&](std::size_t index) {
                    return policy == das::Policy::EDF             ? state[index].job.deadline
                           : policy == das::Policy::FixedPriority ? state[index].job.priority
                                                                  : 0;
                };
                if (best < 0 || rank(i) < rank(best) ||
                    (rank(i) == rank(best) && order[i] < order[best])) {
                    best = i;
                }
            }
        }
        bool future = false;
        for (const auto& j : jobs) {
            if (j.release > t) {
                future = true;
            }
        }
        for (const auto& a : actions) {
            if (a.time > t) {
                future = true;
            }
        }
        if (best < 0 && !future) {
            return state;
        }
        if (best >= 0) {
            auto& s = state[best];
            if (s.first_start < 0) {
                s.first_start = t;
            }
            --s.remaining;
            ++s.executed;
        }
        previous = best;
    }
    throw std::runtime_error("reference exceeded horizon");
}
void heaps() {
    das::IndexedMinHeap heap;
    std::map<std::size_t, das::Key> ref;
    std::mt19937 rng(20261101);
    for (int step = 0; step < 30000; ++step) {
        std::size_t id = rng() % 128;
        das::Key key{static_cast<int>(rng() % 100) - 50, static_cast<std::uint64_t>(rng() % 5)};
        switch (rng() % 4) {
        case 0:
            if (!ref.count(id)) {
                heap.insert(id, key);
                ref[id] = key;
            } else {
                rejects([&] {
                    heap.insert(id, key);
                });
            }
            break;
        case 1:
            if (ref.count(id)) {
                heap.update(id, key);
                ref[id] = key;
            } else {
                rejects([&] {
                    heap.update(id, key);
                });
            }
            break;
        case 2:
            check(heap.erase(id) == static_cast<bool>(ref.erase(id)), "erase agreement");
            break;
        case 3:
            if (!ref.empty()) {
                auto best =
                    std::min_element(ref.begin(), ref.end(), [](const auto& a, const auto& b) {
                        if (a.second.rank != b.second.rank) {
                            return a.second.rank < b.second.rank;
                        }
                        if (a.second.order != b.second.order) {
                            return a.second.order < b.second.order;
                        }
                        return a.first < b.first;
                    });
                check(heap.pop().id == best->first, "pop reference");
                ref.erase(best);
            } else {
                rejects([&] {
                    heap.pop();
                });
            }
            break;
        }
        check(heap.valid(), "heap-map invariant");
        check(heap.size() == ref.size(), "heap size");
        if (!ref.empty()) {
            auto best = std::min_element(ref.begin(), ref.end(), [](const auto& a, const auto& b) {
                if (a.second.rank != b.second.rank) {
                    return a.second.rank < b.second.rank;
                }
                if (a.second.order != b.second.order) {
                    return a.second.order < b.second.order;
                }
                return a.first < b.first;
            });
            check(heap.top().id == best->first && heap.top().key == best->second,
                  "minimum agreement");
        }
    }
    while (!heap.empty()) {
        heap.pop();
    }
    check(heap.valid(), "empty invariant");
    rejects([&] {
        heap.top();
    });
}
void scheduling() {
    using namespace das;
    std::vector<Job> jobs{
        {"background", 0, 8, 20, 5}, {"urgent", 2, 2, 5, 0}, {"normal", 3, 2, 12, 2}};
    auto edf = simulate(jobs, {}, Policy::EDF), fifo = simulate(jobs, {}, Policy::FIFO);
    check(edf.jobs[1].finish == 4 && edf.jobs[0].finish == 12, "preemptive EDF");
    check(edf.metrics.preemptions == 1 && edf.metrics.misses == 0, "preemption metrics");
    check(fifo.jobs[1].finish == 10 && fifo.metrics.misses == 1, "FIFO overload");
    check(simulate({{"a", 0, 3, 3}}, {}, Policy::EDF).metrics.misses == 0,
          "exact deadline succeeds");
    auto cancelled = simulate({{"a", 4, 3, 6}}, {{4, "a", ActionKind::Cancel}}, Policy::EDF);
    check(cancelled.jobs[0].executed == 0 && cancelled.metrics.cancelled == 1, "cancel at release");
    auto past = simulate({{"a", 0, 10, 20}},
                         {{5, "a", ActionKind::Deadline, 3}, {6, "a", ActionKind::Deadline, 20}},
                         Policy::EDF);
    check(past.jobs[0].missed && past.jobs[0].finish == 10,
          "miss remains observed after extension");
    auto gap = simulate({{"a", 1000000000000LL, 1, 1000000000000LL}}, {}, Policy::EDF);
    check(gap.timeline.size() == 2 && gap.jobs[0].finish == 1000000000001LL,
          "event driven long idle");
    rejects([&] {
        simulate({}, {}, Policy::EDF);
    });
    rejects([&] {
        simulate({{"a", 0, 0, 1}}, {}, Policy::EDF);
    });
    rejects([&] {
        simulate({{"a", 0, 1, 1}, {"a", 1, 1, 2}}, {}, Policy::EDF);
    });
    rejects([&] {
        simulate(jobs, {{0, "missing", ActionKind::Cancel}}, Policy::EDF);
    });
    rejects([&] {
        simulate(jobs, {{0, "urgent", ActionKind::Deadline, 1}}, Policy::EDF);
    });
    std::mt19937 rng(20261102);
    for (int trial = 0; trial < 2000; ++trial) {
        std::vector<Job> input;
        std::vector<Action> controls;
        auto count = 1 + rng() % 12;
        for (unsigned i = 0; i < count; ++i) {
            Tick release = rng() % 20;
            input.push_back({"j" + std::to_string(i), release, 1 + rng() % 6,
                             release + static_cast<Tick>(rng() % 20), static_cast<int>(rng() % 4)});
        }
        for (unsigned i = 0; i < rng() % 10; ++i) {
            auto id = rng() % count;
            auto kind = static_cast<ActionKind>(rng() % 3);
            auto value = kind == ActionKind::Deadline
                             ? input[id].release + static_cast<Tick>(rng() % 30)
                             : static_cast<Tick>(rng() % 4);
            controls.push_back({static_cast<Tick>(rng() % 45), input[id].id, kind, value});
        }
        for (auto policy : {Policy::EDF, Policy::FixedPriority, Policy::FIFO}) {
            auto actual = simulate(input, controls, policy);
            auto expected = reference(input, controls, policy);
            Tick busy = 0, last = 0;
            for (const auto& segment : actual.timeline) {
                check(segment.start == last && segment.end > segment.start, "continuous timeline");
                last = segment.end;
                if (segment.job >= 0) {
                    busy += segment.end - segment.start;
                }
            }
            check(busy == actual.metrics.busy &&
                      busy + actual.metrics.idle == actual.metrics.elapsed,
                  "time accounting");
            for (std::size_t i = 0; i < input.size(); ++i) {
                const auto& a = actual.jobs[i];
                const auto& e = expected[i];
                check(a.state == e.state && a.finish == e.finish &&
                          a.first_start == e.first_start && a.executed == e.executed &&
                          a.remaining == e.remaining && a.missed == e.missed &&
                          a.cancelled_at == e.cancelled_at,
                      "differential scheduling");
            }
        }
    }
}
} // namespace
int main() {
    try {
        heaps();
        scheduling();
        std::cout << checks
                  << " C++ checks passed; 6000 differential scenarios; seeds 20261101/20261102\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
