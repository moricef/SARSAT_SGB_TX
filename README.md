# SARSAT_SGB — COSPAS-SARSAT T.018 (2nd Generation) Beacon Transmitter

COSPAS-SARSAT T.018 second-generation beacon (SGB) transmitter for the
**Odroid-C4 + ADALM-PLUTO (PlutoSDR)** platform. Builds a T.018 frame,
modulates it (DSSS-OQPSK), and either transmits it through the PlutoSDR
or writes it to an I/Q file for off-line analysis.

---

## Overview

| Parameter | Value |
|-----------|-------|
| Modulation | OQPSK with DSSS (Direct Sequence Spread Spectrum) |
| Pulse shaping | Half-sine (each chip shaped by `sin(π·t/Tc)`) |
| Chip rate | 38.4 kchips/s per channel |
| Sample rate | **2.4576 MHz** (64 samples/chip, integer SPS) |
| Data rate | 300 bps (150 bps per I/Q channel) |
| Spreading | 256 chips/bit per channel |
| Error correction | BCH(250,202) — 48 parity bits |
| Frame | 252 bits (2 header + 202 info + 48 BCH) |
| Burst duration | ~1.0 s (38 400 chips/channel) |
| Frequency | 403 MHz (training), 406.028 MHz (operational) |

The 2.4576 MHz sample rate gives an integer 64 samples/chip and is the
rate the companion decoder (`DEC406_SGB`) expects.

---

## Features

- **PRN generator** (`prn_generator.c`) — 23-bit LFSR, polynomial
  x²³+x¹⁸+1, validated against T.018 Table 2.2 (`8000 0108 4212 84A1`).
- **BCH(250,202) encoder** (`t018_protocol.c`) — GF(2⁶), generator
  polynomial `0x1C7EB85DF3C97`, with frame integrity check.
- **OQPSK modulator** (`oqpsk_modulator.c`) — DSSS spreading, Tc/2
  Q-channel offset, half-sine pulse shaping, 2.4576 MHz I/Q output.
- **Frame builder** (`t018_protocol.c`) — GPS position encoding
  (T.018 Appendix C), all rotating-field types (G.008, ELT-DT, RLS,
  CANCEL), country codes (MID).
- **ELT sequence management** — phase 1 (5 s, 0–3 min), phase 2 (10 s,
  3–30 min), phase 3 (28.5 ± 1.5 s, 30 min+).
- **PlutoSDR interface** (`pluto_control.c`) — libiio TX, cyclic-buffer
  transmission, or SigMF file output.

---

## Hardware

- **Odroid-C4** (ARM64) — reference platform
- **ADALM-PLUTO (PlutoSDR)** over USB or network
- OS: Armbian / Ubuntu 20.04+ (64-bit)

Developed and run on the Odroid-C4. It also builds and runs on any Linux
host with libiio — the PlutoSDR provides the RF front end.

## Dependencies

```bash
sudo apt update
sudo apt install build-essential libiio-dev libiio-utils
```

| Package | Purpose |
|---------|---------|
| gcc (≥9) | C compiler |
| libiio-dev | PlutoSDR control library |
| libm | math library (part of glibc) |

---

## Build

```bash
make check-deps      # verify gcc + libiio
make                 # build bin/sarsat_sgb
```

Other targets: `make clean`, `make install` (to `/usr/local/bin`),
`make test`, `make debug`, `make help`.

---

## Usage

```
sarsat_sgb [options]

  -f <freq>    Frequency in Hz            (default: 403000000)
  -g <gain>    TX gain in dB              (default: 0)
  -t <type>    0=EPIRB 1=PLB 2=ELT 3=ELT-DT (default: 0)
  -c <code>    Country code / MID         (default: 227, France)
  -s <serial>  Serial number              (default: 13398)
  -m <mode>    0=Exercise 1=Test          (default: 1)
  -i <sec>     TX interval in seconds     (default: 10)
  -lat <deg>   Latitude                   (default: 43.2)
  -lon <deg>   Longitude                  (default: 5.4)
  -alt <m>     Altitude in metres         (default: 0)
  -u <uri>     PlutoSDR URI               (default: ip:192.168.2.1)
  -o <file>    Write I/Q to file instead of transmitting
  -h           Show help
```

### Examples

```bash
# Transmit an EPIRB test beacon (403 MHz, 10 s interval)
./bin/sarsat_sgb -f 403000000 -m 1 -i 10

# ELT simulation, 2-minute interval
./bin/sarsat_sgb -t 2 -c 227 -lat 45.5 -lon 1.5 -alt 1500 -i 120

# Custom PlutoSDR address
./bin/sarsat_sgb -t 1 -u ip:192.168.3.1
```

---

## File-output mode

With `-o`, one frame is generated and written as a **SigMF pair** instead
of being transmitted — no PlutoSDR required.

```bash
./bin/sarsat_sgb -o beacon.sigmf-data -t 0 -lat 43.2 -lon 5.4
```

| Property | Value |
|----------|-------|
| Format | SigMF — `.sigmf-data` + `.sigmf-meta` |
| Data type | `cf32_le` (interleaved complex float32) |
| Sample rate | 2.4576 MHz |
| Samples | ~2 457 632 (≈ 1.0 s) |
| File size | ~20 MB |

The file decodes directly with `DEC406_SGB`'s `dec406_iq` (default
float32 input). See `USAGE_FILE_MODE.md` for details.

---

## T.018 frame

252-bit frame: 2 header bits + 202 information bits + 48 BCH parity bits.
The information field carries TAC, serial, country (MID), homing/RLS/test
flags, GPS position (T.018 Appendix C), vessel ID, beacon type and one
rotating field (G.008, ELT-DT, RLS or CANCEL). BCH(250,202) corrects up
to 6 bit errors at the receiver.

---

## Project structure

```
SARSAT_SGB/
├── src/
│   ├── main.c              # CLI, transmission loop
│   ├── t018_protocol.c     # frame building, BCH(250,202)
│   ├── prn_generator.c     # 23-bit LFSR PRN sequences
│   ├── oqpsk_modulator.c   # OQPSK + DSSS, half-sine shaping
│   ├── rrc_filter.c        # RRC taps (legacy, kept for reference)
│   └── pluto_control.c     # PlutoSDR (libiio) + SigMF file output
├── include/                # headers
├── tools/                  # test-signal / analysis helpers
├── Makefile
└── README.md
```

---

## Validation

- **PRN**: checked against T.018 Table 2.2 at startup.
- **BCH**: encoder verifiable with the T.018 Appendix B.1 vector.
- **Frame**: BCH parity recomputed and compared on every frame.
- **Modulation**: I/Q range, average power and NaN/Inf sanity checks.

---

## Legal notice

**For training and testing only.**

- 403.000 MHz — authorised training frequency.
- 406.028 MHz — operational frequency, **emergency use only**.
- Keep TX power low during tests.
- Unauthorised transmission on 406 MHz can trigger false SAR alarms and
  is illegal in most jurisdictions. Ensure compliance with local
  spectrum regulations before transmitting.

---

## References

- **C/S T.018** — Specification for COSPAS-SARSAT Second-Generation
  406 MHz Distress Beacons
- T.018 Table 2.2 — PRN reference sequence
- T.018 Appendix B — BCH test vectors
- T.018 Appendix C — GPS position encoding
- Companion receiver: `DEC406_SGB` (decodes the signal this transmitter produces)

---

## Author

**Fabrice Morel (F4MLV)**, radio amateur and member of **FNRASEC /
ADRASEC09** — Association Départementale des RadioAmateurs au service de
la Sécurité Civile, département de l'Ariège.

Developed with the support of **ADRASEC09**, sponsor of this work.
ADRASEC volunteers perform 406 MHz distress-beacon direction-finding for
civil-security search-and-rescue operations.
