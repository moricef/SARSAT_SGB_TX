#!/usr/bin/env python3
"""Create version with sps=2 (2 samples/chip)."""

import numpy as np

# Read sps=16 version
with open('test_frame_known.iq', 'rb') as f:
    data = np.fromfile(f, dtype=np.float32)

iq = data[::2] + 1j * data[1::2]
print(f"Original: {len(iq)} samples @ 614,400 Hz (sps=16)")

# Decimate by 8 to get sps=2
decimated = iq[::8]  # 614,400 / 8 = 76,800
print(f"Decimated: {len(decimated)} samples @ 76,800 Hz (sps=2)")
print(f"Expected: 38,400 chips × 2 sps = 76,800 samples")

# Save
data_out = np.zeros(len(decimated) * 2, dtype=np.float32)
data_out[::2] = decimated.real
data_out[1::2] = decimated.imag

with open('test_frame_sps2.iq', 'wb') as f:
    data_out.tofile(f)

print(f"\n✓ Fichier sps=2 sauvegardé: test_frame_sps2.iq")
print(f"  Samples: {len(decimated)}")
print(f"  Sample rate: 76,800 Hz")
print(f"  SPS: 2")
print(f"  q_delay: 2/2 = 1 ✓")
print(f"\nTest avec:")
print(f"  ./dec406_dsss_test test_frame_sps2.iq cf32 2 76800 12000")
