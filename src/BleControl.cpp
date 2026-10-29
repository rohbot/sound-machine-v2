#include "BleControl.h"
#include <NimBLEDevice.h>
#include <cstring>

namespace ble {
namespace {
QueueHandle_t requests = nullptr;
NimBLECharacteristic* state = nullptr;
NimBLECharacteristic* events = nullptr;

void event(const char* text, uint16_t connection = BLE_HS_CONN_HANDLE_NONE) {
  if (!events) return;
  events->setValue(text);
  // Pass an explicit payload: another callback may update the readable event value.
  events->notify(reinterpret_cast<const uint8_t*>(text), std::strlen(text), connection);
}

class Commands : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    const auto value = characteristic->getValue();
    if (!value.size() || value.size() > 20) { event("error:size", info.getConnHandle()); return; }
    Request request{};
    request.connection = info.getConnHandle();
    request.length = value.size();
    std::memcpy(request.text, value.data(), value.size());
    // Only queue here. The main loop owns settings, LED output, and flash writes.
    if (xQueueSend(requests, &request, 0) != pdTRUE) event("error:busy", request.connection);
  }
} callbacks;
} // namespace

bool begin(const char* initialState) {
  requests = xQueueCreate(8, sizeof(Request));
  if (!requests || !NimBLEDevice::init("SoundMachine")) return false;
  auto* server = NimBLEDevice::createServer();
  if (!server) return false;
  server->advertiseOnDisconnect(true);
  auto* service = server->createService(SERVICE);
  if (!service) return false;
  auto* command = service->createCharacteristic(COMMAND, NIMBLE_PROPERTY::WRITE, 20);
  state = service->createCharacteristic(STATE, NIMBLE_PROPERTY::READ, 512);
  events = service->createCharacteristic(EVENT, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 20);
  if (!command || !state || !events) return false;
  command->setCallbacks(&callbacks);
  command->createDescriptor("2901", NIMBLE_PROPERTY::READ)->setValue("Command: name=value");
  state->createDescriptor("2901", NIMBLE_PROPERTY::READ)->setValue("Current settings (JSON)");
  events->createDescriptor("2901", NIMBLE_PROPERTY::READ)->setValue("Command results / changes");
  state->setValue(initialState);
  events->setValue("ready");
  if (!server->start()) return false;
  auto* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE);
  advertising->enableScanResponse(true);
  advertising->setName("SoundMachine");
  return advertising->start();
}

bool next(Request& request) {
  return requests && xQueueReceive(requests, &request, 0) == pdTRUE;
}

void stateChanged(const char* json) {
  if (!state) return;
  state->setValue(json);
  event("changed");
}

void reply(const Request& request, const char* error) {
  if (error) { event(error, request.connection); return; }
  char result[24];
  snprintf(result, sizeof(result), "ok:%s", request.text);
  event(result, request.connection);
}
} // namespace ble
