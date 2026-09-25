# Analyse du firmware actuel (Arborisis Relay / Pocket)

*État du dépôt au commit `1f71d50` (septembre 2026). Ce document sert de point
de départ au nouveau firmware bi-protocole `mesh/` (Arborisis Mesh).*

## 1. Ce que c'est

Le firmware de la racine du dépôt est un empilement de trois projets :

| Couche | Origine | Licence | Rôle |
|---|---|---|---|
| RNode Firmware | Mark Qvist | GPL-3.0 | modem LoRa piloté par un hôte en KISS (USB / BLE), pilotes radio maison `sx126x` / `sx127x` / `sx128x` |
| microReticulum | Chad Attermann (`lib/microReticulum`, v0.2.4 patchée) | Apache-2.0 | pile Reticulum C++ embarquée : transport, annonces, chemins, liens |
| RTNode | jrl290 | GPL-3.0 | nœud de transport LoRa ↔ TCP (WiFi), `FIREWALL_MODE`, portail captif |
| Arborisis | ce dépôt | GPL-3.0 | profil réseau belge (`Arborisis.h`), configurateur série JSON, écrans OLED, image « Pocket » |

Deux images Arborisis en sortent :

- **Arborisis Relay** — Heltec WiFi LoRa 32 V3 / V4 (ESP32-S3 + SX1262) :
  relais LoRa ↔ TCP vers `rns.arborisis.com` et `rns2.arborisis.com`, canal
  869,525 MHz / 125 kHz / SF8 / CR 4/5, budget d'airtime 10 %.
- **Arborisis Pocket** — Seeed Wio Tracker L1 Pro (nRF52840 + SX1262) : RNode
  pour Sideband en BLE / USB, et petit nœud de transport autonome.

## 2. Architecture

```
 RNode_Firmware.ino (3 345 lignes)
   ├─ setup(): EEPROM/ROM, radio, BLE, WiFi, portail, RNS
   ├─ loop():  file TX + CSMA, KISS série, RNS, TCP, écran
   ├─ receive_callback() (ISR) → pbuf → KISS vers l'hôte + LoRaInterface RNS
   └─ serial_callback() → commandes KISS + configurateur « ARB {json} »
 Boards.h (1 033 lignes)       carte = BOARD_MODEL + blocs #if de broches
 Utilities.h (2 196 lignes)    KISS, LED, EEPROM, airtime, affichage…
 sx126x/sx127x/sx128x.cpp      pilotes radio propres à RNode (pas RadioLib)
 FirewallMode.h / FirewallConfig.h / TcpInterface.h   boundary WAN (ESP32)
 lib/microReticulum            pile Reticulum
```

Points structurants :

1. **Monolithe à `#ifdef`.** L'essentiel de la logique est dans un seul `.ino`
   et des en-têtes contenant du code (`Utilities.h`, `Display.h`,
   `FirewallConfig.h`). Chaque fonctionnalité ajoute des branches
   `#if BOARD_MODEL == …`, `#ifdef FIREWALL_MODE`, `#ifdef ARBORISIS_*`.
2. **La carte est une constante de compilation.** Ajouter une carte, c'est
   ajouter un code produit/modèle, un bloc de broches dans `Boards.h`, parfois
   des cas dans `Display.h`, `Power.h`, `Input.h`, et un `env` PlatformIO.
3. **Pilotes radio maison.** Ils supportent SX126x, SX127x et SX128x, mais pas
   les LR1110 / LR1121 (T1000-E, Wio WM1110, Seeed L1 E-ink selon versions),
   ni le STM32WL (Wio-E5, RAK3172), ni les options fines de RadioLib.
4. **Reticulum greffé sur le chemin KISS.** Les paquets reçus en LoRa sont
   copiés vers l'hôte ET vers une `LoRaInterface` microReticulum
   (`kiss_write_packet`) ; les paquets RNS sortants passent par la même file
   que ceux de l'hôte. C'est simple et compatible RNode sur l'air, mais le
   transport embarqué dépend du mode « hôte » du modem.
5. **Format radio RNode.** Octet d'en-tête (4 bits de séquence aléatoires +
   drapeau `split`), paquets jusqu'à 508 octets en deux trames LoRa,
   préambule `max(18 symboles, 24 ms)`, mot de synchro `0x12` (`0x1424` en
   SX126x), CRC, en-tête explicite. **Tout nouveau firmware qui veut parler
   aux RNode existants doit reproduire ce format à l'octet près** — c'est ce
   que fait `mesh/` (voir `RNodeFraming`).
6. **FIREWALL_MODE** (voir `CORE_PRINCIPLES.md`) : filtrage immédiat du trafic
   WAN par listes blanches alimentées par le trafic LAN, pour protéger le tas
   de l'ESP32. C'est la partie la plus aboutie et la plus testée du projet.

## 3. Couverture matérielle réelle

`platformio.ini` déclare 34 environnements, mais :

| Catégorie | Environnements | État |
|---|---|---|
| Images Arborisis | `arborisis_heltec_v3`, `arborisis_heltec_v4`, `arborisis_pocket_l1` | V3 validé sur matériel ; V4 et Pocket jamais testés sur matériel |
| RTNode amont | `rtnode_heltec_v3`, `rtnode_heltec_v4` | testés par l'amont |
| RNode « stock » (hérités) | T-Beam, T3S3, LoRa32 v1/v2/v2.1, Heltec V2/V3/V4, T-Deck, Feather, XIAO S3, RAK4631, Wio L1… | compilent le RNode + microReticulum sans les ajouts Arborisis |
| Environnements `-local` | T-Beam, LoRa32 v2.1, Heltec V4, RAK4631, T114 | environnements de développement ; `ttgo-t-beam-local` et `wiscore_rak4631-local` dépendent de `symlink://../Adafruit_SPIFlash` et ne compilent pas hors de la machine de l'auteur |

Familles de MCU : **ESP32 et nRF52840 uniquement**. Pas de RP2040, pas de
STM32, pas d'ESP32-C3/C6. Radios : SX1262/1268, SX1276/1278, SX1280.

Compilation vérifiée dans cette session (`pio run -e arborisis_heltec_v3`) :
RAM statique 74 772 o / 327 680 o (22,8 %), flash 1 351 325 o / 3 342 336 o
(40,4 %), 210 avertissements.

## 4. MeshCore

**Absent.** Le firmware ne connaît ni le format de paquet MeshCore, ni ses
identités (Ed25519 / X25519, AES-128 + HMAC tronqué), ni son canal européen
(869,618 MHz / 62,5 kHz / SF8, préambule de 32 symboles). Un appareil
flashé Arborisis est invisible pour un réseau MeshCore, et inversement.

## 5. Forces à conserver

- Compatibilité RNode sur l'air et côté hôte (Sideband, `rnsd`, MeshChat).
- Le boundary firewall et son contrat (`CORE_PRINCIPLES.md`).
- Le profil réseau centralisé (`Arborisis.h`) : un seul endroit pour le canal.
- Le configurateur JSON sur port série, pilotable par une page Web Serial.
- Le travail de fond sur microReticulum (`MICRORETICULUM_BUGS.md` : MTU des
  liens, horodatages copiés par valeur, tables non bornées…).

## 6. Faiblesses

| # | Problème | Conséquence |
|---|---|---|
| F1 | Pilotes radio maison, une carte = un bloc `Boards.h` | chaque nouvelle carte coûte cher ; LR11x0 / STM32WL impossibles |
| F2 | ESP32 + nRF52 seulement | pas de RP2040 (Pico W, RAK11310), pas de STM32WL, pas d'ESP32-C3/C6 |
| F3 | Aucun support MeshCore | deux réseaux, deux parcs d'appareils |
| F4 | Monolithe `.ino` + en-têtes à code | revue et tests difficiles, effets de bord entre modes |
| F5 | Pas de tests automatisés du format radio | une régression d'octet d'en-tête casse l'interopérabilité en silence |
| F6 | Environnements `-local` cassés, fichiers de travail versionnés (`crash.log`, `nohup.out`, `*.bak`, `rtnode_firmware.zip`, binaires) | bruit, dépôt de 36 Mo |
| F7 | Validation matérielle partielle (V4, Pocket) | risque au premier flash |

## 7. Conséquences pour le nouveau firmware

La couverture matérielle la plus large disponible aujourd'hui est celle de
**MeshCore** : 87 variantes de cartes (ESP32, ESP32-S3/C3/C6, nRF52840,
RP2040, STM32WL), toutes décrites par la même interface (`board`,
`radio_driver`, `radio_init()`, RadioLib pour SX126x / SX127x / LLCC68 /
LR1110 / LR2021 / STM32WL). Plutôt que de porter ces 87 cartes dans
`Boards.h`, le nouveau firmware **réutilise cette couche matérielle** et
construit par-dessus une application Arborisis qui parle les deux protocoles :

- MeshCore : répéteur complet (même CLI, même administration à distance) ;
- Reticulum : format radio RNode, modem KISS compatible RNS/Sideband, et nœud
  de transport microReticulum embarqué quand la mémoire le permet ;
- un **arbitre radio** qui partage l'unique émetteur-récepteur entre les deux.

Le détail est dans [`ARCHITECTURE.md`](ARCHITECTURE.md).
