// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// The ESP32 half follows RNode's BLESerial (../../BLESerial.cpp): the same
// service, the same security (static passkey, LE Secure Connections with
// MITM protection, bonding), so a phone that pairs with an RNode pairs with
// this the same way.

#include "BleLink.h"

#if ARB_WITH_BLE

namespace arb {

void BleLink::onRx(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    size_t next = (_rx_head + 1) % RX_CAP;
    if (next == _rx_tail) break;          // full: the host will time out and retry
    _rx[_rx_head] = data[i];
    _rx_head = next;
  }
}

int BleLink::available() {
  size_t h = _rx_head, t = _rx_tail;
  return (int)((h + RX_CAP - t) % RX_CAP);
}

int BleLink::read() {
  if (_rx_head == _rx_tail) return -1;
  uint8_t b = _rx[_rx_tail];
  _rx_tail = (_rx_tail + 1) % RX_CAP;
  return b;
}

void BleLink::put(uint8_t b) {
  if (_tx_n < TX_CAP) _tx[_tx_n++] = b;
}

}  // namespace arb

// ── ESP32 family ───────────────────────────────────────────────────────────
#if defined(ESP32)

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

namespace arb {

namespace {

const char* NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const char* NUS_RX      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const char* NUS_TX      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

BLEServer* g_server = nullptr;
BLECharacteristic* g_tx = nullptr;
BleLink* g_link = nullptr;
uint32_t g_pin = 0;

class Callbacks : public BLEServerCallbacks, public BLECharacteristicCallbacks, public BLESecurityCallbacks {
public:
  void onConnect(BLEServer*) override { if (g_link) g_link->onConnect(); }
  void onDisconnect(BLEServer* s) override {
    if (g_link) g_link->onDisconnect();
    s->startAdvertising();
  }
  void onWrite(BLECharacteristic* c) override {
    if (g_link) g_link->onRx(c->getData(), c->getLength());
  }
  uint32_t onPassKeyRequest() override { return g_pin; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t pin) override { return pin == g_pin; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t r) override {
    if (g_link) g_link->onAuthenticated(r.success);
  }
};

Callbacks g_cb;

}  // namespace

bool BleLink::begin(const char* name, uint32_t pin) {
  g_link = this;
  g_pin = pin;
  BLEDevice::init(name);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
  BLEDevice::setSecurityCallbacks(&g_cb);

  esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;
  esp_ble_io_cap_t iocap = ESP_IO_CAP_OUT;          // we show the passkey
  uint8_t key_size = 16;
  uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t auth_option = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_ENABLE;
  uint32_t passkey = pin;
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(uint32_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH, &auth_option, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

  g_server = BLEDevice::createServer();
  g_server->setCallbacks(&g_cb);
  BLEService* svc = g_server->createService(NUS_SERVICE);
  BLECharacteristic* rx = svc->createCharacteristic(NUS_RX, BLECharacteristic::PROPERTY_WRITE |
                                                            BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  rx->setCallbacks(&g_cb);
  g_tx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  g_tx->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  g_tx->addDescriptor(new BLE2902());
  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x20);
  adv->setMaxPreferred(0x40);
  adv->start();
  _running = true;
  return true;
}

bool BleLink::connected() { return _running && _connected; }

void BleLink::flush() {
  if (!connected() || !_authenticated || !g_tx) { _tx_n = 0; return; }
  // Notifications of up to MTU − 3 bytes; the negotiated MTU is at least 23.
  uint16_t mtu = BLEDevice::getMTU();
  size_t chunk = mtu > 23 ? mtu - 3 : 20;
  if (chunk > 512) chunk = 512;
  for (size_t off = 0; off < _tx_n; off += chunk) {
    size_t n = _tx_n - off < chunk ? _tx_n - off : chunk;
    g_tx->setValue(_tx + off, n);
    g_tx->notify();
  }
  _tx_n = 0;
}

}  // namespace arb

// ── nRF52840 ───────────────────────────────────────────────────────────────
#elif defined(NRF52_PLATFORM)

#include <bluefruit.h>

namespace arb {

namespace {

BLEUart g_uart(2048);
BleLink* g_link = nullptr;

void bfConnect(uint16_t) { if (g_link) g_link->onConnect(); }
void bfDisconnect(uint16_t, uint8_t) { if (g_link) g_link->onDisconnect(); }
void bfSecured(uint16_t) { if (g_link) g_link->onAuthenticated(true); }
void bfRx(uint16_t) {
  uint8_t buf[64];
  while (g_uart.available()) {
    int n = g_uart.read(buf, sizeof(buf));
    if (n <= 0) break;
    if (g_link) g_link->onRx(buf, n);
  }
}

}  // namespace

bool BleLink::begin(const char* name, uint32_t pin) {
  g_link = this;
  char charpin[12];
  snprintf(charpin, sizeof(charpin), "%06lu", (unsigned long)pin);
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  if (!Bluefruit.begin(1, 0)) return false;
  Bluefruit.setTxPower(4);
  Bluefruit.setName(name);
  Bluefruit.Security.setMITM(true);
  Bluefruit.Security.setPIN(charpin);
  Bluefruit.Security.setIOCaps(true, false, false);   // display only: the passkey is shown
  Bluefruit.Security.setSecuredCallback(bfSecured);
  Bluefruit.Periph.setConnectCallback(bfConnect);
  Bluefruit.Periph.setDisconnectCallback(bfDisconnect);
  g_uart.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
  g_uart.begin();
  g_uart.setRxCallback(bfRx);
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(g_uart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
  _running = true;
  return true;
}

bool BleLink::connected() { return _running && _connected; }

void BleLink::flush() {
  if (connected() && _authenticated && _tx_n) g_uart.write(_tx, _tx_n);
  _tx_n = 0;
}

}  // namespace arb

#else
  #error "ARB_WITH_BLE on a platform without a BLE implementation"
#endif

#else  // !ARB_WITH_BLE

namespace arb {
bool BleLink::begin(const char*, uint32_t) { return false; }
bool BleLink::connected() { return false; }
int BleLink::available() { return 0; }
int BleLink::read() { return -1; }
void BleLink::put(uint8_t) {}
void BleLink::flush() {}
void BleLink::onRx(const uint8_t*, size_t) {}
}  // namespace arb

#endif
