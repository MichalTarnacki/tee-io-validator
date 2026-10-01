# Fault chunk-driver regressions

[chunk_fault_gtest.cpp](chunk_fault_gtest.cpp) is one of the Google Tests; run all fault-injection suites with
`scripts/run_fault_injection_ut.sh [VALIDATOR_BUILD_DIR]` (default build:
`build-pqc-current`). The script reuses that build's compile flags and static
libraries; it does not configure CMake, rebuild firmware, or access hardware.
Objects and binaries go to a temporary directory. Campaign INI tests in
[integration_gtest.cpp](../host_fault_integration/integration_gtest.cpp) run when
`TEEIO_FAULT_CATALOG_DIR` names the campaign INI directory and are skipped
otherwise.

## What is executed

- The production chunk-driver source (`Fault.1`-`Fault.4`), not a copied selector implementation.
- Real fault-injection code and real libspdm PCI DOE encode/decode.
- Real libspdm CHUNK_SEND responder and error response generator, compiled
  directly from the current sources. A valid control chunk is accepted before
  testing the oversized variant. Both SPDM 1.2 and 1.4 wire layouts are covered.
- Oversized plaintext is 260 bytes for a 256-byte receive limit; encoded DOE
  length is 67 DWORDs. The responder must reject in the first CHUNK_SEND_ACK
  with EARLY_ERROR_DETECTED / embedded InvalidRequest 0x01. Terminal 0x05,
  late 0x01, wrong sequence, and short errors cannot satisfy that predicate.
- Dynamic final-chunk selection for 2 through 9 chunks in both wire versions.
- Partial CERTIFICATE response gating, ignoring unrelated chunked responses.
- Recovery order and failure propagation at each step, using **mocked**
  connection/certificate/session/heartbeat/end-session operations on the same
  allocated context. The original fault result must remain unchanged.
- The certificate-signature driver's real run function with a rejecting positive-control callback: assertion
  fails, no mutation fires, and no certificate-verification provenance is claimed.
- Certificate-signature target parsing and fail-closed mutation checks on the
  libspdm sample `ecp384` responder chain (index 1 CA, ECDSA r low byte).
- Callback and application-context restoration. Strict chunk-transfer INI
  expectations are in `CatalogTest` (integration_gtest.cpp).

## Integration requirements / limits

- No new production translation unit or CMake change is needed. The Fault
  class's setup/run/teardown entry points remain in the main driver. Separate
  key-exchange and FINISH-signature drivers are untouched and can be routed independently.
- The parent campaign must honor failed **driver assertions**, including
  recovery assertions, even when FI fired and its `actual` string matched.
- The certificate-signature positive-control callback remains a responder integration dependency.
  Generic error 0x05 from an always-rejecting callback is not proof of corruption
  detection. The driver now stops and records a failed provenance assertion.
- Chunk abandonment reports `chunk_transfer_abandoned`; missing-final reports
  `final_chunk_withheld`. These mean deliberate local omission, **not** observed
  peer errors. Successful recovery is checked separately by driver assertions.
- Incomplete-transfer recovery uses GET_VERSION via `libspdm_init_connection`
  on the same allocated context, certificate refetch, a fresh authenticated
  session, HEARTBEAT and END_SESSION. No old-session HEARTBEAT is required.
- The reset assertion checks all **local** session slots before starting the new
  session; it does not replay old protected packets to probe peer key retention.
- Host tests do not establish actual hardware sessions or verify hardware
  recovery. Real hardware execution and the certificate-signature positive-control success remain
  outstanding. No allowlist was widened to hide these limitations.
