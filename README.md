# ESP32 Home Assistant Control Panel

Firmware **multi-cartes** (PlatformIO) transformant les écrans tactiles AMOLED
Waveshare en panneau de contrôle tactile pour **Home Assistant** : connexion
WiFi + HA, découverte des entités, sélection via portail web, contrôle tactile
avec mise à jour d'état **temps réel**.

## Cartes supportées

| Carte | Résolution | Écran | Tactile | État |
|---|---|---|---|---|
| ESP32-S3 Touch AMOLED 1.8" | 368×448 | CO5300 (QSPI) | CST816 @0x15 | MVP (référence) |
| ESP32-C6 Touch AMOLED 1.8" | 368×448 | SH8601 (QSPI) | FT3168 @0x38 | profil prêt |
| 2.16" / 1.75" / 2.41" | 480×480 / 466×466 / 536×240 | CO5300 / RM690B0 | à vérifier | à ajouter |

> Pins, contrôleurs et mapping de l'expandeur **vérifiés sur le code de démo
> officiel Waveshare** (`pin_config.h` + exemples GFX). Les cartes S3 et C6 1.8"
> diffèrent sur **les deux** contrôleurs (écran *et* tactile) et sur le pinout —
> le HAL gère cette diversité via les profils.
>
> ⚠️ Une révision **v1** de la carte S3 1.8" utilise SH8601 + FT3168 (@0x38). Le
> **scan I²C au démarrage** (flag `-D DEBUG_I2C_SCAN`, activé par défaut sur S3)
> lève le doute : `0x15` ⇒ CST816 (profil actuel), `0x38` ⇒ FT3168 (basculer
> `DISPLAY_DRIVER_*` et `TOUCH_I2C_ADDR`).

Le reset de l'écran et du tactile n'est pas câblé sur un GPIO mais sur
l'expandeur **TCA9554** (@0x20, EXIO 0/1/2, bus I²C partagé). Le driver
[Tca9554](src/board/io/Tca9554.h) génère la séquence de reset avant l'init du
panneau ([Display.cpp](src/board/Display.cpp) → `resetPanel()`).

## Architecture

Couches indépendantes, l'applicatif ne dépend jamais d'une carte précise :

```
ui/   (LVGL)         écrans boot / setup / dashboard, responsive
ha/   (HAClient)     WebSocket API native HA + EntityStore
net/  (WiFi/portal)  WiFiManager + portail web + stockage NVS/LittleFS
board/(HAL)          BoardConfig + Display (Arduino_GFX) + Touch (ITouch)
core/                types de config partagés
```

Choix techniques : **WebSocket API native HA** (push temps réel, pas de broker),
en **ws:// (clair) ou wss:// (TLS)** au choix, **portail web** pour la config,
**LVGL 8.x** + **Arduino_GFX**, **boucle coopérative** (compatible C6 mono-cœur
et S3 bi-cœur).

> Le TLS utilise le mode *insecure* (pas de validation de certificat), adapté à
> un LAN de confiance / aux certificats auto-signés de HA et à Nabu Casa.

## Compilation & flash

Plateforme : **pioarduino** (Arduino-ESP32 3.x / IDF 5.x, requis pour le C6).

```bash
# Carte de référence (S3 1.8")
pio run -e s3_amoled_18
pio run -e s3_amoled_18 -t upload -t monitor

# Autre carte
pio run -e c6_amoled_18 -t upload
```

## Première utilisation

1. Au 1er démarrage, l'écran affiche « Setup required » et la carte crée un
   point d'accès WiFi **`HA-Panel-Setup`**.
2. S'y connecter : le portail captif s'ouvre → saisir le WiFi + l'**hôte/IP**,
   le **port**, un **token longue durée** Home Assistant, et cocher
   **« Use HTTPS/WSS (TLS) »** si HA est exposé en HTTPS.
   (Token : profil HA → *Long-lived access tokens* → *Create token*.)
3. La carte se connecte à HA, découvre les entités `light`/`switch`.
4. Ouvrir `http://<ip-de-la-carte>/` pour **cocher les entités** à afficher.
5. Le tableau de bord affiche les cartes ; toggles et sliders contrôlent HA en
   temps réel, et tout changement côté HA se reflète à l'écran.

## Ajouter une carte

1. `src/board/profiles/<name>.h` : géométrie, pins, drivers écran/tactile.
2. Une branche dans `src/board/BoardConfig.h`.
3. Un `[env:<name>]` dans `platformio.ini` avec `-D BOARD_<NAME>`.
4. Si nouveau contrôleur tactile : une implémentation `ITouch` dans `Touch.cpp`.

Aucune modification de `ui/`, `ha/` ou `net/` n'est nécessaire.

## Feuille de route

MVP : lumières (on/off + variation) et interrupteurs. Phase 2 : capteurs
(lecture seule), couleur des lumières, scènes/scripts, portage des autres dalles.
