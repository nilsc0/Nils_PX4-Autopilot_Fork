# MSP speed display review — Pixhawk 6X

Reviewed on 2026-10-18 for `px4_fmu-v6x_default`. The comparison baseline is
this branch's HEAD, `d6f12ad1c4`, without the local speed-display changes.
This is a focused review of changed speed-display code and adjacent MSP
transport/VTX memory accesses, not a certification or an audit of all PX4 code.

## Build comparison

Same target, toolchain and build configuration; the six original modified
files were backed up with SHA-256 hashes, replaced with their HEAD versions
for the baseline build, then restored with hash verification. Baseline and
modified ELF/map files were retained under `/tmp/px4-msp-review/` for this session.
Build/version metadata is included in the totals.

| Variant | FLASH used | FLASH free | Static initialized data | BSS |
| --- | ---: | ---: | ---: | ---: |
| Baseline before speed changes | 1,960,168 B (99.70%) | 5,912 B | 5,200 B | 98,140 B |
| Speed displays before this review | 1,960,712 B (99.73%) | 5,368 B | 5,200 B | 98,140 B |
| Speed displays plus transport/VTX hardening | 1,960,832 B (99.73%) | 5,248 B | 5,200 B | 98,140 B |

The configured FLASH region is 1,966,080 bytes (1920 KiB). The original speed
changes add 544 bytes; the reviewed result adds 664 bytes over baseline.
The existing firmware already nearly fills that region. A successful link
checks region capacity; it does not prove runtime or flight safety.

## Findings and corrections

- The speed strings use a 16-byte text buffer, bounded formatting, bounded
  string-length calculation and a four-byte DisplayPort header. A compile-time
  assertion checks the 20-byte structure layout. No heap allocation was added
  to production code. Speeds have explicit labels and km/h units; invalid,
  missing, stale or future-dated samples produce `---`.
- A pre-existing receive overflow was reproduced with AddressSanitizer:
  `MspV1::Receive` read 255 payload bytes plus a CRC into the caller's 255-byte
  buffer. Receive now accepts an explicit capacity and reads CRC separately.
- The receiver now waits for a complete body instead of returning a partial
  read as if it were a complete message. Message IDs are returned only for
  CRC-valid complete packets. Resynchronization scans at most 32 bytes per
  call; the driver processes at most eight receive results per update.
- The send path rejects lengths above MSPv1's 255-byte maximum and nonempty
  null payloads before its stack allocation. Empty null payloads are supported
  without calling `memcpy` on a null pointer.
- VTX table entries are validated for minimum/maximum packet size, index,
  name/label length and channel count before copying or indexing. Shorter
  valid tables are zero-padded locally. Previously, band/index zero could
  index before an array and short packets could reuse uninitialized bytes.
- VTX configuration/power storage is initialized. Status printing validates
  band and power indices; channel switching is bounded to the eight-element
  frequency array and the received band's channel count.

## Verification

- Final Pixhawk-6X firmware build: passed.
- Eleven host tests with AddressSanitizer and UndefinedBehaviorSanitizer:
  passed. Includes all 256 MSPv1 payload lengths, a too-small destination,
  fragmented payload, CRC failure, noise/resynchronization budget, send bounds,
  zero-length null payloads, speed units/source separation, invalid values,
  timestamp boundaries and actual DisplayPort packet bytes/checksums.
- The original receive overflow fails the sanitizer test before the fix and
  passes after it. Leak detection is disabled for this host test environment;
  the exercised production paths do not add dynamic allocation.
- VTX copy/index checks were reviewed in source and compiled for the target;
  these handlers have not been exercised with a real VTX or by the host tests.
- Compiler `-fstack-usage` for ARM: `MspOsd::Run()` 472 B before and after
  transport hardening; driver `Receive()` 288 -> 320 B; parser 32 -> 40 B.
  Speed conversion helper 40 B, per-topic wrappers 16 B. Send has a 32-byte
  fixed frame plus a bounded dynamic packet allocation (at most 261 bytes
  before alignment). These are individual compiler frames, not complete
  worst-case call-chain/interrupt stack bounds. The shared `lp_default` queue
  is configured with 3500 bytes; actual high-water usage must be measured.
- At 10 Hz, the two speed displays add at most 470 UART bytes/s, about 4.1%
  of a 115200-baud 8N1 link. This excludes existing OSD traffic and does not
  establish that writes never fail. The driver uses a nonblocking UART.
- C++ formatting and whitespace checks: passed.

To repeat the host tests after generating the SITL build headers:

```sh
python3 src/drivers/osd/msp_osd/test/run_host_tests.py --sanitize
```

The runner uses the existing SITL compilation database, a host C++ compiler
and system GoogleTest libraries. Test binaries are created in a temporary
directory. The deterministic clock exists only in that test executable.

## Pending bench test — propellers removed, vehicle disarmed

No hardware flashing or FC/O4 bench testing was performed during this review.

1. Load the current `build/px4_fmu-v6x_default/px4_fmu-v6x_default.px4` via
   QGroundControl. Preserve existing configuration. In `OSD_SYMBOLS`, enable
   bit 5 for GS and bit 22 for IAS. With the previous default 16383, both are
   enabled by 4210687; retain any other intentionally configured bits.
2. In QGroundControl's MAVLink Console, capture these read-only diagnostics:

   ```text
   ver all
   msp_osd status
   work_queue status
   top once
   listener sensor_gps 1
   listener airspeed_validated 1
   ```

   Repeat the status/top commands after at least ten minutes of normal OSD
   operation. Inspect CPU use, the `wq:lp_default` stack high-water usage and
   transmit-failure counters. Confirm the FC connection and uptime remain
   stable and that there are no watchdog/hardfault resets.
3. Confirm GS against valid GPS `vel_m_s * 3.6` and IAS against
   `indicated_airspeed_m_s * 3.6`. Without a valid input, expect `---`; a valid
   zero should display zero. Observe behavior when the O4 is unavailable and
   available again while the FC remains powered, if the power arrangement
   permits that test. Check for recovery and compare failure counters.

Stop the test if the FC resets, stack headroom becomes small, the queue is
persistently overloaded, or send failures rise continuously. Investigate the
logs before considering any flight. Successful builds and host memory tests
alone are insufficient to approve flight use.
