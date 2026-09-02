# SPDM fault injection

Requester-side fault injection allows `teeio_validator` to emit or consume
deliberately invalid SPDM traffic. It is disabled by default and has no effect
unless `[FaultInjection] enabled=1` selects a named scenario.

The complete scenario catalog is
[`sample_ini/spdm_negative_cases.ini`](sample_ini/spdm_negative_cases.ini).

## Configuration

```ini
[FaultInjection]
enabled=1
scenario=B1_reserved_version

[FaultRule_1]
scenario=B1_reserved_version
expected=error_or_drop_then_recover
direction=send
doe_type=spdm
spdm_code=0x84
occurrence=1
action=set
offset=0x8
pattern=1f
```

`scenario` selects all enabled rules with the same name. At least one matching
rule is required when fault injection is enabled.

Each `[FaultRule_N]` supports:

| Entry | Values | Meaning |
|---|---|---|
| `scenario` | name | Scenario selected by `[FaultInjection]` |
| `expected` | name | Expected outcome included in the JSON result |
| `enabled` | `0` or `1` | Enable this rule; default `1` |
| `direction` | `send`, `receive` | Requester-relative direction |
| `doe_type` | `any`, `doe_discovery`, `spdm`, `secured_spdm`, `plain_spdm`, `plain_secured_spdm` | Wire or plaintext hook |
| `spdm_code` | `any` or byte | SPDM request/response code |
| `occurrence` | positive integer or `all` | Matching message to modify |
| `action` | `set`, `xor`, `truncate`, `truncate_to`, `extend`, `set_declared_length`, `drop`, `duplicate`, `replay`, `reorder`, `abandon` | Mutation or sequence action |
| `offset` | integer | Absolute offset from the start of the selected representation |
| `offset_from_end` | `0` or `1` | Interpret `offset` as a one-based distance from the end; `offset=1` selects the last byte |
| `size` | integer | Bytes removed, target size, or bytes appended |
| `declared_length` | integer | Replacement DOE DWORD length |
| `pattern` | hexadecimal bytes | Bytes used by `set`, `xor`, and `extend` |

Wire offsets include the 8-byte DOE header. Plaintext hooks use a synthetic
8-byte selector header, so the SPDM header is also at offsets `0x8..0xb`.
`plain_secured_spdm` applies before outbound encryption or after inbound
decryption and is required to target plaintext signature or HMAC fields.

`set`, `xor`, `truncate`, `truncate_to`, `extend`, and
`set_declared_length` are atomic: if bounds, capacity, or wire DWORD alignment
validation fails, the original message is left unchanged and the test fails.
Message pointers returned by the fault engine remain valid only until the next
fault-engine call; transport hooks consume or copy them immediately.

## Results

Every fired rule writes a JSON audit record containing the scenario, rule,
direction, action, occurrence, sizes, expected result, and status. At the end of
the run the validator writes one machine-readable summary containing the rule
ID, disposition, match count, expected outcome, and first recorded actual
outcome:

```text
Fault scenario result: {"scenario":"B1_reserved_version","rule_id":5,"disposition":"mutate","match_count":1,"expected":"error_or_drop_then_recover","actual":"spdm_error_0x01","fired":true,"fire_count":1}
```

An enabled scenario that never fires makes the validator return failure. Fault
counters, replay data, held messages, and deferred DOE traffic are reset when a
new requester context starts.

## Scenario groups

- **B1**: truncated and malformed messages, length mismatches, invalid version,
  request code, parameters, and size.
- **B2**: unsupported, empty, duplicate, and conflicting algorithm offers.
- **B3**: SPDM 1.4 to 1.3 downgrade and replay attempts.
- **B4**: requests issued in an invalid connection or session state.
- **B5**: corrupted signature, HMAC, certificate data, and unsupported slot.
- **B6**: out-of-order, missing, oversized, and abandoned chunk sequences.

## Execution prerequisites

The catalog test validates schema, selection, and rule availability. Protocol
execution additionally requires a target and the message flow matched by the
selected rule:

- B1 and B2 run during version, capability, certificate, or algorithm exchange.
- B3 requires SPDM 1.4 negotiation followed by the selected lower-version flow.
- B4 requires the request named by the scenario; secured-session cases require
    an established session.
- B5 requires certificate retrieval or `KEY_EXCHANGE`/`FINISH` as selected.
- B6 requires a certificate or response large enough to negotiate and use
    SPDM chunking.

If the prerequisite flow is absent, the validator reports `fired=false` and
returns failure rather than claiming that the negative scenario ran.

## Host validation

```bash
teeio-validator/scripts/run_fault_injection_ut.sh
teeio-validator/scripts/run_fault_catalog_test.sh \
    teeio-validator/build/bin/teeio_validator
```

The first command tests selectors, every mutation/sequence action, bounds,
alignment, replay ownership, audit output, and state reset. The second command
selects and parses all 34 catalog scenarios; it does not claim a protocol test
result. Actual expected-versus-actual outcomes require the target device and the
matching prerequisite flow.