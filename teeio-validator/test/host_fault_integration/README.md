# Offline requester integration and exit-policy tests

[integration_gtest.cpp](integration_gtest.cpp) is linked with the validator's own
link line and parser objects. Each validator run is a forked child
(`EXPECT_EXIT`). It is one of the Google Tests; run all fault-injection suites with
`scripts/run_fault_injection_ut.sh [VALIDATOR_BUILD_DIR]` (default build:
`build-pqc-current`). The script reuses that build's compile flags and static
libraries; it does not configure CMake, rebuild firmware, or access hardware.
Objects and binaries go to a temporary directory. Campaign INI tests in
[integration_gtest.cpp](../host_fault_integration/integration_gtest.cpp) run when
`TEEIO_FAULT_CATALOG_DIR` names the campaign INI directory and are skipped
otherwise.

[integration_test.c](integration_test.c) includes the production entry point
and result evaluator, replacing only the entry's `run()` call. This prevents
PCI enumeration, device opening, and hardware writes. The standalone catalog
shell test normally attempts group/PCI setup, so it is deliberately not used
against the hardware-capable binary here.

Verified coverage:

- All 34 catalog INIs pass the actual C parser, CLI selection, and registered
  driver validation and reach the execution replacement. This is parsing
  evidence, **not 34 passing protocol scenarios**.
- Twenty result/exit checks: only a single explicit cached-certificate
  control (`Fault.6`) with the primary assertion and all assertions passing qualifies
  for zero-fire success. Skip, missing primary, failed/not-tested assertions,
  failed group teardown, wrong assertion class, missing driver, wrong category,
  multiple control instances/rules and empty runs fail.
- All six other dedicated scenarios remain subject to the fire requirement.
- A real FI fire cannot hide a failed recovery assertion; fire plus a passing
  synthetic dedicated record produces the expected successful entry exit.
- `Fault.N` (N = 1..7) selects driver N; INIs are found by their
  `[Campaign] driver`, and only records of that case count.

Synthetic records are intentional policy unit-test inputs, never emitted as
DUT evidence. Protocol evidence comes separately from the key-exchange and chunk host
harnesses.

The production `run()` now evaluates its saved per-case result tree **after
teardown and before freeing it**, and returns an explicit control boolean.
It does not read the already-cleared `g_current_case_result` or fabricate an
FI outcome for the positive control. Ordinary legacy negative-test oracles
remain unchanged; strict assertion gating applies to dedicated Fault drivers.