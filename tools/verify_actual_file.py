#!/usr/bin/env python3
import numpy as np
import sys

# Check both locations
files = [
    'test_frame_known.iq',
    'Fichiers_IQ/test_frame_known.iq'
]

for filename in files:
    try:
        with open(filename, 'rb') as f:
            data = np.fromfile(f, dtype=np.float32)
        
        if len(data) == 0:
            print(f"❌ {filename}: VIDE")
            continue
            
        iq = data[::2] + 1j * data[1::2]
        
        print(f"\n{'='*70}")
        print(f"FICHIER: {filename}")
        print(f"{'='*70}")
        print(f"Taille: {len(data)*4} bytes ({len(iq)} samples)")
        print(f"I min/max: {iq.real.min():.4f} / {iq.real.max():.4f}")
        print(f"Q min/max: {iq.imag.min():.4f} / {iq.imag.max():.4f}")
        
        # Skip transient
        core = iq[1000:5000]
        rms = np.sqrt(np.mean(np.abs(core)**2))
        peak = np.abs(core).max()
        
        print(f"\nCore samples (1000-5000):")
        print(f"  RMS amplitude: {rms:.4f}")
        print(f"  Peak: {peak:.4f}")
        print(f"  Attendu (√2): 1.4142")
        
        # Check if values are constant (no RRC)
        samples_per_chip = 16
        chip_values = []
        for i in range(1000, 2000, samples_per_chip):
            chip_values.append(np.abs(iq[i]))
        
        std_dev = np.std(chip_values)
        print(f"  Variabilité entre chips: σ={std_dev:.6f}")
        
        if std_dev < 0.01:
            print(f"  ✓ Amplitude CONSTANTE (sans RRC)")
        else:
            print(f"  ⚠ Amplitude VARIABLE (avec RRC?)")
            
        # Show first samples
        print(f"\nPremiers symboles (chaque 16 samples):")
        for i in range(0, 80, 16):
            print(f"  [{i:4d}] = {iq[i].real:+.3f} {iq[i].imag:+.3f}j  |mag|={np.abs(iq[i]):.4f}")
            
    except FileNotFoundError:
        print(f"\n❌ {filename}: FICHIER NON TROUVÉ")
    except Exception as e:
        print(f"\n❌ {filename}: ERREUR - {e}")

print(f"\n{'='*70}")
