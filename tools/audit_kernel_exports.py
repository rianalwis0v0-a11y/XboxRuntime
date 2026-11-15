#!/usr/bin/env python3
"""Audit XboxRuntime's native x86 kernel thunk coverage against the export catalogue.

This is deliberately an audit, not a fake compatibility test: a routed thunk is
reported as implemented-at-dispatch, not semantically verified. The guest XBE
diagnostics remain responsible for behavioural PASS/FAIL results.
"""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "tools" / "kernel_exports.json"
SOURCE = ROOT / "guest32" / "kernel_compat.cpp"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="fail if routes are inconsistent or unknown")
    args = parser.parse_args()
    catalogue = json.loads(CATALOG.read_text(encoding="utf-8"))
    exports = catalogue["exports"]
    by_ordinal = {int(item["ordinal"]): item["name"] for item in exports}
    if len(by_ordinal) != len(exports):
        raise SystemExit("ERROR: duplicate ordinal in kernel export catalogue")

    cpp = SOURCE.read_text(encoding="utf-8")
    dispatch = cpp.split('extern "C" __declspec(naked) void KernelDispatch()', 1)[1]
    dispatch = dispatch.split("\n}\n", 1)[0]
    routed = {int(n) for n in re.findall(r"cmp eax, (\d+)", dispatch)}
    data_body = cpp.split("void* DataFor(uint32_t ordinal)", 1)[1].split("\n}", 1)[0]
    data = {int(n) for n in re.findall(r"case (\d+)", data_body)}
    data.update(int(n) for n in re.findall(r"case (\d+): case", data_body))
    labels = set(re.findall(r"je ([a-zA-Z_][a-zA-Z_0-9]*)", dispatch))
    defined_labels = set(re.findall(r"^\s*([a-zA-Z_][a-zA-Z_0-9]*):\s*(?:jmp|push)\b", dispatch, re.M))
    missing_labels = sorted(labels - defined_labels)
    overlap = sorted(routed & data)
    unknown = sorted((routed | data) - set(by_ordinal))
    mapped = routed | data
    missing = sorted(set(by_ordinal) - mapped)

    print(f"Xbox kernel export catalogue: {len(by_ordinal)} entries")
    print(f"Function ordinals routed by KernelDispatch: {len(routed)}")
    print(f"Data ordinals mapped by DataFor: {len(data)}")
    print(f"Mapped export ordinals: {len(mapped)}")
    print(f"Not mapped yet: {len(missing)}")
    if missing:
        print("Unmapped exports:")
        for ordinal in missing:
            print(f"  {ordinal:3d}  {by_ordinal[ordinal]}")
    problems = []
    if unknown:
        problems.append(f"routes for unknown ordinals: {unknown}")
    if overlap:
        problems.append(f"ordinals mapped as both function and data: {overlap}")
    if missing_labels:
        problems.append(f"conditional jumps without handler labels: {missing_labels}")
    if args.check and problems:
        for problem in problems:
            print("ERROR:", problem)
        return 1
    if problems:
        for problem in problems:
            print("WARNING:", problem)
    print("Note: dispatch coverage does not prove API behaviour; only guest tests can earn PASS.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
