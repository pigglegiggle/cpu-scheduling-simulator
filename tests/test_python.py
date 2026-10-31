import json
from pathlib import Path
import subprocess
import sys

import pytest
from scheduler.io import compare, load, loads, prepare, run
from scheduler._native import IndexedHeap, Job, simulate

ROOT = Path(__file__).resolve().parents[1]
BASE = {"jobs": [{"id": "a", "release": 0, "execution": 2, "deadline": 2}]}


def test_exact_deadline_and_bridge():
    result = run(BASE)
    assert result["metrics"]["deadline_misses"] == 0
    assert result["jobs"][0]["finish"] == 2
    assert result["timeline"] == [{"start": 0, "end": 2, "job": "a"}]
    assert result["heaps"]["ready"]["inserts"] == 1


@pytest.mark.parametrize("path", sorted((ROOT / "examples").glob("*.json")))
def test_examples_and_policy_repeatability(path):
    document = load(path)
    for policy, first in compare(document).items():
        second = run(document, policy)
        first.pop("engine_wall_ms")
        second.pop("engine_wall_ms")
        assert first == second
        m = first["metrics"]
        assert m["completed"] + m["cancelled"] == len(first["jobs"])
        assert m["busy"] + m["idle"] == m["elapsed"]
        assert sum(j["executed"] for j in first["jobs"]) == m["busy"]


@pytest.mark.parametrize("document", [
    {}, {"job": []}, {"jobs": "bad"}, {"version": True, **BASE}, {"version": 2, **BASE},
    {"jobs": [{"id": "a", "release": 0, "execution": True, "deadline": 2}]},
    {"jobs": [{"id": "a", "release": 0, "execution": 1.5, "deadline": 2}]},
    {"jobs": [{"id": "a", "release": -1, "execution": 2, "deadline": 2}]},
    {"jobs": [{"id": "a", "release": 0, "execution": 0, "deadline": 2}]},
    {"jobs": [*BASE["jobs"], *BASE["jobs"]]},
    {**BASE, "actions": [{"time": 0, "kind": "cancel", "job": "missing"}]},
    {**BASE, "actions": [{"time": 0, "kind": "deadline", "job": "a"}]},
    {**BASE, "actions": [{"time": 0, "kind": "cancel", "job": "a", "value": 2}]},
    {"periodic": [{"id": "t", "execution": 1, "period": 0, "relative_deadline": 1, "count": 2}]},
    {"jobs": [{"id": "has space", "release": 0, "execution": 1, "deadline": 2}]},
])
def test_rejects_bad_workloads(document):
    with pytest.raises((ValueError, TypeError, OverflowError)):
        run(document)


def test_bounded_expansion_and_strict_json():
    task = {"periodic": [{"id": "a", "period": 3, "execution": 1, "relative_deadline": 2, "count": 3}]}
    jobs, _ = prepare(task)
    assert [j["deadline"] for j in jobs] == [2, 5, 8]
    with pytest.raises(ValueError):
        prepare(task, limit=2)
    for text in ['{"jobs": [], "jobs": []}', '{"x": NaN}', 'x' * (2 * 1024 * 1024 + 1)]:
        with pytest.raises(ValueError):
            loads(text)


def test_cancellation_and_past_deadline_update():
    result = run({"jobs": [{"id": "a", "release": 0, "execution": 10, "deadline": 20}],
                  "actions": [{"time": 5, "job": "a", "kind": "deadline", "value": 3},
                              {"time": 6, "job": "a", "kind": "deadline", "value": 20}]})
    assert result["jobs"][0]["missed"]
    assert result["jobs"][0]["tardiness"] == 0
    assert [e["time"] for e in result["trace"] if e["event"] == "deadline_miss"] == [5]


def test_native_validation_and_heap_mutation():
    with pytest.raises(ValueError):
        simulate([Job("a", 0, 0, 1)], [], "edf")
    h = IndexedHeap()
    h.insert(1, 20)
    h.insert(2, 10)
    h.update(1, 3)
    assert h.entries()[0]["id"] == 1
    assert h.erase(1) and h.valid()
    with pytest.raises((KeyError, IndexError)):
        h.update(99, 2)


def test_cli(tmp_path):
    output = tmp_path / "result.json"
    subprocess.run([sys.executable, "-m", "scheduler", "compare", "examples/preemption.json", "--output", str(output)],
                   cwd=ROOT, check=True, capture_output=True)
    assert set(json.loads(output.read_text())) == {"edf", "fixed", "fifo"}
    bad = tmp_path / "bad.json"
    bad.write_text('{"jobs": []}')
    result = subprocess.run([sys.executable, "-m", "scheduler", "run", str(bad)], cwd=ROOT, capture_output=True)
    assert result.returncode == 2 and b"error:" in result.stderr


def test_streamlit_flow():
    from streamlit.testing.v1 import AppTest
    app = AppTest.from_file(str(ROOT / "web/ui.py"), default_timeout=30).run()
    assert not app.exception
    assert app.session_state["simulation_results"]["edf"]["metrics"]["deadline_misses"] == 0
    editor = app.text_area[0]
    editor.set_value('{"jobs": []}')
    next(b for b in app.button if b.label == "Run simulation").click().run()
    assert not app.exception and app.error
    assert "simulation_results" not in app.session_state
    editor = app.text_area[0]
    editor.set_value(json.dumps(BASE))
    next(b for b in app.button if b.label == "Run simulation").click().run()
    assert not app.exception and app.session_state["simulation_results"]["edf"]["jobs"][0]["finish"] == 2
    next(s for s in app.selectbox if s.label == "Operation").select("Update deadline")
    next(n for n in app.number_input if n.label == "Numeric job ID").set_value(10)
    next(n for n in app.number_input if n.label == "Deadline / rank").set_value(3)
    next(b for b in app.button if b.label == "Apply operation").click().run()
    assert not app.exception
    assert app.session_state["heap"].entries()[0]["id"] == 10
