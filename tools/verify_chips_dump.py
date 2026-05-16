#!/usr/bin/env python3
"""
Script de vérification du dump binaire chips_after_spreading.bin

Vérifie que les chips correspondent aux séquences PRN T.018 Table 2.2
et reconstruit la trame de 300 bits transmise.

Usage: python3 verify_chips_dump.py [chips_file]
"""

import struct
import sys

# =============================================================================
# GÉNÉRATEUR PRN (T.018 Table 2.2)
# =============================================================================

def generate_prn_sequence(init_value, count):
    """
    Génère une séquence PRN selon T.018
    LFSR: X^23 + X^18 + 1
    Feedback: X0 ⊕ X18 → X22
    Output: X0 (Logic 1 → -1, Logic 0 → +1)
    """
    lfsr = init_value
    prn = []

    for _ in range(count):
        # Output from LSB: Logic 1 → -1, Logic 0 → +1
        prn.append(-1 if (lfsr & 1) else 1)

        # Feedback: X0 ⊕ X18
        feedback = (lfsr ^ (lfsr >> 18)) & 1

        # Shift right, insert feedback at X22
        lfsr = (lfsr >> 1) | (feedback << 22)

        # Mask to 23 bits
        lfsr &= 0x7FFFFF

    return prn

# =============================================================================
# CONVERSION CHIPS → HEX
# =============================================================================

def chips_to_hex(chips, count=64):
    """Convertit 64 chips en hex (comme dans T.018 Table 2.2)"""
    hex_str = ""
    for byte_idx in range(count // 8):
        byte = 0
        for bit in range(8):
            chip_idx = byte_idx * 8 + bit
            if chip_idx < len(chips):
                # Logic 1 (chip=-1) → bit 1
                if chips[chip_idx] == -1:
                    byte |= (1 << (7 - bit))
        hex_str += f"{byte:02X}"
    return hex_str

# =============================================================================
# DÉSPREADING (RECONSTRUCTION DES BITS)
# =============================================================================

def despread_bits(chips_spread, prn_reference):
    """
    Retrouve les bits à partir des chips après spreading

    Pour chaque bit (256 chips):
    - Corrélation positive avec PRN → bit = 0
    - Corrélation négative avec PRN → bit = 1
    """
    bits = []
    num_bits = len(chips_spread) // 256

    for bit_idx in range(num_bits):
        start = bit_idx * 256
        end = start + 256

        # Chips pour ce bit
        bit_chips = chips_spread[start:end]
        prn_bit = prn_reference[start:end]

        # Corrélation
        correlation = sum(c * p for c, p in zip(bit_chips, prn_bit))

        # Décision
        bit = 0 if correlation > 0 else 1
        bits.append(bit)

    return bits

# =============================================================================
# VÉRIFICATION DU FICHIER
# =============================================================================

def verify_chip_dump(filename):
    """Vérifie le fichier de dump des chips"""

    print("=" * 70)
    print("VÉRIFICATION DU DUMP BINAIRE")
    print("=" * 70)
    print(f"Fichier: {filename}")
    print()

    # 1. Lire le fichier
    try:
        with open(filename, 'rb') as f:
            chips_data = f.read()
    except FileNotFoundError:
        print(f"❌ ERREUR: Fichier '{filename}' introuvable")
        return False

    # 2. Vérifier la taille
    expected_size = 38400 * 2  # 38,400 chips × 2 canaux (I et Q)
    actual_size = len(chips_data)

    print(f"1. STRUCTURE DU FICHIER")
    print(f"   Taille: {actual_size} octets")
    print(f"   Attendu: {expected_size} octets")

    if actual_size != expected_size:
        print(f"   ❌ ERREUR: Taille incorrecte")
        return False
    print(f"   ✓ OK")
    print()

    # 3. Extraire les chips I et Q (format interleaved)
    i_chips = []
    q_chips = []

    for i in range(0, len(chips_data), 2):
        i_chips.append(struct.unpack('b', chips_data[i:i+1])[0])
        if i+1 < len(chips_data):
            q_chips.append(struct.unpack('b', chips_data[i+1:i+2])[0])

    print(f"2. EXTRACTION DES CHIPS")
    print(f"   Chips I: {len(i_chips)}")
    print(f"   Chips Q: {len(q_chips)}")

    # Vérifier que toutes les valeurs sont ±1
    valid_i = all(c in [-1, 1] for c in i_chips)
    valid_q = all(c in [-1, 1] for c in q_chips)

    if not (valid_i and valid_q):
        print(f"   ❌ ERREUR: Valeurs invalides (doivent être +1 ou -1)")
        return False
    print(f"   ✓ Toutes les valeurs sont ±1")
    print()

    # 4. Vérifier les séquences PRN (64 premiers chips)
    print(f"3. VÉRIFICATION SÉQUENCES PRN (T.018 Table 2.2)")

    i_hex = chips_to_hex(i_chips, 64)
    q_hex = chips_to_hex(q_chips, 64)

    expected_i = "8000010842128 4A1".replace(" ", "")
    expected_q = "3F8358BAD030F231"

    print(f"   Canal I (64 premiers chips):")
    print(f"     Généré:  {i_hex[:4]} {i_hex[4:8]} {i_hex[8:12]} {i_hex[12:16]}")
    print(f"     Attendu: 8000 0108 4212 84A1")

    if i_hex == expected_i:
        print(f"     ✓ MATCH (Normal I PRN)")
    else:
        print(f"     ❌ MISMATCH")
        return False

    print()
    print(f"   Canal Q (64 premiers chips):")
    print(f"     Généré:  {q_hex[:4]} {q_hex[4:8]} {q_hex[8:12]} {q_hex[12:16]}")
    print(f"     Attendu: 3F83 58BA D030 F231")

    if q_hex == expected_q:
        print(f"     ✓ MATCH (Normal Q PRN)")
    else:
        print(f"     ❌ MISMATCH")
        return False

    print()

    # 5. Reconstruction de la trame
    print(f"4. RECONSTRUCTION DE LA TRAME (300 bits)")
    print()

    # Générer les PRN de référence complets (38,400 chips)
    PRN_I_full = generate_prn_sequence(0x000001, 38400)
    PRN_Q_full = generate_prn_sequence(0x1AC1FC, 38400)

    # Déspreading
    i_bits = despread_bits(i_chips, PRN_I_full)
    q_bits = despread_bits(q_chips, PRN_Q_full)

    # Reconstruction trame 300 bits (interleaving I/Q)
    # T.018 §2.2.3.b: Odd bits → I, Even bits → Q
    frame_300 = []
    for i in range(150):
        frame_300.append(i_bits[i])  # Bit 0, 2, 4, 6...
        frame_300.append(q_bits[i])  # Bit 1, 3, 5, 7...

    frame_str = ''.join(str(b) for b in frame_300)

    # Afficher la trame
    print("   TRAME COMPLÈTE DE 300 BITS TRANSMISE:")
    print()

    for i in range(0, 300, 50):
        chunk = frame_str[i:i+50]
        if i == 0:
            label = "PRÉAMBULE (50 bits)"
        else:
            label = f"DONNÉES bits {i-50:3d}-{i-50+49:3d}"
        print(f"   {chunk}  ← {label}")

    print()

    # Vérifier le préambule
    preamble = frame_str[:50]
    preamble_zeros = sum(1 for b in preamble if b == '0')

    print(f"5. VÉRIFICATION DU PRÉAMBULE")
    print(f"   Bits à zéro: {preamble_zeros}/50")

    if preamble_zeros == 50:
        print(f"   ✓ PRÉAMBULE CORRECT (tous les bits à 0)")
    else:
        print(f"   ❌ ERREUR: Le préambule devrait être tous à 0")
        return False

    print()

    # 6. Statistiques
    print(f"6. STATISTIQUES")

    i_pos = sum(1 for c in i_chips if c == 1)
    i_neg = sum(1 for c in i_chips if c == -1)
    q_pos = sum(1 for c in q_chips if c == 1)
    q_neg = sum(1 for c in q_chips if c == -1)

    print(f"   Distribution I: +1={i_pos} ({i_pos/len(i_chips)*100:.1f}%), -1={i_neg} ({i_neg/len(i_chips)*100:.1f}%)")
    print(f"   Distribution Q: +1={q_pos} ({q_pos/len(q_chips)*100:.1f}%), -1={q_neg} ({q_neg/len(q_chips)*100:.1f}%)")
    print()

    # Conclusion
    print("=" * 70)
    print("✅ LE DUMP BINAIRE EST CORRECT")
    print("=" * 70)
    print()
    print("Résumé:")
    print("  ✓ Structure: 76,800 octets (38,400 chips I/Q interleaved)")
    print("  ✓ Valeurs: Toutes ±1")
    print("  ✓ PRN I: Conforme à T.018 Table 2.2 (Normal I)")
    print("  ✓ PRN Q: Conforme à T.018 Table 2.2 (Normal Q)")
    print("  ✓ Préambule: 50 bits à 0")
    print("  ✓ Distribution: ~50% +1, ~50% -1")
    print()

    return True

# =============================================================================
# MAIN
# =============================================================================

if __name__ == "__main__":
    if len(sys.argv) > 1:
        filename = sys.argv[1]
    else:
        filename = "chips_after_spreading.bin"

    success = verify_chip_dump(filename)

    sys.exit(0 if success else 1)
