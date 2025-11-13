#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BleCombo.h>

// ---------- WIFI НАСТРОЙКИ ----------
const char* WIFI_SSID = "MTS_GPON_09AC";
const char* WIFI_PASS = "6TeQGAGC";

// ---------- HTTP ----------
WebServer server(80);

// прототипы
void handlePing();
void handleMouseMove();
void handleMouseClick();
void handleMouseWheel();
void handleKeyType();
void handleKeyCombo();
void handleCombo();

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("BOOT");

  // BLE HID
  Keyboard.begin();
  Mouse.begin();
  Serial.println("BLE HID started");

  // WiFi STA
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  // HTTP маршруты
  server.on("/ping", HTTP_GET, handlePing);
  server.on("/mouse/move", HTTP_GET, handleMouseMove);
  server.on("/mouse/click", HTTP_GET, handleMouseClick);
  server.on("/mouse/wheel", HTTP_GET, handleMouseWheel);
  server.on("/key/type", HTTP_POST, handleKeyType);
  server.on("/key/combo", HTTP_GET, handleKeyCombo);
  server.on("/combo", HTTP_GET, handleCombo);

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  // Ничего больше; BLE HID живёт, пока подключен по BT
}

// ---------- Вспомогательное ----------

bool ensureBleConnected() {
  if (!Keyboard.isConnected()) {
    server.send(503, "text/plain", "BLE not connected");
    return false;
  }
  return true;
}

// ---------- Handlers ----------

void handlePing() {
  server.send(200, "text/plain", "OK");
}

void handleMouseMove() {
  if (!ensureBleConnected()) return;

  if (!server.hasArg("dx") || !server.hasArg("dy")) {
    server.send(400, "text/plain", "dx, dy required");
    return;
  }

  int dx = server.arg("dx").toInt();
  int dy = server.arg("dy").toInt();

  if (dx < -128) dx = -128;
  if (dx > 127) dx = 127;
  if (dy < -128) dy = -128;
  if (dy > 127) dy = 127;

  Mouse.move((int8_t)dx, (int8_t)dy);
  server.send(200, "text/plain", "OK");
}

void handleMouseClick() {
  if (!ensureBleConnected()) return;

  if (!server.hasArg("buttons")) {
    server.send(400, "text/plain", "buttons required");
    return;
  }

  uint8_t mask = (uint8_t)server.arg("buttons").toInt();

  if (mask & 0x01) Mouse.click(MOUSE_LEFT);
  if (mask & 0x02) Mouse.click(MOUSE_RIGHT);
  if (mask & 0x04) Mouse.click(MOUSE_MIDDLE);

  server.send(200, "text/plain", "OK");
}

void handleMouseWheel() {
  if (!ensureBleConnected()) return;

  if (!server.hasArg("v")) {
    server.send(400, "text/plain", "v required");
    return;
  }

  int v = server.arg("v").toInt();
  if (v < -128) v = -128;
  if (v > 127) v = 127;

  Mouse.move(0, 0, (int8_t)v);
  server.send(200, "text/plain", "OK");
}

void handleKeyType() {
  if (!ensureBleConnected()) return;

  if (!server.hasArg("text")) {
    server.send(400, "text/plain", "text required");
    return;
  }

  String text = server.arg("text");
  for (size_t i = 0; i < text.length(); i++) {
    Keyboard.print(text[i]);
  }

  server.send(200, "text/plain", "OK");
}

void handleKeyCombo() {
  if (!ensureBleConnected()) return;

  if (!server.hasArg("mods") || !server.hasArg("keys")) {
    server.send(400, "text/plain", "mods and keys required");
    return;
  }

  uint8_t mods = (uint8_t)server.arg("mods").toInt();
  String keysStr = server.arg("keys");

  // парсим keys=4,5,6 в массив
  uint8_t keys[6];
  uint8_t count = 0;
  int start = 0;
  while (start < (int)keysStr.length() && count < 6) {
    int comma = keysStr.indexOf(',', start);
    if (comma < 0) comma = keysStr.length();
    String sub = keysStr.substring(start, comma);
    sub.trim();
    if (sub.length() > 0) {
      keys[count++] = (uint8_t)sub.toInt();
    }
    start = comma + 1;
  }

  // модификаторы
  if (mods & 0x01) Keyboard.press(KEY_LEFT_CTRL);
  if (mods & 0x02) Keyboard.press(KEY_LEFT_SHIFT);
  if (mods & 0x04) Keyboard.press(KEY_LEFT_ALT);
  if (mods & 0x08) Keyboard.press(KEY_LEFT_GUI);
  if (mods & 0x10) Keyboard.press(KEY_RIGHT_CTRL);
  if (mods & 0x20) Keyboard.press(KEY_RIGHT_SHIFT);
  if (mods & 0x40) Keyboard.press(KEY_RIGHT_ALT);
  if (mods & 0x80) Keyboard.press(KEY_RIGHT_GUI);

  for (uint8_t i = 0; i < count; i++) {
    Keyboard.press(keys[i]);
  }

  delay(5);
  Keyboard.releaseAll();
  server.send(200, "text/plain", "OK");
}

void handleCombo() {
  if (!Keyboard.isConnected()) {
    server.send(503, "text/plain", "BLE not connected");
    return;
  }

  if (!server.hasArg("mods") || !server.hasArg("keys") || !server.hasArg("mouse")) {
    server.send(400, "text/plain", "mods, keys, mouse required");
    return;
  }

  uint8_t mods  = (uint8_t)server.arg("mods").toInt();
  String keysStr = server.arg("keys");
  uint8_t mouseMask = (uint8_t)server.arg("mouse").toInt();
  int32_t duration_ms = (int32_t)server.arg("duration_ms").toInt();
  bool release = (bool)server.arg("release").toInt();

  // --- Модификаторы ---
  if (mods & 0x01) Keyboard.press(KEY_LEFT_CTRL);
  if (mods & 0x02) Keyboard.press(KEY_LEFT_SHIFT);
  if (mods & 0x04) Keyboard.press(KEY_LEFT_ALT);
  if (mods & 0x08) Keyboard.press(KEY_LEFT_GUI);
  if (mods & 0x10) Keyboard.press(KEY_RIGHT_CTRL);
  if (mods & 0x20) Keyboard.press(KEY_RIGHT_SHIFT);
  if (mods & 0x40) Keyboard.press(KEY_RIGHT_ALT);
  if (mods & 0x80) Keyboard.press(KEY_RIGHT_GUI);

  // --- Клавиши ---
  uint8_t keys[6];
  uint8_t count = 0;
  int start = 0;
  while (start < (int)keysStr.length() && count < 6) {
    int comma = keysStr.indexOf(',', start);
    if (comma < 0) comma = keysStr.length();
    String sub = keysStr.substring(start, comma);
    sub.trim();
    if (sub.length() > 0)
      keys[count++] = sub[0];
    start = comma + 1;
  }
  for (uint8_t i = 0; i < count; i++) {
    Serial.println(keysStr);
    Keyboard.press(keys[i]);
  }

  // --- Кнопки мыши ---
  if (mouseMask & 0x01) Mouse.press(MOUSE_LEFT);
  if (mouseMask & 0x02) Mouse.press(MOUSE_RIGHT);
  if (mouseMask & 0x04) Mouse.press(MOUSE_MIDDLE);

  delay(duration_ms);  // даём хосту увидеть нажатие

  if (release) {
    Keyboard.releaseAll();
    Mouse.release(MOUSE_LEFT);
    Mouse.release(MOUSE_RIGHT);
    Mouse.release(MOUSE_MIDDLE);
  }

  server.send(200, "text/plain", "OK");
}

