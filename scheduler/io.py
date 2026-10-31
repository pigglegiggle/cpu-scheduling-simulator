"""Strict, bounded JSON input; all scheduling and heap work stays in C++."""

import json
from pathlib import Path
from ._native import Action, Job, simulate

MAX_BYTES = 2 * 1024 * 1024


def _pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_nonfinite_number(value):
    raise ValueError(f"Invalid JSON number: {value}")


def loads(text):
    if len(text.encode("utf-8")) > MAX_BYTES:
        raise ValueError("Workload JSON exceeds 2 MiB")
    return json.loads(
        text,
        object_pairs_hook=_pairs,
        parse_constant=_reject_nonfinite_number,
    )


def load(path):
    with Path(path).open("rb") as source:
        data = source.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError("Workload JSON exceeds 2 MiB")
    return loads(data.decode("utf-8"))


def shape(value, allowed, required, where):
    if not isinstance(value, dict):
        raise ValueError(f"{where} must be an object")
    unknown = set(value) - set(allowed)
    missing = set(required) - set(value)
    if unknown or missing:
        raise ValueError(
            f"{where}: unknown fields {sorted(unknown)}, missing fields {sorted(missing)}"
        )


def integer(value, where):
    if type(value) is not int or not -(2**63) <= value < 2**63:
        raise ValueError(f"{where} must be a signed 64-bit integer (not a boolean)")
    return value


def identifier(value):
    if (
        not isinstance(value, str)
        or not 1 <= len(value) <= 80
        or any(not 33 <= ord(c) <= 126 for c in value)
    ):
        raise ValueError("IDs must contain 1..80 printable ASCII characters without spaces")
    return value


def prepare(document, limit=100000):
    shape(
        document, {"version", "name", "time_unit", "jobs", "periodic", "actions"}, set(), "workload"
    )
    if type(document.get("version", 1)) is not int or document.get("version", 1) != 1:
        raise ValueError("Only workload version 1 is supported")
    if not isinstance(document.get("name", ""), str) or len(document.get("name", "")) > 200:
        raise ValueError("name must be a string of at most 200 characters")
    if document.get("time_unit", "ticks") not in {"ticks", "ms", "us"}:
        raise ValueError("time_unit must be ticks, ms, or us; it is a simulation label")
    for field in ("jobs", "periodic", "actions"):
        if not isinstance(document.get(field, []), list):
            raise ValueError(f"{field} must be an array")
    jobs, actions, ids = [], [], set()

    def add(job):
        if len(jobs) >= limit:
            raise ValueError(f"Expanded workload exceeds {limit} jobs")
        shape(
            job,
            {"id", "release", "execution", "deadline", "priority"},
            {"id", "release", "execution", "deadline"},
            "job",
        )
        job = {**job, "priority": job.get("priority", 0)}
        job["id"] = identifier(job["id"])
        if job["id"] in ids:
            raise ValueError(f"Duplicate job ID: {job['id']}")
        ids.add(job["id"])
        for field in ("release", "execution", "deadline", "priority"):
            integer(job[field], field)
        jobs.append(job)

    for job in document.get("jobs", []):
        add(job)
    for task in document.get("periodic", []):
        shape(
            task,
            {"id", "offset", "execution", "period", "relative_deadline", "count", "priority"},
            {"id", "execution", "period", "relative_deadline", "count"},
            "periodic task",
        )
        prefix = identifier(task["id"])
        for field in ("execution", "period", "relative_deadline", "count"):
            integer(task[field], field)
        offset = integer(task.get("offset", 0), "offset")
        if not 1 <= task["count"] <= limit or task["period"] < 1 or task["relative_deadline"] < 0:
            raise ValueError(
                "Periodic count/period must be positive; relative_deadline must be nonnegative"
            )
        if len(jobs) + task["count"] > limit:
            raise ValueError(f"Expanded workload exceeds {limit} jobs")
        for i in range(task["count"]):
            release = offset + i * task["period"]
            add(
                {
                    "id": f"{prefix}#{i}",
                    "release": release,
                    "execution": task["execution"],
                    "deadline": release + task["relative_deadline"],
                    "priority": task.get("priority", 0),
                }
            )
    if len(document.get("actions", [])) > 100000:
        raise ValueError("At most 100000 actions")
    for action in document.get("actions", []):
        shape(action, {"time", "job", "kind", "value"}, {"time", "job", "kind"}, "action")
        kind = action["kind"]
        if kind not in {"cancel", "deadline", "priority"}:
            raise ValueError("Action kind must be cancel, deadline, or priority")
        if kind == "cancel" and "value" in action:
            raise ValueError("Cancel action has no value")
        if kind != "cancel" and "value" not in action:
            raise ValueError("Update action requires value")
        identifier(action["job"])
        if action["job"] not in ids:
            raise ValueError(f"Unknown action job: {action['job']}")
        actions.append(
            {
                "time": integer(action["time"], "time"),
                "job": action["job"],
                "kind": kind,
                "value": integer(action.get("value", 0), "value"),
            }
        )
    return jobs, actions


def run(document, policy="edf", trace=True, limit=100000):
    jobs, actions = prepare(document, limit)
    result = simulate([Job(**j) for j in jobs], [Action(**a) for a in actions], policy, trace)
    return {
        "name": document.get("name", "Custom workload"),
        "time_unit": document.get("time_unit", "ticks"),
        **result,
    }


def compare(document, trace=True, limit=100000):
    return {policy: run(document, policy, trace, limit) for policy in ("edf", "fixed", "fifo")}
