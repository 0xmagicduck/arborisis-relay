# Cartes prises en charge

*Généré par `tools/matrix.py` à partir de `docs/boards.json` (`tools/gen_envs.py`)
et `docs/build-results.json` (`tools/build_matrix.py`). Ne pas éditer à la main.*

**94 environnements** pour **83 cartes** (variantes MeshCore). Compilés lors de la dernière passe : 5, réussis : **5**.

Familles : ESP32 / S3 / C3 46, ESP32-C6 5, nRF52840 35, RP2040 4, STM32WL 4.  
Radios : LR1110 6, LR2021 2, STM32WL 4, SX1262 75, SX1268 3, SX1276 4.

Reticulum sur la carte : toutes les familles sauf STM32WL (64 Ko de RAM), où la carte
reste un modem RNode pour un hôte et un répéteur MeshCore. « Écran » : la classe
d'affichage de la variante MeshCore (vide = sans écran).

| Environnement | Carte (variante) | MCU | Radio | Écran | Compilation | RAM | Flash |
|---|---|---|---|---|---|---|---|
| `arb_ebyte_eora_s3` | ebyte_eora_s3 | ESP32 / S3 / C3 | SX1262 | SSD1306Display | — |  |  |
| `arb_gat562_30s_mesh_kit` | gat562_30s_mesh_kit | nRF52840 | SX1262 | SSD1306Display | — |  |  |
| `arb_gat562_mesh_evb_pro` | gat562_mesh_evb_pro | nRF52840 | SX1262 |  | — |  |  |
| `arb_gat562_mesh_tracker_pro` | gat562_mesh_tracker_pro | nRF52840 | SX1262 | SSD1306Display | — |  |  |
| `arb_gat562_mesh_watch13` | gat562_mesh_watch13 | nRF52840 | SX1262 |  | — |  |  |
| `arb_generic_e22_sx1262` | generic-e22 | ESP32 / S3 / C3 | SX1262 |  | — |  |  |
| `arb_generic_e22_sx1268` | generic-e22 | ESP32 / S3 / C3 | SX1268 |  | — |  |  |
| `arb_heltec_ct62` | heltec_ct62 | ESP32 / S3 / C3 | SX1262 |  | — |  |  |
| `arb_heltec_e213` | heltec_e213 | ESP32 / S3 / C3 | SX1262 | E213Display | — |  |  |
| `arb_heltec_e290` | heltec_e290 | ESP32 / S3 / C3 | SX1262 | E290Display | — |  |  |
| `arb_heltec_mesh_solar` | heltec_mesh_solar | nRF52840 | SX1262 |  | — |  |  |
| `arb_heltec_rc32` | heltec_rc32 | ESP32 / S3 / C3 | SX1262 | NV3001BDisplay | — |  |  |
| `arb_heltec_rc32_without_display` | heltec_rc32 | ESP32 / S3 / C3 | SX1262 |  | — |  |  |
| `arb_heltec_t096` | heltec_t096 | nRF52840 | SX1262 | ST7735Display | — |  |  |
| `arb_heltec_t1` | heltec_t1 | nRF52840 | SX1262 | ST7735Display | — |  |  |
| `arb_heltec_t114` | heltec_t114 | nRF52840 | SX1262 | ST7789Display | — |  |  |
| `arb_heltec_t114_without_display` | heltec_t114 | nRF52840 | SX1262 |  | — |  |  |
| `arb_heltec_t190` | heltec_t190 | ESP32 / S3 / C3 | SX1262 | ST7789Display | — |  |  |
| `arb_heltec_tower_v2` | heltec_tower_v2 | nRF52840 | SX1262 |  | — |  |  |
| `arb_heltec_tracker_v2` | heltec_tracker_v2 | ESP32 / S3 / C3 | SX1262 | ST7735Display | — |  |  |
| `arb_heltec_v2` | heltec_v2 | ESP32 / S3 / C3 | SX1276 | SSD1306Display | — |  |  |
| `arb_heltec_v3` | heltec_v3 | ESP32 / S3 / C3 | SX1262 | SSD1306Display | ✅ | 29.3 % | 59.0 % |
| `arb_heltec_v4` | heltec_v4 | ESP32 / S3 / C3 | SX1262 | SSD1306Display | — |  |  |
| `arb_heltec_v4_expansionkit` | heltec_v4 | ESP32 / S3 / C3 | SX1262 | SSD1306Display | — |  |  |
| `arb_heltec_v4_r8` | heltec_v4_r8 | ESP32 / S3 / C3 | SX1262 | SSD1306Display | — |  |  |
| `arb_heltec_v4_r8_tft` | heltec_v4_r8 | ESP32 / S3 / C3 | SX1262 | ST7789LCDDisplay | — |  |  |
| `arb_heltec_v4_tft` | heltec_v4 | ESP32 / S3 / C3 | SX1262 | ST7789LCDDisplay | — |  |  |
| `arb_heltec_wireless_paper` | heltec_wireless_paper | ESP32 / S3 / C3 | SX1262 | E213Display | — |  |  |
| `arb_heltec_wireless_tracker` | heltec_tracker | ESP32 / S3 / C3 | SX1262 | ST7735Display | — |  |  |
| `arb_heltec_wsl3` | heltec_v3 | ESP32 / S3 / C3 | SX1262 |  | — |  |  |
| `arb_ikoka_handheld_nrf_e22_30dbm` | ikoka_handheld_nrf | nRF52840 | SX1262 |  | — |  |  |
| `arb_keepteenlt1` | keepteen_lt1 | nRF52840 | SX1262 | SSD1306Display | — |  |  |
| `arb_lilygo_t3s3_sx1262` | lilygo_t3s3 | ESP32 / S3 / C3 | SX1262 | SSD1306Display | — |  |  |
| `arb_lilygo_t3s3_sx1276` | lilygo_t3s3_sx1276 | ESP32 / S3 / C3 | SX1276 | SSD1306Display | — |  |  |
| `arb_lilygo_t_echo` | lilygo_techo | nRF52840 | SX1262 | GxEPDDisplay | — |  |  |
| `arb_lilygo_t_echo_card` | lilygo_techo_card | nRF52840 | SX1262 | U8g2Display | — |  |  |
| `arb_lilygo_t_echo_lite` | lilygo_techo_lite | nRF52840 | SX1262 | GxEPDDisplay | — |  |  |
| `arb_lilygo_t_impulse_plus` | lilygo_t_impulse_plus | nRF52840 | SX1262 |  | — |  |  |
| `arb_lilygo_tbeam_1w` | lilygo_tbeam_1w | ESP32 / S3 / C3 | SX1262 | SH1106Display | — |  |  |
| `arb_lilygo_tdeck` | lilygo_tdeck | ESP32 / S3 / C3 | SX1262 | ST7789LCDDisplay | — |  |  |
| `arb_lilygo_teth_elite_sx1262` | lilygo_teth_elite | ESP32 / S3 / C3 | SX1262 |  | — |  |  |
| `arb_lilygo_tlora_c6` | lilygo_tlora_c6 | ESP32-C6 | SX1262 |  | — |  |  |
| `arb_lilygo_tlora_v2_1_1_6` | lilygo_tlora_v2_1 | ESP32 / S3 / C3 | SX1276 | SSD1306Display | — |  |  |
| `arb_m5stack_unit_c6l` | m5stack_unit_c6l | ESP32-C6 | SX1262 |  | — |  |  |
| `arb_mesh_pocket` | mesh_pocket | nRF52840 | SX1262 | GxEPDDisplay | — |  |  |
| `arb_meshadventurer_sx1262` | meshadventurer | ESP32 / S3 / C3 | SX1262 | SSD1306Display | — |  |  |
| `arb_meshadventurer_sx1268` | meshadventurer | ESP32 / S3 / C3 | SX1268 | SSD1306Display | — |  |  |
| `arb_meshimi` | xiao_c6 | ESP32-C6 | SX1262 |  | — |  |  |
| `arb_meshnology_w12` | meshnology_w12 | ESP32 / S3 / C3 | LR2021 | SSD1306Display | — |  |  |
| `arb_meshtiny` | meshtiny | nRF52840 | SX1262 |  | — |  |  |
| `arb_meshtracker_x1` | meshtracker_x1 | nRF52840 | LR2021 |  | — |  |  |
| `arb_minewsemi_me25ls01` | minewsemi_me25ls01 | nRF52840 | LR1110 | NullDisplayDriver | — |  |  |
| `arb_nano_g2_ultra` | nano_g2_ultra | nRF52840 | SX1262 | SH1106Display | — |  |  |
| `arb_nibble_screen_connect` | nibble_screen_connect | ESP32 / S3 / C3 | SX1262 | SSD1306Display | — |  |  |
| `arb_nibble_zero_connect` | nibble_zero_connect | ESP32 / S3 / C3 | SX1262 | SSD1306Display | — |  |  |
| `arb_picow` | rpi_picow | RP2040 | SX1262 |  | — |  |  |
| `arb_promicro` | promicro | nRF52840 | SX1262 | SSD1306Display | — |  |  |
| `arb_r1neo` | muziworks_r1_neo | nRF52840 | SX1262 | NullDisplayDriver | — |  |  |
| `arb_rak_11310` | rak11310 | RP2040 | SX1262 |  | — |  |  |
| `arb_rak_3112` | rak3112 | ESP32 / S3 / C3 | SX1262 |  | — |  |  |
| `arb_rak_3401` | rak3401 | nRF52840 | SX1262 | SSD1306Display | — |  |  |
| `arb_rak_3x72` | rak3x72 | STM32WL | STM32WL |  | — |  |  |
| `arb_rak_4631` | rak4631 | nRF52840 | SX1262 | SSD1306Display | ✅ | 20.6 % | 97.7 % |
| `arb_rak_wismesh_tag` | rak_wismesh_tag | nRF52840 | SX1262 | NullDisplayDriver | — |  |  |
| `arb_sensecap_solar` | sensecap_solar | nRF52840 | SX1262 |  | — |  |  |
| `arb_station_g2` | station_g2 | ESP32 / S3 / C3 | SX1262 | SH1106Display | — |  |  |
| `arb_station_g3_esp32` | station_g3_esp32 | ESP32 / S3 / C3 | SX1262 | SH1106Display | — |  |  |
| `arb_t1000e` | t1000-e | nRF52840 | LR1110 |  | — |  |  |
| `arb_t_beam_s3_supreme_sx1262` | lilygo_tbeam_supreme_SX1262 | ESP32 / S3 / C3 | SX1262 | SH1106Display | — |  |  |
| `arb_tbeam_sx1262` | lilygo_tbeam_SX1262 | ESP32 / S3 / C3 | SX1262 | SSD1306Display | — |  |  |
| `arb_tbeam_sx1276` | lilygo_tbeam_SX1276 | ESP32 / S3 / C3 | SX1276 | SSD1306Display | — |  |  |
| `arb_tenstar_c3_sx1262` | tenstar_c3 | ESP32 / S3 / C3 | SX1262 |  | — |  |  |
| `arb_tenstar_c3_sx1268` | tenstar_c3 | ESP32 / S3 / C3 | SX1268 |  | — |  |  |
| `arb_thinknode_m1` | thinknode_m1 | nRF52840 | SX1262 |  | — |  |  |
| `arb_thinknode_m2` | thinknode_m2 | ESP32 / S3 / C3 | SX1262 | SH1106Display | — |  |  |
| `arb_thinknode_m3` | thinknode_m3 | nRF52840 | LR1110 |  | — |  |  |
| `arb_thinknode_m5` | thinknode_m5 | ESP32 / S3 / C3 | SX1262 | GxEPDDisplay | — |  |  |
| `arb_thinknode_m6` | thinknode_m6 | nRF52840 | SX1262 |  | — |  |  |
| `arb_thinknode_m7` | thinknode_m7 | ESP32 / S3 / C3 | LR1110 |  | — |  |  |
| `arb_thinknode_m9` | thinknode_m9 | ESP32 / S3 / C3 | LR1110 | ST7789Display | — |  |  |
| `arb_tiny_relay` | tiny_relay | STM32WL | STM32WL |  | — |  |  |
| `arb_waveshare_rp2040_lora` | waveshare_rp2040_lora | RP2040 | SX1262 |  | — |  |  |
| `arb_why2025_badge` | xiao_c6 | ESP32-C6 | SX1262 |  | — |  |  |
| `arb_wio_e5` | wio-e5-dev | STM32WL | STM32WL |  | — |  |  |
| `arb_wio_e5_mini` | wio-e5-mini | STM32WL | STM32WL |  | ✅ | 43.4 % | 95.8 % |
| `arb_wio_wm1110` | wio_wm1110 | nRF52840 | LR1110 |  | — |  |  |
| `arb_wiotrackerl1` | wio-tracker-l1 | nRF52840 | SX1262 | SH1106Display | — |  |  |
| `arb_wiotrackerl1eink` | wio-tracker-l1-eink | nRF52840 | SX1262 |  | — |  |  |
| `arb_xiao_c3` | xiao_c3 | ESP32 / S3 / C3 | SX1262 |  | ✅ | 26.8 % | 65.1 % |
| `arb_xiao_c6` | xiao_c6 | ESP32-C6 | SX1262 |  | ✅ | 20.8 % | 52.3 % |
| `arb_xiao_nrf52` | xiao_nrf52 | nRF52840 | SX1262 | NullDisplayDriver | — |  |  |
| `arb_xiao_rp2040` | xiao_rp2040 | RP2040 | SX1262 |  | — |  |  |
| `arb_xiao_s3` | xiao_s3 | ESP32 / S3 / C3 | SX1262 |  | — |  |  |
| `arb_xiao_s3_wio` | xiao_s3_wio | ESP32 / S3 / C3 | SX1262 |  | — |  |  |
