# SuperNova Phase 2 — architecture and API map

Status: implementation for a lab research prototype. This is not a production
ledger, wallet, currency, or security claim. Build-, QEMU-, and hardware-level
evidence are kept separate (see `docs/qemu.md` and `docs/flash-gate.md`).

## Scope and safety boundary

Phase 2 implements the revised thesis software path with explicit, testable
interfaces:

- canonical, versioned transaction/block encoding;
- a bounded in-memory UTXO ledger with atomic validate-then-commit;
- a deterministic permissioned quorum-vote prototype (not a BFT claim);
- a software test-key signing baseline and a disabled DS adapter boundary;
- deterministic in-memory storage plus a separately tested journal backend;
- a deterministic loopback transport plus a disabled-by-default ESP-NOW adapter;
- an end-to-end diagnostic test application with structured serial logs.

Hard rules preserved from the Phase 2 brief:

- No eFuse write/burn/program/protect operation or API is generated or called.
- Secure Boot and flash encryption stay disabled.
- No flash erase, no NVS/SPIFFS auto-format, no silent storage erase.
- Radios are disabled by default; QEMU never transmits.
- Hardware flashing is only possible through `scripts/flash-gate.sh --flash`
  after explicit user approval. The implementation agent does not run it.
- Test keys are test-only constants in the test application, never logged and
  never presented as a security feature.

## Component map

| Component | Responsibility | Key files |
| --- | --- | --- |
| `sn_protocol` | Canonical wire encoding, bounds, domains, txid/block hashes, frame envelope | `components/sn_protocol` |
| `sn_crypto` | SHA-256 wrapper, signing-provider interface, software test-key signer, disabled DS boundary | `components/sn_crypto` |
| `sn_ledger` | Bounded UTXO state, authorization, atomic apply, replay/double-spend/capacity checks, snapshot | `components/sn_ledger` |
| `sn_consensus` | Fixed membership, proposer rotation, signed votes, quorum finality, timeout, restore | `components/sn_consensus` |
| `sn_storage` | KV persistence abstraction, deterministic memory backend, two-slot journal backend with crash injection | `components/sn_storage` |
| `sn_transport` | Transport abstraction, deterministic loopback with drop/duplicate/reorder injection, ESP-NOW adapter behind Kconfig | `components/sn_transport` |
| `sn_app` | Deterministic test suite and end-to-end path, structured logs, test-only keys | `components/sn_app` |
| `main` | ESP-IDF entry point | `main/main.c` |

## Protocol specification (v1)

All multi-byte integers are **big-endian**. No packed C struct is a wire
format. Every encoder writes field by field and every decoder checks bounds and
trailing bytes.

Constants:

- protocol version `1`, test network id `0x02`
- public key: uncompressed P-256, 65 bytes (`0x04 || X || Y`)
- signature: ECDSA P-256/SHA-256, 64 bytes (`r || s`, 32 bytes each)
- signature algorithm id `1`
- max inputs per tx `4`; max outputs per tx `4`
- max tx bytes `2048`; max block txs `8`; max block bytes `16384`
- max UTXOs `32`; recent replay window `32` txids
- max amount `UINT64_MAX - 1`

Domain separation strings (hashed as `SHA256(domain || data)`):

| Purpose | Domain |
| --- | --- |
| Full transaction encoding | `SN2/TX/v1` |
| Transaction id | `SN2/TXID/v1` |
| Transaction signing digest | `SN2/TXSIG/v1` |
| Block header | `SN2/BLOCK/v1` |
| Block hash | `SN2/BLOCKID/v1` |
| Block signing digest | `SN2/BLOCKSIG/v1` |
| Transaction root | `SN2/TXROOT/v1` |
| Ledger state root | `SN2/STATE/v1` |
| Genesis allocation id | `SN2/GENESIS/v1` |
| Consensus vote digest | `SN2/VOTE/v1` |

### Transaction wire layout (full)

```
u16  version
u8   network_id
u16  input_count
repeat input_count:
  u8[32] prev_txid
  u32    prev_output_index
  u8[65] pubkey
  u8     sig_alg
  u8[64] signature
u16  output_count
repeat output_count:
  u64    value
  u8[65] owner_pubkey
```

The **unsigned** form is identical but omits each 64-byte signature. The
transaction id is `SHA256("SN2/TXID/v1" || full_encoding)`. The signing digest
is `SHA256("SN2/TXSIG/v1" || unsigned_encoding)`. Each input signature covers
the same whole-transaction signing digest, binding inputs and outputs together.

### Block wire layout

```
u16  version
u8   network_id
u32  height
u8[32] prev_block_hash
u64  timestamp
u16  tx_count
u8[32] tx_root
u8[32] state_root
u8[65] proposer_pubkey
repeat tx_count:
  u16 tx_len
  u8[tx_len] full_transaction_encoding
```

`tx_root = SHA256("SN2/TXROOT/v1" || txid_0 || ... || txid_n-1)`. The block hash
is `SHA256("SN2/BLOCKID/v1" || header)`; the proposer signing digest is
`SHA256("SN2/BLOCKSIG/v1" || block_hash)`. `state_root` commits to the ledger
state after applying the block. Genesis has height 0, zero `prev_block_hash`,
zero transactions, and the genesis allocation state root.

### Frame envelope

```
u32 magic = 0x534E3246 ("SN2F")
u8  version = 1
u8  type (1=tx, 2=block, 3=vote)
u16 reserved = 0
u16 payload_len
u8[payload_len] payload
```

Frames reject bad magic/version/reserved, zero/oversized lengths, and trailing
bytes. The loopback transport carries frames; the application validates the
payload before acting on it.

## Ledger API

`components/sn_ledger/include/sn_ledger.h`:

```c
void sn_ledger_init(sn_ledger_t *ledger);
esp_err_t sn_ledger_genesis(sn_ledger_t *ledger,
                            const sn_genesis_output_t *outputs, size_t count,
                            uint32_t height, const uint8_t tip_hash[32]);
esp_err_t sn_ledger_apply_tx(sn_ledger_t *ledger, const sn_tx_t *tx,
                             sn_ledger_result_t *result);
esp_err_t sn_ledger_apply_block(sn_ledger_t *ledger, const sn_block_t *block,
                                sn_ledger_result_t *result);
uint64_t sn_ledger_balance(const sn_ledger_t *ledger,
                           const uint8_t owner_pubkey[65]);
esp_err_t sn_ledger_state_root(const sn_ledger_t *ledger, uint8_t out[32]);
esp_err_t sn_ledger_snapshot_encode(const sn_ledger_t *ledger,
                                    uint8_t *buf, size_t cap, size_t *out_len);
esp_err_t sn_ledger_snapshot_decode(const uint8_t *buf, size_t len,
                                    sn_ledger_t *ledger);
```

Rules enforced before any mutation:

1. protocol shape, version, network, counts, bounds;
2. no duplicate input references in one transaction;
3. transaction id not in the bounded replay window;
4. every input exists, is unspent, and its owner matches the signing key;
5. every input signature verifies against the whole-transaction digest;
6. no zero-value outputs; no value overflow;
7. `sum(outputs) == sum(inputs)` (no fee in this prototype);
8. resulting UTXO count does not exceed the bounded capacity.

Only after all checks pass are inputs marked spent, outputs appended, the
transaction id recorded, and the state root invalidated. A block is applied to
a heap scratch copy and committed only if every transaction, the tx root, the
previous block hash, and the resulting state root all match. Failure leaves the
original ledger byte-for-byte unchanged.

## Consensus API

`components/sn_consensus/include/sn_consensus.h`:

```c
esp_err_t sn_consensus_init(sn_consensus_t *c,
                            const sn_validator_t *members, size_t member_count,
                            size_t quorum, uint32_t timeout_steps);
esp_err_t sn_consensus_start_height(sn_consensus_t *c, uint32_t height,
                                    const uint8_t block_hash[32]);
esp_err_t sn_consensus_sign_vote(const sn_signer_t *signer, uint8_t voter_id,
                                 uint32_t height, uint32_t round,
                                 const uint8_t block_hash[32],
                                 sn_vote_decision_t decision, sn_vote_t *out);
esp_err_t sn_consensus_cast_vote(sn_consensus_t *c, const sn_vote_t *vote);
esp_err_t sn_consensus_timeout(sn_consensus_t *c);
esp_err_t sn_consensus_snapshot_encode(const sn_consensus_t *c,
                                       uint8_t *buf, size_t cap, size_t *out_len);
esp_err_t sn_consensus_snapshot_decode(const uint8_t *buf, size_t len,
                                       sn_consensus_t *c);
```

Protocol properties implemented and tested:

- static membership set at initialization; quorum must be `> member_count/2`
  and `<= member_count`;
- deterministic proposer rotation
  `members[(height - 1 + round) % member_count]`;
- each vote is signed over
  `SHA256("SN2/VOTE/v1" || height || round || block_hash || voter_id || decision)`;
  the round is part of the signed digest, so a vote from an old round cannot be
  replayed into a new round;
- one vote per validator per height/round; a second vote is rejected
  (equivocation is not counted);
- finality when at least `quorum` distinct accept votes exist for the current
  height and block hash;
- a partition that leaves fewer than `quorum` votes leaves the block pending,
  not final; after votes are delivered again, the same pending votes can reach
  quorum;
- timeout increments the round, clears the current vote set, rotates the
  proposer, and rejects stale-round votes;
- consensus snapshots restore votes and finality after restart.

This is a **permissioned quorum-vote prototype**. It assumes honest,
non-equivocating validators and authenticated votes. It is not a Byzantine
fault-tolerant protocol, has no view-change proof, no durable anti-equivocation
across independent machines, and no production safety claim.

## Crypto API

`components/sn_crypto/include/sn_crypto.h`:

```c
esp_err_t sn_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
esp_err_t sn_sha256_many(const sn_iovec_t *parts, size_t count, uint8_t out[32]);

typedef struct {
    const char *name;
    void *ctx;
    esp_err_t (*get_pubkey)(void *ctx, uint8_t out[65]);
    esp_err_t (*sign_digest)(void *ctx, const uint8_t digest[32],
                             uint8_t out_sig[64]);
} sn_signer_t;

esp_err_t sn_verify_digest(const uint8_t pubkey[65],
                           const uint8_t digest[32],
                           const uint8_t signature[64]);
esp_err_t sn_soft_signer_init(sn_soft_signer_t *s, const uint8_t scalar[32]);
void sn_soft_signer_deinit(sn_soft_signer_t *s);
esp_err_t sn_ds_signer_init(sn_ds_signer_t *s);
```

`sn_soft_signer_*` is the software test-key baseline. It uses deterministic
ECDSA (RFC 6979) over P-256. Test key material lives only in
`sn_app_test_keys.c`; it is not logged and is explicitly marked test-only.

`sn_ds_signer_*` is a disabled/unprovisioned adapter boundary. It performs no
eFuse read/write/program operation. It returns `ESP_ERR_NOT_SUPPORTED` until a
separately reviewed provisioning and use plan is authorized. The ESP32-S3 DS
peripheral is RSA-based, not ECDSA; it is not used to sign the ECDSA test path.

## Storage API

`components/sn_storage/include/sn_storage.h`:

```c
typedef struct {
    const char *name;
    void *ctx;
    esp_err_t (*get)(void *ctx, const char *key, uint8_t *buf, size_t cap,
                     size_t *out_len);
    esp_err_t (*put)(void *ctx, const char *key, const uint8_t *buf, size_t len);
    esp_err_t (*commit)(void *ctx);
    esp_err_t (*rollback)(void *ctx);
} sn_storage_backend_t;

esp_err_t sn_storage_mem_init(sn_storage_mem_t *m);
void sn_storage_mem_deinit(sn_storage_mem_t *m);
esp_err_t sn_storage_journal_init(sn_storage_journal_t *j);
void sn_storage_journal_deinit(sn_storage_journal_t *j);
```

- The memory backend stages writes and only publishes them on `commit`.
  `rollback` discards staged writes. It never erases on error.
- The journal backend stores each key in two slots with sequence numbers and a
  CRC. `commit` writes the inactive slot and then advances the active index.
  A simulated power loss during the write leaves the previous committed slot
  valid; a power loss after commit preserves the new value. A corrupt slot falls
  back to the other valid slot. If both slots are corrupt, reads return an error;
  nothing is auto-formatted or erased.
- Both backends are deterministic and run in QEMU. Hardware NVS/SPIFFS
  persistence is deliberately deferred. It would need a separate reviewed
  design for partition layout, authenticated envelopes, nonce/freshness, and
  rollback protection.

## Transport API

`components/sn_transport/include/sn_transport.h`:

```c
typedef struct {
    const char *name;
    void *ctx;
    esp_err_t (*init)(void *ctx);
    esp_err_t (*send)(void *ctx, const uint8_t *data, size_t len);
    esp_err_t (*recv)(void *ctx, uint8_t *buf, size_t cap, size_t *out_len);
    esp_err_t (*deinit)(void *ctx);
} sn_transport_t;

esp_err_t sn_transport_loopback_init(sn_transport_loopback_t *lb);
void sn_transport_loopback_inject_drop(sn_transport_loopback_t *lb, size_t count);
void sn_transport_loopback_inject_duplicate(sn_transport_loopback_t *lb, size_t count);
void sn_transport_loopback_inject_reorder(sn_transport_loopback_t *lb);
esp_err_t sn_transport_espnow_init(sn_transport_espnow_t *en);
```

- Loopback is deterministic, bounded, and radio-free. It can drop, duplicate,
  or reorder frames for integration tests.
- The ESP-NOW adapter is compiled behind `CONFIG_SN_ENABLE_ESPNOW` (default
  `n`). When disabled, initialization and sends return errors without touching
  Wi-Fi. When enabled, it implements single-hop ESP-NOW with an explicit peer
  and channel. Multi-hop routing, forwarding, deduplication, retransmission,
  confidentiality, peer authentication handshakes, and partition recovery are
  **deferred** and documented as such. Application signatures and the ledger
  provide authorization/replay checks, not transport confidentiality.
- QEMU builds must have `CONFIG_SN_ENABLE_ESPNOW` unset. The QEMU harness fails
  closed if it is enabled.

## Application and diagnostics

`components/sn_app` runs a deterministic suite on boot:

1. protocol vectors and malformed inputs;
2. signature accept/reject;
3. ledger genesis, spend, atomicity, conservation, overflow, zero-value,
   duplicate input, unknown input, owner mismatch, double spend, replay,
   capacity, block apply, bad state root, bad previous hash;
4. consensus finality, quorum loss/restore, duplicate/equivocating votes,
   bad signatures, partition, timeout, snapshot restore;
5. storage atomic commit, rollback, crash before/after commit, corrupt slot,
   both-corrupt refusal, ledger snapshot restart;
6. transport loopback, drop, duplicate, reorder;
7. end-to-end happy path, double-spend rejection, and partition/recovery.

Structured log lines are stable and machine-readable:

```
SN2|event=TEST|id=<id>|status=PASS|expected=<text>|actual=<text>|sim_ms=<n>
SN2|event=SUMMARY|total=<n>|passed=<n>|failed=<n>|sim_ms=<n>
SN2|event=RUN_COMPLETE|result=PASS|sim_ms=<n>
```

The application never logs private keys, random sample bytes, signatures as
secrets, or real-value data. `sim_ms` is a deterministic logical step counter,
not a hardware timing measurement.

## Test execution notes

The deterministic suite runs on the single-threaded main task. Large bounded
buffers (wire images, blocks, ledger snapshots, and the loopback queue) are kept
in static storage or the heap so the 64 KiB main-task stack is not exhausted.
The suite also uses the performance compiler optimization level so software
P-256 operations finish in bounded QEMU time. These are execution choices only;
they do not change protocol semantics or test expectations.

## Evidence levels

- **Build verified:** ESP-IDF v5.2.2 compiled the ESP32-S3 image. The current
  build produces `build/phase_2.bin` with 8 MB flash and radios disabled.
- **QEMU verified:** the pinned S3 QEMU
  (`esp_develop_9.2.2_20261109`) booted the complete 8 MB image and the
  deterministic suite passed: `177/177` tests, `RUN_COMPLETE|result=PASS`.
  See `docs/qemu.md` and the per-run manifest under `qemu-runs/`.
- **Gate/tooling verified:** `scripts/flash-gate.sh --check` passed and all ten
  `scripts/gate-regression.sh` refusal/no-device checks passed. See
  `docs/flash-gate.md`.
- **Hardware verified:** not performed. Physical flashing remains behind
  `scripts/flash-gate.sh --flash` and requires a separate approved hardware run
  on an identified expendable board. No hardware run is performed by this
  implementation.

QEMU does not emulate Wi-Fi or Bluetooth; its RNG, timing, power, and eFuse
behavior are simulated and are not hardware evidence. Secure Boot is unsupported
in the referenced S3 QEMU guide. The emulated eFuse model is not used.
