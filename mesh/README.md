# Arborisis Mesh

**Un firmware LoRa pour deux réseaux : MeshCore et Reticulum.** Un nœud
flashé Arborisis Mesh est à la fois un répéteur MeshCore (le vrai, avec sa
CLI et l'administration depuis l'application MeshCore), un nœud de
transport Reticulum, et un modem RNode pour Sideband, `rnsd` ou MeshChat —
sur **83 cartes** (94 environnements) : ESP32, ESP32-S3, ESP32-C3, ESP32-C6,
nRF52840, RP2040 et STM32WL, avec des radios SX1262, SX1268, SX1276,
LLCC68, LR1110, LR2021 et STM32WL.

- Analyse du firmware d'origine : [`docs/ANALYSE-FIRMWARE-ACTUEL.md`](docs/ANALYSE-FIRMWARE-ACTUEL.md)
- Architecture : [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- Cartes et état de compilation : [`docs/MATERIEL.md`](docs/MATERIEL.md)

> **État : bêta, non testé sur matériel.** Tout compile et les tests hôtes
> passent ; le partage de la radio entre deux canaux n'a pas encore été
> mesuré sur une vraie carte. Les modes à un seul protocole (`meshcore`,
> `rns`, `rnode`) reposent sur des mécanismes éprouvés ; `dual` sur deux
> canaux est expérimental. Retours bienvenus.

## Ce que fait un nœud, selon le mode

| Mode | MeshCore | Reticulum sur la carte | Modem RNode par USB |
|---|---|---|---|
| `dual` (défaut) | répéteur | nœud de transport | oui |
| `meshcore` | répéteur | — | oui |
| `rns` | — | nœud de transport | oui |
| `rnode` | — | — | oui |

Canaux par défaut (profil Arborisis, `app/ArbProfile.h`) :

- **Reticulum** : 869,525 MHz, 125 kHz, SF8, CR 4/5 — le canal des relais
  Arborisis Belgium ;
- **MeshCore** : 869,618 MHz, 62,5 kHz, SF8 — le défaut européen de MeshCore,
  réglable comme sur tout répéteur (`set radio …`) ;
- **budget d'émission** : 10 % par heure pour l'appareil entier (sous-bande
  869,4–869,65 MHz, ETSI EN 300 220).

## Compiler

```bash
python3 -m venv .venv && .venv/bin/pip install platformio
cd mesh
../.venv/bin/pio run -e arb_heltec_v3        # une carte
../.venv/bin/pio run                          # toutes les cartes (long)
../.venv/bin/pio test -e arb_native           # tests hôtes
python3 test/sim/e2e_rns.py                   # vraie pile Reticulum contre le protocole RNode (pip install rns)
python3 tools/release.py arb_heltec_v3 arb_rak_4631   # images + manifest dans dist/
```

Les noms d'environnements sont dans `arborisis_envs.ini` (générés) et dans
[`docs/MATERIEL.md`](docs/MATERIEL.md).

## Flasher

| Famille | Fichier | Comment |
|---|---|---|
| ESP32 / S3 / C3 / C6 | `…_merged.bin` | `esptool.py write_flash 0x0 fichier_merged.bin`, ou un flasheur Web Serial ESP |
| nRF52840 | `….uf2` | double appui sur RESET, déposer l'UF2 sur le disque USB qui apparaît |
| RP2040 | `….uf2` | BOOTSEL + branchement, déposer l'UF2 |
| STM32WL | `….hex` | STM32CubeProgrammer (SWD) |

Sur ESP32, le premier démarrage formate le système de fichiers en LittleFS
si besoin : une carte qui portait un firmware MeshCore recrée alors son
identité MeshCore.

## Utiliser

### Avec Sideband, `rnsd`, MeshChat (Reticulum)

La carte est un RNode sur son port USB. Dans la configuration de Reticulum :

```ini
[[Arborisis LoRa]]
  type = RNodeInterface
  enabled = yes
  port = /dev/ttyACM0
  frequency = 869525000
  bandwidth = 125000
  txpower = 14
  spreadingfactor = 8
  codingrate = 5
```

Pendant que l'hôte est branché, **son** canal devient le canal Reticulum de
la carte (le répéteur MeshCore continue sur le sien) ; quand il part, le
canal configuré revient.

En Bluetooth LE (cartes ESP32 et nRF52), la carte s'annonce
**« RNode XXXX »** : appairez-la dans les réglages Bluetooth du téléphone avec
le code à six chiffres affiché à l'écran (ou donné par `arb ble` sur la
console), puis choisissez-la comme RNode dans Sideband.

### Avec l'application MeshCore

Le répéteur apparaît comme n'importe quel répéteur MeshCore : connexion
avec le mot de passe administrateur (`password` par défaut — **à changer** :
`password <nouveau>` sur la console), statistiques, voisins, réglages radio
de MeshCore.

### Console série (115200 bauds)

Les lignes `arb …` règlent Arborisis Mesh ; toute autre ligne va à la CLI du
répéteur MeshCore.

| Commande | Effet |
|---|---|
| `arb` / `arb status` | état : mode, canaux, plan d'écoute, compteurs, airtime |
| `arb json` | le même état en JSON (outils, page Web) |
| `arb mode dual\|meshcore\|rns\|rnode` | change de mode (redémarre) |
| `arb rns radio 869.525,125,8,5` | canal Reticulum : MHz, kHz, SF, CR (immédiat) |
| `arb rns freq\|bw\|sf\|cr\|txp <v>` | un paramètre du canal Reticulum |
| `arb rns transport on\|off` | relayer pour les autres (redémarre) |
| `arb rns paths <n>` | taille de la table de chemins, 0 = défaut (redémarre) |
| `arb ble` / `arb ble on\|off` / `arb ble pin <6 chiffres>` | RNode en Bluetooth LE : état, activation, code d'appairage (redémarre) |
| `arb duty <%>` | budget d'émission de l'appareil, 0 = sans limite |
| `arb name <texte>` | nom affiché |
| `arb display <s>` | extinction de l'écran, 0 = toujours allumé |
| `arb log on\|off` | journaux Reticulum et arbitre sur la console |
| `arb reboot` / `arb reset` | redémarrer / réglages Arborisis par défaut |
| `ver`, `set radio …`, `neighbors`, `advert`, `password …` | CLI MeshCore |

### Changer de mode avec le bouton

Sur les cartes avec écran et bouton (52 environnements) :

- **appui court** : page suivante (vue d'ensemble, MeshCore, Reticulum, radio) ;
- **appui long** : ouvre le menu **Mode** — `MC + Reticulum`, `MeshCore`,
  `Reticulum`, `RNode modem` ; les appuis courts déplacent le curseur, un
  **appui long** enregistre le mode choisi et redémarre la carte (un appui
  long sur le mode actuel, ou 15 s sans appui, referme le menu).

Sur les cartes sans écran dont le fichier de carte MeshCore indique le
sens du bouton (`USER_BTN_PRESSED` : T1000-E, MeshTracker X1, R1 Neo…), un
**triple appui** passe au mode suivant et redémarre ; la console l'annonce.
Partout, `arb mode …` fait la même chose depuis la console.

## Ajouter une carte, suivre MeshCore

Les cartes viennent de MeshCore (`variants/`). Pour suivre une nouvelle
version de MeshCore :

```bash
tools/sync_meshcore.sh <commit-ou-tag>     # importe MeshCore, régénère app/mc
python3 tools/gen_envs.py                  # un environnement par carte
../.venv/bin/pio test -e arb_native && ../.venv/bin/pio run
python3 tools/matrix.py                    # met docs/MATERIEL.md à jour
```

Une carte que MeshCore ajoute devient ainsi une carte Arborisis Mesh.

## Licences

`app/`, `tools/`, `test/`, `docs/` : GPL-3.0-or-later (comme le reste du
dépôt). MeshCore (`src/`, `variants/`, `lib/`, `boards/`, `arch/`,
`examples/`, `app/mc/`) : MIT, voir `LICENSE-MeshCore.txt`. microReticulum
(`../lib/microReticulum`) : Apache-2.0.
