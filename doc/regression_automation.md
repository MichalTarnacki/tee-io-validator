# TDISP regression automation

`teeio_regression.py` parses the non-detailed `teeio_validator` summary format:

```text
TestCase Capabilities.1: fail (pass: 9, fail: 1)
```

It compares every observed case with a versioned JSON baseline, checks known
assertion counts, reports missing and unexpected cases, and writes a structured
JSON result.

## Compare a run

```bash
python3 teeio-validator/tools/teeio_regression.py \
    requester.log \
    teeio-validator/test/regression/tdisp_ecp384_v10.json \
    --output comparison.json
```

Exit status is `0` when the run matches, `1` when a regression is found, and
`2` when the input or baseline is invalid.

The committed v10 baseline contains all 22 evidence-backed verdicts. Issue #53
retained exact assertion counts only for `Capabilities.1` (`9/1`),
`LockInterface.2` (`0/1`), and `StopInterface.3` (`6/0`). Other count fields
are deliberately `null`; they are not guessed and therefore do not participate
in comparison until an archived v10 log is supplied.

## Complete assertion counts from the archived log

```bash
python3 teeio-validator/tools/teeio_regression.py \
    archived-v10-requester.log \
    teeio-validator/test/regression/tdisp_ecp384_v10.json \
    --update complete-v10-baseline.json
```

Review the generated baseline and replace the committed file only after its log
provenance and checksum are recorded.

## Self-test

```bash
teeio-validator/scripts/run_regression_selftest.sh
```

The self-test verifies a matching 22-case fixture, a seeded verdict regression,
and a missing case. The fixture is synthetic and is not hardware evidence.