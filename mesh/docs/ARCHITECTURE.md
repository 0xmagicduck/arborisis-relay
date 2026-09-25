# Arborisis Mesh — architecture

Un firmware, deux réseaux LoRa : **MeshCore** et **Reticulum**, sur toutes les
cartes LoRa que MeshCore décrit (83 cartes, 94 environnements de compilation,
cinq familles de microcontrôleurs). L'analyse du firmware d'origine qui a
mené à cette conception est dans [`ANALYSE-FIRMWARE-ACTUEL.md`](ANALYSE-FIRMWARE-ACTUEL.md).

## Vue d'ensemble

```
                 USB (console partagée)                       écran + bouton
   rnsd / Sideband / MeshChat        outils MeshCore / terminal       │
            │ trames KISS                     │ lignes texte          │
            ▼                                 ▼                       ▼
     ┌─────────────┐   « arb … »   ┌──────────────────┐        ┌───────────┐
     │  RNodeHost  │◄──── Console ─┤ CLI du répéteur  │        │  ArbUI    │
     │ (protocole  │               │ MeshCore (MyMesh)│        │ 4 pages   │
     │   RNode)    │               └────────┬─────────┘        └───────────┘
     └──────┬──────┘                        │ mesh::Radio
            │ RNodeBackend                  ▼
     ┌──────┴──────┐  paquets RNS   ┌──────────────┐
     │   RnsSide   │◄──────────────►│ McRadioPort  │
     │ réassemblage│                └──────┬───────┘
     │ RNode, rôle │                       │
     └──┬───────┬──┘                       │
        │       │ RnsStack (microReticulum, transport embarqué)
        │       ▼                          │
        │  ┌──────────────┐                │
        └─►│ RadioArbiter │◄───────────────┘
           │ canaux, écoute, CAD, émission, budget d'airtime
           └──────┬───────┘
                  │ RadioLibWrapper (setParams, gain RX, AGC…) + PhysicalLayer
                  ▼
     variants/<carte>/ de MeshCore : board, radio_driver, display, capteurs
     SX1262 · SX1268 · SX1276 · LLCC68 · LR1110 · LR2021 · STM32WL
```

## 1. Couche matérielle : celle de MeshCore

MeshCore (MIT) décrit chaque carte dans `variants/<carte>/` : un fichier
`platformio.ini` avec les broches et la classe radio, un `target.cpp` qui
construit `board` (alimentation, LED, batterie, FEM), `radio_driver` (une
enveloppe RadioLib propre à la puce), l'horloge, les capteurs et l'écran.
Arborisis Mesh **n'en modifie rien** : l'arborescence de MeshCore est
importée telle quelle (`tools/sync_meshcore.sh`, commit dans
`MESHCORE_VERSION`), et `tools/gen_envs.py` produit un environnement
`arb_<carte>` par carte, qui hérite de l'environnement « répéteur » de
MeshCore et remplace son application par la nôtre (`app/`).

Conséquence : **toute carte que MeshCore ajoute devient une carte Arborisis
Mesh** après `sync_meshcore.sh` + `gen_envs.py`.

## 2. L'arbitre radio (`RadioArbiter`)

Un seul émetteur-récepteur, deux protocoles, en général deux canaux (en
Belgique : MeshCore sur 869,618 MHz / 62,5 kHz / SF8, Reticulum sur
869,525 MHz / 125 kHz / SF8). L'arbitre :

- garde le canal de chaque protocole (celui de MeshCore vient de ses
  préférences, celui de Reticulum de `arb rns …` ou de l'hôte RNode) ;
- reprogramme la puce avec `radio_driver.setParams()` — la fonction de la
  variante, donc toutes les particularités de puce que MeshCore gère sont
  gérées — et fixe le préambule de chaque protocole ;
- pilote réception, émission et CAD lui-même, par RadioLib et un unique
  drapeau d'interruption.

### Écouter deux canaux (`ChannelPlan`)

Un récepteur LoRa n'écoute qu'un canal à la fois ; ce qui rend le partage
possible, c'est le **préambule** : avant chaque paquet, l'émetteur envoie
une suite de symboles identiques, et un récepteur qui arrive pendant cette
suite attrape encore le paquet.

- Le récepteur **reste** sur le canal au préambule le plus court (A) ;
- toutes les `peek_every_ms`, il **jette un œil** à l'autre canal (B) par
  une détection d'activité (CAD, deux ou quatre symboles) et revient ;
- si la CAD voit un préambule, il reste sur B et reçoit le paquet.

La période de coup d'œil vaut un tiers du préambule de B (chaque paquet sur
B est vu au moins deux fois) ; un coup d'œil coûte deux changements de canal
et une CAD, et doit rester plus court que le préambule de A moins ce dont le
récepteur de A a besoin pour se caler (six symboles). Sinon le plan est
marqué `degraded` (affiché par `arb status`).

Avec les canaux belges : A = Reticulum (18 symboles × 2,05 ms = 36,9 ms),
B = MeshCore (32 symboles × 4,1 ms = 131 ms), un coup d'œil toutes les
43 ms pendant ~17 ms : **non dégradé**. Les tests hôtes le vérifient
(`test/test_arb_core`).

### Un seul canal pour les deux

Si les deux protocoles utilisent exactement la même fréquence, la même
largeur de bande et le même SF, il n'y a rien à surveiller : un seul
récepteur, et chaque trame est triée par son contenu
(`FrameClassifier.h`). Les deux protocoles utilisent le mot de synchro
0x12 et n'étiquettent pas leurs trames ; le tri reconnaît la structure
d'une trame RNode/Reticulum (en-tête RNode, drapeaux, nombre de sauts,
octet de contexte, taille minimale d'une annonce). Une trame Reticulum est
toujours reconnue ; environ 1 % des diffusions MeshCore d'un certain type
(REQ, ADVERT, PATH) est prise à tort pour du Reticulum et perdue. **Deux
canaux distincts restent la configuration recommandée.**

### Émettre

- MeshCore garde son `Dispatcher` : l'écoute avant émission passe par
  `isReceiving()`, qui répond depuis l'arbitre ; si MeshCore est le canal
  surveillé par coups d'œil, `isReceiving()` fait une CAD immédiate sur son
  canal (10 à 20 ms) et, s'il est libre, laisse la radio sur ce canal pour
  l'émission qui suit.
- Reticulum passe par une file de 8 paquets. Chaque paquet est découpé au
  format RNode (`RNodeFraming.h`) au moment d'émettre ; la seconde moitié
  d'un paquet long suit immédiatement la première, comme sur un RNode.
  Écoute avant émission : détection de préambule sur le canal écouté, CAD
  sur le canal surveillé, repli aléatoire de 1 à 8 créneaux CSMA RNode.
- **Budget d'airtime unique** (`arb duty`, 10 % par défaut) : les deux
  protocoles partagent un émetteur, donc une seule part de la sous-bande
  869,4–869,65 MHz. Au-delà, une émission MeshCore est refusée (MeshCore
  l'abandonne) et la file Reticulum attend. Un hôte RNode peut en plus
  imposer sa limite long terme (`CMD_LT_ALOCK`) au seul Reticulum.

## 3. MeshCore : le répéteur, inchangé

`app/mc/` contient les trois fichiers du répéteur de MeshCore
(`examples/simple_repeater` : `MyMesh.h`, `MyMesh.cpp`, `RateLimiter.h`),
recopiés par `sync_meshcore.sh` avec une seule ligne ajoutée :
`#include "McRedirect.h"`, qui redirige `radio_driver` vers le port de
l'arbitre (`McRadioPort`). Tout le reste est celui de MeshCore : même CLI
(`set freq`, `neighbors`, `advert`, `password`…), même administration à
distance depuis l'application MeshCore, mêmes régions, même ACL. La version
annoncée est celle de MeshCore suivie de la nôtre
(`v1.17.1-arb0.1.0`) : les outils MeshCore coupent au tiret.

## 4. Reticulum : deux usages du même canal

1. **Modem RNode pour un hôte** (`RNodeHost.h`). Sur le port USB, la carte
   parle le protocole RNode que `RNS/Interfaces/RNodeInterface.py` attend :
   détection, version ≥ 1.52, plateforme, écho exact de chaque paramètre
   radio, `CMD_DATA` dans les deux sens précédé de RSSI/SNR, `CMD_READY`,
   rapports de canal, de PHY et de batterie. `rnsd`, Sideband (USB),
   MeshChat et NomadNet l'utilisent comme n'importe quel RNode — sur toutes
   les cartes, y compris celles sans place pour Reticulum embarqué. Tant
   qu'un hôte est attaché avec la radio allumée, **son** canal devient le
   canal Reticulum ; à son départ (`CMD_LEAVE` ou port fermé), le canal
   configuré revient.
2. **Nœud de transport embarqué** (`RnsStack.cpp`, microReticulum de
   `../lib/microReticulum`). Une `LoRaInterface` en mode `FULL`, transport
   activable (`arb rns transport`), table de chemins dimensionnée par
   plateforme (48 sur ESP32 sans PSRAM, 100 avec, 16 sur nRF52, 32 sur
   RP2040), identité persistante sur le système de fichiers.

Les trames reçues sur le canal Reticulum sont réassemblées une fois
(`RNodeReassembler`) puis remises aux deux.

microReticulum et MeshCore ne peuvent pas partager une unité de
compilation (la macro `SEED_SIZE` de MeshCore casse la constante du même nom
de la bibliothèque Crypto) : la pile vit seule dans `RnsStack.cpp`, derrière
une interface C++ sans en-tête de l'une ou de l'autre.

## 5. Modes

| Mode | MeshCore | Reticulum embarqué | Modem RNode (USB) |
|---|---|---|---|
| `dual` (défaut) | répéteur | transport | oui |
| `meshcore` | répéteur | — | oui (l'hôte prend le canal Reticulum) |
| `rns` | — | transport | oui |
| `rnode` | — | — | oui |

Sur STM32WL (64 Ko de RAM), Reticulum embarqué n'existe pas : `dual` y
devient `meshcore` et `rns` devient `rnode`.

## 6. Console USB

Un seul port pour trois usages, sans commutateur :

- octets entre délimiteurs `FEND` → `RNodeHost` ;
- lignes `arb …` → réglages Arborisis ;
- toute autre ligne → CLI du répéteur MeshCore, réponses préfixées `  -> `
  comme sur un répéteur MeshCore.

Le texte n'est écrit qu'entre deux trames KISS complètes, et un hôte KISS
ignore ce qui est hors trame. Une trame « ouverte » par le délimiteur
partagé et restée vide 50 ms rend la main au texte.

## 7. Plateformes

| Famille | Système de fichiers | Reticulum embarqué | Remarques |
|---|---|---|---|
| ESP32, ESP32-S3, ESP32-C3 | LittleFS (partition `spiffs`) | oui, chemins persistants | Heltec V3 : RAM 21,7 %, flash 42 % |
| ESP32-C6 | LittleFS | oui | Arduino 3.x (pioarduino), expérimental chez MeshCore |
| nRF52840 | InternalFS | oui, sans persistance des chemins | capteurs optionnels retirés pour tenir en flash (RAK4631 : 97,4 %) |
| RP2040 | LittleFS | oui | |
| STM32WL | InternalFS | non | modem RNode + répéteur MeshCore |

Sur ESP32, le système de fichiers passe de SPIFFS (MeshCore) à LittleFS
(répertoires nécessaires à Reticulum) : une carte qui portait un firmware
MeshCore recrée son identité MeshCore au premier démarrage.

## 8. Ce qui est vérifié, ce qui ne l'est pas

Vérifié dans ce dépôt :

- compilation des environnements (voir [`MATERIEL.md`](MATERIEL.md)) ;
- tests hôtes (`pio test -e arb_native`) : format RNode octet par octet
  (découpage, réassemblage, trame vide résiduelle, paquet court entre deux
  moitiés), KISS, protocole hôte RNode tel que RNS le vérifie, plan
  d'écoute, tri sur canal partagé, budget d'airtime, configuration.

**Pas encore vérifié sur matériel** : l'écoute de deux canaux par CAD, le
partage de l'émetteur, et le comportement RF des cartes. Les modes à un
seul protocole réutilisent des mécanismes éprouvés (Dispatcher de MeshCore,
format RNode) ; le mode `dual` sur deux canaux est **expérimental** tant
qu'il n'a pas été mesuré — `arb status` et la page « Radio » de l'écran
donnent le nombre de coups d'œil et de détections pour le faire.
