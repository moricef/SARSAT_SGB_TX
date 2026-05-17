# SARSAT_SGB — Mode génération de fichier I/Q

Le mode fichier (`-o`) génère **une** trame T.018 modulée et l'écrit sur
disque au lieu de la transmettre. Aucun PlutoSDR n'est requis.

## Utilisation

```bash
./bin/sarsat_sgb -o beacon.sigmf-data [options]
```

Le programme génère une seule trame puis s'arrête. Toutes les options de
configuration (`-t`, `-c`, `-s`, `-m`, `-r`, `-lat`, `-lon`, `-alt`)
restent valables — voir `README.md`. Les types `-t` : 0=ELT, 1=EPIRB,
2=PLB, 3=ELT-DT.

### Exemples

```bash
# EPIRB (France, mode test)
./bin/sarsat_sgb -o epirb_test.sigmf-data -t 1 -lat 43.2 -lon 5.4

# PLB
./bin/sarsat_sgb -o plb_test.sigmf-data -t 2 -s 12345 -lat 45.0 -lon 6.0

# ELT avec altitude
./bin/sarsat_sgb -o elt_test.sigmf-data -t 0 -alt 1500 -lat 46.5 -lon 7.2

# EPIRB avec champ tournant RLS Type 1/2
./bin/sarsat_sgb -o rls_test.sigmf-data -t 1 -r rls -lat 43.2 -lon 5.4
```

## Format du fichier généré

`-o` produit une **paire SigMF** : `<nom>.sigmf-data` + `<nom>.sigmf-meta`.

| Propriété | Valeur |
|-----------|--------|
| Type de données | `cf32_le` (complex float32 entrelacé I/Q) |
| Taux d'échantillonnage | 2.4576 MHz |
| Nombre d'échantillons | ~2 457 632 |
| Durée | ~1.0 s |
| Taille | ~20 Mo |

Données : `[I0, Q0, I1, Q1, ...]`, chaque valeur un `float` 32 bits,
amplitude normalisée. Le `.sigmf-meta` (JSON) décrit le taux, la
modulation et la durée.

## Décodage et analyse

```bash
# Décoder avec le récepteur compagnon DEC406_SGB
./build/dec406_iq beacon.sigmf-data -s 2457600

# Inspecter le spectre
inspectrum beacon.sigmf-data        # régler Sample rate = 2457600 Hz

# GNU Radio : File Source -> Complex float32, sample rate = 2.4576e6
```

## Caractéristiques du signal

- Modulation OQPSK + DSSS, mise en forme demi-sinus
- Chip rate 38.4 kchips/s, 256 chips/bit, 64 échantillons/chip
- Durée de trame ~1.0 s (38 400 chips par canal I/Q)
- Décalage OQPSK : voie Q retardée de Tc/2 (32 échantillons)

## Dépannage

- **Fichier non créé** : vérifier les droits d'écriture et l'espace
  disque (~20 Mo nécessaires) ; essayer un chemin absolu.
- **Validation `oqpsk_verify_output()` échouée** : I/Q hors plage, ou
  présence de NaN/Inf — vérifier la chaîne de modulation.

## Référence

- Spécification : COSPAS-SARSAT T.018
- Fréquences : 431,975 MHz (exercice, bande 70 cm) / 406.028 MHz (opérationnel)
- Récepteur compagnon : `DEC406_SGB`
