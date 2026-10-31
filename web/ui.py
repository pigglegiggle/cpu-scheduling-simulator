"""A thin Streamlit workbench. Scheduling, mutations and heap state come from C++."""
import hashlib
import json
from pathlib import Path
import sys

import streamlit as st

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scheduler.io import compare, loads, run
from scheduler._native import IndexedHeap

st.set_page_config(page_title="Deadline-Aware Scheduling Simulator", layout="wide")
st.title("Deadline-Aware Scheduling Simulator")
st.caption("C++ backend · indexed min-heaps · deterministic, preemptive single-CPU simulation")
st.info("Times below are virtual workload units. This is a scheduling simulator, not an RTOS or a wall-clock deadline guarantee.")
scheduling, heap_lab, measured = st.tabs(["Scheduling", "Indexed heap", "Measured benchmarks"])

with scheduling:
    examples = {p.stem.replace("_", " ").title(): p for p in sorted((ROOT / "examples").glob("*.json"))}
    labels = list(examples)
    selection = st.selectbox("Example workload", labels, index=labels.index("Preemption"))
    if st.session_state.get("selected_example") != selection:
        st.session_state["workload_text"] = examples[selection].read_text()
        st.session_state["selected_example"] = selection
    uploaded = st.file_uploader("Or upload workload JSON (up to 2 MiB)", type=["json"], max_upload_size=2)
    if uploaded:
        data = uploaded.getvalue()
        digest = hashlib.sha256(data).hexdigest()
        if st.session_state.get("upload_digest") != digest:
            try:
                text = data.decode("utf-8")
                loads(text)
                st.session_state["workload_text"] = text
                st.session_state["upload_digest"] = digest
            except (ValueError, UnicodeError) as error:
                st.error(str(error))
    with st.form("workload"):
        with st.expander("Edit workload JSON"):
            st.text_area("Workload JSON", key="workload_text", height=250)
        mode = st.selectbox("Run policies", ["Compare all", "EDF", "Fixed priority", "FIFO"])
        submitted = st.form_submit_button("Run simulation", type="primary")
    if submitted or "simulation_results" not in st.session_state:
        st.session_state.pop("simulation_results", None)
        try:
            document = loads(st.session_state["workload_text"])
            policies = {"EDF": "edf", "Fixed priority": "fixed", "FIFO": "fifo"}
            results = compare(document, limit=2000) if mode == "Compare all" else {
                policies[mode]: run(document, policies[mode], limit=2000)}
            st.session_state["simulation_results"] = results
            st.session_state["results_digest"] = hashlib.sha256(st.session_state["workload_text"].encode()).hexdigest()
        except (ValueError, TypeError, KeyError, OverflowError, RuntimeError) as error:
            st.error(f"Cannot run workload: {error}")
    results = st.session_state.get("simulation_results")
    if results:
        if hashlib.sha256(st.session_state["workload_text"].encode()).hexdigest() != st.session_state["results_digest"]:
            st.warning("Results below are from the previous run. Run the edited workload to refresh them.")
        st.subheader("Policy comparison")
        st.dataframe([{"policy": p.upper(), "deadline misses": r["metrics"]["deadline_misses"],
                       "completed": r["metrics"]["completed"], "cancelled": r["metrics"]["cancelled"],
                       "preemptions": r["metrics"]["preemptions"], "mean wait": r["metrics"]["mean_wait"],
                       "C++ host runtime (ms)": r["engine_wall_ms"]} for p, r in results.items()], hide_index=True)
        policy = st.radio("Inspect policy", list(results), horizontal=True, format_func=str.upper)
        result = results[policy]
        metrics = result["metrics"]
        cols = st.columns(4)
        cols[0].metric("Deadline misses", metrics["deadline_misses"])
        cols[1].metric("Completed jobs", metrics["completed"])
        cols[2].metric("Preemptions", metrics["preemptions"])
        cols[3].metric("Virtual CPU utilization", f"{metrics['utilization']:.1%}")
        st.code("JSON → periodic releases + control events → indexed ready/deadline heaps → dispatch → timeline + metrics", language=None)
        st.subheader("Execution timeline")
        timeline = result["timeline"]
        if timeline:
            shown = timeline[:300]
            if len(timeline) > len(shown):
                st.caption(f"Showing the first {len(shown)} of {len(timeline)} segments. Download JSON for the complete trace.")
            data = [{**s, "lane": "Idle" if s["job"] is None else s["job"].split("#")[0],
                     "job": s["job"] or "Idle", "duration": s["end"] - s["start"]} for s in shown]
            spec = {"mark": {"type": "bar", "cornerRadius": 1},
                    "encoding": {"x": {"field": "start", "type": "quantitative", "title": f"Virtual time ({result['time_unit']})"},
                                 "x2": {"field": "end"}, "y": {"field": "lane", "type": "nominal", "title": None,
                                                               "axis": {"labelOverlap": False}},
                                 "color": {"field": "lane", "type": "nominal", "legend": None},
                                 "tooltip": [{"field": x} for x in ["job", "start", "end", "duration"]]},
                    "height": max(180, min(600, len({s['lane'] for s in data}) * 40))}
            st.vega_lite_chart(data, spec, width="stretch")
        else:
            st.caption("No CPU execution or idle interval was needed.")
        st.caption("Bars show execution intervals; per-job deadlines and misses are in the table below. No context-switch or I/O cost is modeled.")
        st.subheader("Event replay")
        trace = result["trace"]
        if trace:
            key = f"replay_{policy}_{st.session_state['results_digest']}"
            index = st.slider("Event index", 0, max(1, len(trace) - 1), 0, key=key)
            event = trace[min(index, len(trace) - 1)]
            st.json(event, expanded=True)
            st.caption("ready_count includes the running job. next_job is the current heap root immediately after this event.")
        st.subheader("Job outcomes")
        st.dataframe(result["jobs"], hide_index=True)
        with st.expander("DSA operation counters"):
            st.json(result["heaps"])
        with st.expander("Complete event log"):
            st.dataframe(trace, hide_index=True)
        st.download_button("Download result JSON", json.dumps(result, indent=2),
                           file_name=f"schedule-{policy}.json", mime="application/json")

with heap_lab:
    st.subheader("Indexed min-heap lab")
    st.write("Mutate the actual C++ heap. The ID → array-position map makes arbitrary updates and cancellation efficient.")
    if "heap" not in st.session_state:
        h = IndexedHeap()
        for job_id, rank in [(10, 32), (20, 12), (30, 16), (40, 5), (50, 24), (60, 8)]:
            h.insert(job_id, rank)
        st.session_state["heap"] = h
    h = st.session_state["heap"]
    with st.form("heap_mutation"):
        operation = st.selectbox("Operation", ["Insert", "Update deadline", "Cancel"])
        columns = st.columns(2)
        job_id = columns[0].number_input("Numeric job ID", 0, 1000000, 70, 1)
        rank = columns[1].number_input("Deadline / rank", -1000000, 1000000, 3, 1)
        mutate = st.form_submit_button("Apply operation")
    if mutate:
        try:
            if operation == "Insert":
                if len(h.entries()) >= 100:
                    raise ValueError("Heap lab is limited to 100 entries for readability")
                h.insert(job_id, rank)
            elif operation == "Update deadline":
                h.update(job_id, rank)
            elif not h.erase(job_id):
                raise ValueError("Job ID is not in the heap")
            if not h.valid():
                raise RuntimeError("Heap/position-map invariant failure")
            st.success("Applied. Heap and position-map invariants hold.")
        except (ValueError, IndexError, KeyError) as error:
            st.error(f"Cannot apply operation: {error}")
    if st.button("Reset heap"):
        st.session_state.pop("heap", None)
        st.rerun()
    entries = h.entries()
    if entries:
        dot = ['digraph heap { rankdir=TB; node [shape=box,fontname="Helvetica"];']
        for entry in entries:
            i = entry["index"]
            dot.append(f'n{i} [label="ID {entry["id"]} | deadline {entry["rank"]}\\nindex {i}"];')
            if entry["parent"] is not None:
                dot.append(f'n{entry["parent"]} -> n{i};')
        dot.append("}")
        st.graphviz_chart("\n".join(dot))
    else:
        st.caption("Heap is empty.")
    st.dataframe(entries, hide_index=True)
    st.json(h.stats())
    st.caption("Minimum is the root. Ties use numeric job ID in this lab. Insert/update/erase repair one heap path; hash-map lookup is expected O(1).")

with measured:
    st.subheader("Measured benchmarks")
    raw = ROOT / "docs/benchmarks/latest.json"
    if raw.exists():
        report = json.loads(raw.read_text())
        st.caption(f"Recorded {report['recorded_at']} · {report.get('cpu', report['platform'])}")
        st.dataframe([{"entries": row["jobs"], "heap µs/op": row["heap_median_us"],
                       "scan µs/op": row["scan_median_us"], "scan / heap": row["scan_median_us"] / row["heap_median_us"],
                       "EDF simulation ms": row["scheduler_wall_ms"]} for row in report["sizes"]], hide_index=True)
        st.warning("µs/op is batch-amortized mutation + minimum lookup time, not per-request p50. Full simulation time is host runtime, not virtual deadline latency.")
        st.write("\n".join("- " + item for item in report["methodology"]))
    else:
        st.code("make benchmark", language="bash")
