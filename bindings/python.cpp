#include "das/scheduler.hpp"
#include <chrono>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;
namespace {
py::dict heap_stats(const das::HeapStats& s) {
    py::dict d;
    d["comparisons"] = s.comparisons;
    d["swaps"] = s.swaps;
    d["inserts"] = s.inserts;
    d["updates"] = s.updates;
    d["erases"] = s.erases;
    d["peak_size"] = s.peak;
    return d;
}
py::dict serialize(const das::Result& r, double wall) {
    py::dict d;
    d["policy"] = das::policy_name(r.policy);
    d["engine_wall_ms"] = wall;
    d["virtual_time"] = true;
    auto id = [&](std::int64_t index) -> py::object {
        return index < 0 ? py::none() : py::cast(r.jobs.at(index).job.id);
    };
    const auto& m = r.metrics;
    py::dict metrics;
    metrics["elapsed"] = m.elapsed;
    metrics["busy"] = m.busy;
    metrics["idle"] = m.idle;
    metrics["completed"] = m.completed;
    metrics["cancelled"] = m.cancelled;
    metrics["deadline_misses"] = m.misses;
    metrics["deadline_met"] = m.deadline_met;
    metrics["preemptions"] = m.preemptions;
    metrics["dispatches"] = m.dispatches;
    metrics["utilization"] = m.utilization;
    metrics["mean_response"] = m.mean_response;
    metrics["mean_wait"] = m.mean_wait;
    metrics["mean_turnaround"] = m.mean_turnaround;
    metrics["max_tardiness"] = m.max_lateness;
    d["metrics"] = metrics;
    py::list jobs, timeline, trace;
    for (const auto& j : r.jobs) {
        py::dict item;
        item["id"] = j.job.id;
        item["release"] = j.job.release;
        item["execution"] = j.job.execution;
        item["deadline"] = j.job.deadline;
        item["priority"] = j.job.priority;
        item["state"] = das::state_name(j.state);
        item["executed"] = j.executed;
        item["remaining"] = j.remaining;
        item["missed"] = j.missed;
        item["first_start"] = j.first_start < 0 ? py::none() : py::cast(j.first_start);
        item["finish"] = j.finish < 0 ? py::none() : py::cast(j.finish);
        item["cancelled_at"] = j.cancelled_at < 0 ? py::none() : py::cast(j.cancelled_at);
        item["wait"] =
            j.finish < 0 ? py::none() : py::cast(j.finish - j.job.release - j.job.execution);
        item["tardiness"] =
            j.finish < 0 ? py::none() : py::cast(std::max<das::Tick>(0, j.finish - j.job.deadline));
        jobs.append(item);
    }
    for (const auto& s : r.timeline) {
        py::dict item;
        item["start"] = s.start;
        item["end"] = s.end;
        item["job"] = id(s.job);
        timeline.append(item);
    }
    for (const auto& e : r.trace) {
        py::dict item;
        item["time"] = e.time;
        item["event"] = e.type;
        item["job"] = id(e.job);
        item["ready_count"] = e.ready;
        item["next_job"] = id(e.next);
        trace.append(item);
    }
    d["jobs"] = jobs;
    d["timeline"] = timeline;
    d["trace"] = trace;
    py::dict heaps;
    heaps["ready"] = heap_stats(r.ready_heap);
    heaps["deadlines"] = heap_stats(r.deadline_heap);
    d["heaps"] = heaps;
    return d;
}
} // namespace
PYBIND11_MODULE(_native, module) {
    module.doc() = "Deterministic C++ event-driven scheduling and indexed min-heap.";
    py::class_<das::Job>(module, "Job")
        .def(py::init<std::string, das::Tick, das::Tick, das::Tick, std::int64_t>(), py::arg("id"),
             py::arg("release"), py::arg("execution"), py::arg("deadline"),
             py::arg("priority") = 0);
    py::class_<das::Action>(module, "Action")
        .def(py::init([](das::Tick time, std::string job, std::string kind, std::int64_t value) {
                 das::ActionKind k;
                 if (kind == "cancel") {
                     k = das::ActionKind::Cancel;
                 } else if (kind == "deadline") {
                     k = das::ActionKind::Deadline;
                 } else if (kind == "priority") {
                     k = das::ActionKind::Priority;
                 } else {
                     throw std::invalid_argument("invalid action kind");
                 }
                 return das::Action{time, job, k, value};
             }),
             py::arg("time"), py::arg("job"), py::arg("kind"), py::arg("value") = 0);
    module.def(
        "simulate",
        [](const std::vector<das::Job>& jobs, const std::vector<das::Action>& actions,
           const std::string& policy, bool trace) {
            das::Result r;
            auto start = std::chrono::steady_clock::now();
            {
                py::gil_scoped_release release;
                r = das::simulate(jobs, actions, das::parse_policy(policy), trace);
            }
            auto wall =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            return serialize(r, wall);
        },
        py::arg("jobs"), py::arg("actions"), py::arg("policy") = "edf", py::arg("trace") = true);
    py::class_<das::IndexedMinHeap>(module, "IndexedHeap")
        .def(py::init<>())
        .def("insert",
             [](das::IndexedMinHeap& h, std::size_t id, std::int64_t rank) {
                 h.insert(id, {rank, id});
             })
        .def("update",
             [](das::IndexedMinHeap& h, std::size_t id, std::int64_t rank) {
                 h.update(id, {rank, id});
             })
        .def("erase", &das::IndexedMinHeap::erase)
        .def("valid", &das::IndexedMinHeap::valid)
        .def("entries",
             [](const das::IndexedMinHeap& h) {
                 py::list items;
                 for (std::size_t i = 0; i < h.entries().size(); ++i) {
                     const auto& e = h.entries()[i];
                     py::dict d;
                     d["index"] = i;
                     d["id"] = e.id;
                     d["rank"] = e.key.rank;
                     d["parent"] = i ? py::cast((i - 1) / 2) : py::none();
                     items.append(d);
                 }
                 return items;
             })
        .def("stats", [](const das::IndexedMinHeap& h) {
            return heap_stats(h.stats());
        });
}
