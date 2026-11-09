# Phase 2 test matrix

Every row is executed by the deterministic firmware suite in `sn_app` unless
marked as a shell regression. QEMU captures the structured `SN2|...` log lines.
`sim_ms` is a logical step counter, not a hardware timing measurement.

## Protocol

| ID | Scenario | Expected result |
| --- | --- | --- |
| `protocol.tx.vector` | Encode canonical 1-in/2-out test transaction | Exact byte vector and txid match independent Python reference |
| `protocol.tx.signing_digest` | Hash unsigned transaction with domain separation | Exact digest matches reference |
| `protocol.block.vector` | Encode canonical block with one transaction | Exact header bytes, tx root, and block hash match reference |
| `protocol.malformed.truncated` | Decode truncated transaction | Rejected with length error |
| `protocol.malformed.version` | Decode unsupported version | Rejected |
| `protocol.malformed.counts` | Zero/oversized input or output counts | Rejected |
| `protocol.malformed.trailing` | Decode with trailing bytes | Rejected |
| `protocol.malformed.frame_magic` | Decode frame with wrong magic | Rejected |
| `protocol.malformed.frame_length` | Decode frame with truncated/oversized payload | Rejected |

## Crypto

| ID | Scenario | Expected result |
| --- | --- | --- |
| `crypto.sig.accept` | Sign and verify a known digest with a test key | Accepted |
| `crypto.sig.reject_tamper` | Verify after changing one digest byte | Rejected |
| `crypto.sig.reject_key` | Verify with a different public key | Rejected |
| `crypto.sig.reject_malformed` | All-zero/out-of-range signature | Rejected |
| `crypto.ds.disabled` | Initialize the DS adapter boundary | `ESP_ERR_NOT_SUPPORTED`; no eFuse access |

## Ledger

| ID | Scenario | Expected result |
| --- | --- | --- |
| `ledger.genesis` | Register deterministic genesis allocations | Balances and state root match |
| `ledger.spend.valid` | Spend one UTXO to two outputs | Accepted; balances and state root update |
| `ledger.spend.conservation` | Outputs exceed inputs | Rejected; state unchanged |
| `ledger.spend.zero_output` | Zero-value output | Rejected |
| `ledger.spend.overflow` | Output sum overflows | Rejected |
| `ledger.spend.duplicate_input` | Same UTXO referenced twice | Rejected |
| `ledger.spend.unknown_input` | Spend non-existent UTXO | Rejected |
| `ledger.spend.owner_mismatch` | Sign with key that does not own input | Rejected |
| `ledger.spend.double_spend` | Spend the same UTXO in a second tx | Rejected |
| `ledger.spend.replay` | Re-submit an accepted transaction | Rejected by replay window |
| `ledger.capacity` | Exceed bounded UTXO capacity | Rejected atomically |
| `ledger.atomicity` | One valid and one invalid input | Rejected; state root/balance unchanged |
| `ledger.block.apply` | Apply block with valid txs and state root | Accepted; tip/height update |
| `ledger.block.bad_state_root` | Block state root mismatch | Rejected atomically |
| `ledger.block.bad_prev` | Block previous hash mismatch | Rejected |

## Consensus

| ID | Scenario | Expected result |
| --- | --- | --- |
| `consensus.finality` | 3 of 4 signed accept votes | Finalized |
| `consensus.no_quorum` | 2 of 4 signed accept votes | Pending, not final |
| `consensus.duplicate_vote` | Same validator votes twice | Second vote rejected; not double-counted |
| `consensus.equivocation` | Same validator votes for conflicting hashes | Second vote rejected |
| `consensus.bad_signature` | Tampered vote signature | Rejected |
| `consensus.partition` | 2/2 split with no quorum | Neither side finalizes |
| `consensus.restore` | Restore pending votes and add the third | Finalized |
| `consensus.timeout` | Timeout clears votes and rotates proposer | Old-round vote rejected; new round works |
| `consensus.snapshot` | Encode/decode finalized state | Restored state matches |

## Storage

| ID | Scenario | Expected result |
| --- | --- | --- |
| `storage.mem.atomic` | Staged write before/after commit | Invisible before, visible after |
| `storage.mem.rollback` | Roll back staged write | Old value preserved |
| `storage.journal.commit` | Commit and read a key | New value returned |
| `storage.journal.crash_before_commit` | Power loss during write | Previous committed value preserved |
| `storage.journal.crash_after_commit` | Power loss after commit | New value preserved |
| `storage.journal.corrupt_slot` | Corrupt active slot | Falls back to valid slot |
| `storage.journal.both_corrupt` | Corrupt both slots | Read error; no auto-format |
| `storage.ledger.restart` | Snapshot ledger, decode into new ledger | State root, balances, and tip match |

## Transport

| ID | Scenario | Expected result |
| --- | --- | --- |
| `transport.loopback` | Send/receive a frame | Exact bytes delivered |
| `transport.drop` | Drop injected | Receive reports empty/timeout |
| `transport.duplicate` | Duplicate injected | Duplicate delivered; ledger rejects replay |
| `transport.reorder` | Dependent transaction arrives first | Rejected as unknown input; accepted after parent |

## End-to-end

| ID | Scenario | Expected result |
| --- | --- | --- |
| `e2e.happy_path` | Genesis → signed tx → loopback → ledger → quorum → block → apply → snapshot → restore | All steps succeed; state root matches |
| `e2e.double_spend` | Submit the same spend twice through the full path | Second transaction rejected |
| `e2e.partition_recovery` | Block pending without quorum, restore votes | Finalizes and applies exactly once |

## Shell regressions

| ID | Scenario | Expected result |
| --- | --- | --- |
| `gate.check.no_device` | `flash-gate.sh --check` with fake IDF | Build-only success; no serial/flash/eFuse command invoked |
| `gate.check.phase_scope` | Gate run against a non-phase_2 fixture | Refused |
| `gate.check.secure_boot` | Secure Boot enabled in sdkconfig | Refused |
| `gate.check.flash_encryption` | Flash encryption enabled in sdkconfig | Refused |
| `gate.check.efuse_write` | eFuse write API in source | Refused |
| `gate.check.auto_erase` | Auto-erase/format call in source | Refused |
| `gate.flash.missing_args` | `--flash` without port/board/size | Refused |
| `gate.flash.bad_port` | Non-existent serial path | Refused |
| `gate.flash.size_mismatch` | Board size differs from sdkconfig | Refused before device access |
| `gate.flash.noninteractive` | `--flash` without a terminal | Refused before device access |

## QEMU harness

The harness must:

- build the pinned ESP-IDF v5.2.2 image;
- create the flash image from `build/flash_args` with the configured 8 MB size;
- verify the pinned S3 QEMU release and machine;
- run with radios disabled and no eFuse drive;
- capture UART and exit status;
- write a per-run manifest with source revision, IDF/QEMU versions, target,
  sdkconfig hash, command, scenario, log path, and test summary;
- fail on any `status=FAIL`, missing `RUN_COMPLETE`, or missing summary.
