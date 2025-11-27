#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"

USBHIDKeyboard Keyboard;
USBHIDMouse    Mouse;

// буфер для входящей строки из COM-порта
String inputLine;

// прототип
void handleCommand(const String &line);

void setup() {
  // Это UART -> USB мост (порт, который ты видишь как COMx в Windows)
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("BOOT (USB HID + Serial command)");

  // Нативный USB как HID
  USB.begin();       // поднимает устройство USB (HID + CDC, зависит от настроек платы)
  Keyboard.begin();
  Mouse.begin();
  Serial.println("USB HID started");
}

void loop() {
  // Читаем строки из COM-порта
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;   // игнорируем CR
    if (c == '\n') {
      if (inputLine.length() > 0) {
        handleCommand(inputLine);
        inputLine = "";
      }
    } else {
      if (inputLine.length() < 256) {
        inputLine += c;
      }
    }
  }
}

// ===== Разбор и выполнение команд =====
//
// Формат команд (одна команда в строке):
//   PING
//   MOVE dx dy
//   CLICK mask
//   WHEEL v
//   TYPE текст...
//   KCOMBO mods key1,key2,...
//   COMBO mods key1,key2,... mouseMask duration_ms releaseFlag
//
// Всё разделено пробелами, числа десятичные.

void handleCommand(const String &line) {
  String s = line;
  s.trim();
  if (s.length() == 0) return;

  // отделяем команду от аргументов
  int sp = s.indexOf(' ');
  String cmd  = (sp < 0) ? s : s.substring(0, sp);
  String args = (sp < 0) ? "" : s.substring(sp + 1);
  cmd.toUpperCase();

  if (cmd == "PING") {
    Serial.println("OK");
    return;
  }

  if (cmd == "MOVE") {
    // MOVE dx dy
    int sp1 = args.indexOf(' ');
    if (sp1 < 0) {
      Serial.println("ERR MOVE needs dx dy");
      return;
    }
    String sdx = args.substring(0, sp1);
    String sdy = args.substring(sp1 + 1);
    int dx = sdx.toInt();
    int dy = sdy.toInt();
    if (dx < -128) dx = -128;
    if (dx > 127)  dx = 127;
    if (dy < -128) dy = -128;
    if (dy > 127)  dy = 127;
    Mouse.move((int8_t)dx, (int8_t)dy);
    Serial.println("OK");
    return;
  }

  if (cmd == "CLICK") {
    // CLICK mask (1 = L, 2 = R, 4 = M)
    int mask = args.toInt();
    if (mask & 0x01) Mouse.click(MOUSE_LEFT);
    if (mask & 0x02) Mouse.click(MOUSE_RIGHT);
    if (mask & 0x04) Mouse.click(MOUSE_MIDDLE);
    Serial.println("OK");
    return;
  }

  if (cmd == "WHEEL") {
    // WHEEL v
    int v = args.toInt();
    if (v < -128) v = -128;
    if (v > 127)  v = 127;
    Mouse.move(0, 0, (int8_t)v);
    Serial.println("OK");
    return;
  }

  if (cmd == "TYPE") {
    // TYPE текст...
    // Всё, что после пробела — печатаем как есть
    Keyboard.print(args);
    Serial.println("OK");
    return;
  }

  if (cmd == "KCOMBO") {
    // KCOMBO mods key1,key2,...
    // mods — битовая маска модификаторов
    // keyN — HID-коды (например, 0x04 = 'A', смотри таблицу)
    int sp1 = args.indexOf(' ');
    if (sp1 < 0) {
      Serial.println("ERR KCOMBO needs mods and keys");
      return;
    }
    String smods = args.substring(0, sp1);
    String skeys = args.substring(sp1 + 1);

    uint8_t mods = (uint8_t)smods.toInt();

    // модификаторы
    if (mods & 0x01) Keyboard.press(KEY_LEFT_CTRL);
    if (mods & 0x02) Keyboard.press(KEY_LEFT_SHIFT);
    if (mods & 0x04) Keyboard.press(KEY_LEFT_ALT);
    if (mods & 0x08) Keyboard.press(KEY_LEFT_GUI);
    if (mods & 0x10) Keyboard.press(KEY_RIGHT_CTRL);
    if (mods & 0x20) Keyboard.press(KEY_RIGHT_SHIFT);
    if (mods & 0x40) Keyboard.press(KEY_RIGHT_ALT);
    if (mods & 0x80) Keyboard.press(KEY_RIGHT_GUI);

    // парсим key1,key2,...
    uint8_t keys[6];
    uint8_t count = 0;
    int start = 0;
    while (start < (int)skeys.length() && count < 6) {
      int comma = skeys.indexOf(',', start);
      if (comma < 0) comma = skeys.length();
      String sub = skeys.substring(start, comma);
      sub.trim();
      if (sub.length() > 0) {
        keys[count++] = (uint8_t)sub.toInt();
      }
      start = comma + 1;
    }

    for (uint8_t i = 0; i < count; i++) {
      Keyboard.press(keys[i]);
    }

    delay(5);
    Keyboard.releaseAll();
    Serial.println("OK");
    return;
  }

  if (cmd == "COMBO") {
    // COMBO mods key1,key2,... mouseMask duration_ms releaseFlag
    // Пример: COMBO 1 4,5 1 50 1
    //   mods=1 (LEFT_CTRL), keys=4,5, mouseMask=1 (ЛКМ), duration=50ms, release=1
    // Парсим по шагам
    int sp1 = args.indexOf(' ');
    if (sp1 < 0) { Serial.println("ERR COMBO"); return; }
    String smods = args.substring(0, sp1);

    int sp2 = args.indexOf(' ', sp1 + 1);
    if (sp2 < 0) { Serial.println("ERR COMBO"); return; }
    String skeys = args.substring(sp1 + 1, sp2);

    int sp3 = args.indexOf(' ', sp2 + 1);
    if (sp3 < 0) { Serial.println("ERR COMBO"); return; }
    String smouse = args.substring(sp2 + 1, sp3);

    int sp4 = args.indexOf(' ', sp3 + 1);
    if (sp4 < 0) { Serial.println("ERR COMBO"); return; }
    String sdur = args.substring(sp3 + 1, sp4);

    String srel = args.substring(sp4 + 1);

    uint8_t mods  = (uint8_t)smods.toInt();
    uint8_t mouseMask = (uint8_t)smouse.toInt();
    int32_t duration_ms = (int32_t)sdur.toInt();
    bool release = (srel.toInt() != 0);

    // модификаторы
    if (mods & 0x01) Keyboard.press(KEY_LEFT_CTRL);
    if (mods & 0x02) Keyboard.press(KEY_LEFT_SHIFT);
    if (mods & 0x04) Keyboard.press(KEY_LEFT_ALT);
    if (mods & 0x08) Keyboard.press(KEY_LEFT_GUI);
    if (mods & 0x10) Keyboard.press(KEY_RIGHT_CTRL);
    if (mods & 0x20) Keyboard.press(KEY_RIGHT_SHIFT);
    if (mods & 0x40) Keyboard.press(KEY_RIGHT_ALT);
    if (mods & 0x80) Keyboard.press(KEY_RIGHT_GUI);

    // клавиши (тут предполагаем, что это HID-коды,
    // но можно сделать вариант с ASCII, как у тебя)
    uint8_t keys[6];
    uint8_t count = 0;
    int start = 0;
    while (start < (int)skeys.length() && count < 6) {
      int comma = skeys.indexOf(',', start);
      if (comma < 0) comma = skeys.length();
      String sub = skeys.substring(start, comma);
      sub.trim();
      if (sub.length() > 0)
        keys[count++] = (uint8_t)sub.toInt();
      start = comma + 1;
    }
    for (uint8_t i = 0; i < count; i++) {
      Keyboard.press(keys[i]);
    }

    // мышь
    if (mouseMask & 0x01) Mouse.press(MOUSE_LEFT);
    if (mouseMask & 0x02) Mouse.press(MOUSE_RIGHT);
    if (mouseMask & 0x04) Mouse.press(MOUSE_MIDDLE);

    delay(duration_ms);

    if (release) {
      Keyboard.releaseAll();
      Mouse.release(MOUSE_LEFT);
      Mouse.release(MOUSE_RIGHT);
      Mouse.release(MOUSE_MIDDLE);
    }

    Serial.println("OK");
    return;
  }

  // если команда неизвестна
  Serial.print("ERR unknown cmd: ");
  Serial.println(cmd);
}
