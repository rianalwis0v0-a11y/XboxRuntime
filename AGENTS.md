# Agent instructions for XboxRuntime

## Mission
Build a correct, testable original-Xbox runtime. Prioritize observable compatibility and measured performance over counting exports or producing large amounts of unverified code.

## Required workflow
1. Read the relevant module, its headers, callers, tests, and current CI workflow before editing.
2. Claim one task from `docs/AGENT_WORKSTREAMS.md`. Work on one task per branch using `agent/<area>/<task-id>`.
3. Before changing shared headers, public interfaces, ordinal tables, or CMake wiring, check for conflicting work and document the interface change in the PR.
4. Keep changes narrowly scoped. Do not edit another agent's owned files without coordination.
5. Add a regression test for every behaviour change. Tests must assert observable results, not merely successful compilation.
6. Run the relevant local tests and report exact commands and outcomes. GitHub Actions must pass before merge.
7. Open a pull request targeting `work/nv2a-d3d11-integration`. Never merge your own PR.

## Engineering rules
- No placeholders, fake success paths, or silent no-op implementations. Unsupported guest APIs and GPU commands must fail explicitly and diagnostically.
- Do not describe an ordinal as supported merely because it has a dispatch route. Correct semantics and tests are required.
- For NV2A, prefer translating commands, state, and shaders to host-GPU operations. Keep CPU work focused on decoding, validation, synchronization, and unavoidable fallback paths. Do not claim GPU acceleration without profiling evidence.
- Xbox vertex microcode translation to HLSL is runtime translation; do not rebuild or rewrite the guest XBE as a substitute.
- Preserve guest ABI details: 32-bit pointers, alignment, calling conventions, register semantics, and guest memory bounds.
- Avoid introducing new heavyweight dependencies without a measured need.
- Never commit ROMs, copyrighted game assets, EEPROM dumps, credentials, API keys, tokens, or private user data.

## Free-service constraint
- Do not configure paid model providers, paid APIs, paid runners, or billing-backed services.
- Use only a provider/model explicitly marked free in the user's account. If free inference or execution capacity is unavailable, stop and report the blocker rather than selecting a paid fallback.
- Never add provider keys or other secrets to the repository, workflow files, logs, or pull requests.

## Build and test
The repository's authoritative CI is `.github/workflows/build-nv2a.yml`. It builds the Windows x64 host modules, runs CTest smoke tests, builds the x86 guest, and runs XBE diagnostics. Read the current workflow before assuming a command or target is still valid.

At minimum, run the narrowest relevant test locally, then verify the full GitHub Actions workflow on the PR. Report failures honestly; do not mark skipped tests as passed.

## Pull request requirements
Every PR must state:
- the task ID and exact behaviour changed;
- files/modules touched and any shared interface changes;
- tests added and exact commands run;
- CI run link and result;
- known limitations and unsupported cases;
- performance measurements when claiming an optimization.

Keep PRs small enough to review. Integration owns cross-module changes and resolves conflicts; tests must pass before merge.
