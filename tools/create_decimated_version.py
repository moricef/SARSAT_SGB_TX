#!/usr/bin/env python3
"""Create version at chip rate (1 sample/chip instead of 16)."""

import numpy as np

# Read current version (16 sps)
with open('test_frame_known.iq', 'rb') as f:
    data = np.fromfile(f, dtype=np.float32)

iq = data[::2] + 1j * data[1::2]
print(f"Original: {len(iq)} samples (16 sps)")

# Decimate by 16 to get 1 sample/chip
decimated = iq[::16]
print(f"Decimated: {len(decimated)} samples (1 sps = chip rate)")
print(f"Expected: 38,400 chips × 2 channels = 76,800 chips")

# Save decimated version
data_out = np.zeros(len(decimated) * 2, dtype=np.float32)
data_out[::2] = decimated.real
data_out[1::2] = decimated.imag

with open('test_frame_chiprate.iq', 'wb') as f:
    data_out.tofile(f)

print(f"\n✓ Fichier décimé sauvegardé: test_frame_chiprate.iq")
print(f"  Samples: {len(decimated)}")
print(f"  Sample rate: 38,400 Hz (chip rate)")
print(f"  SPS: 1")
print(f"\nTest avec:")
print(f"  ./dec406_dsss_test test_frame_chiprate.iq cf32 1 38400 12000")
