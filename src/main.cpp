#include <Arduino.h>
#include <BleCombo.h>

enum Command : uint8_t {
  CMD_MOUSE_MOVE   = 0x01,
  CMD_MOUSE_CLICK  = 0x02,
  CMD_MOUSE_WHEEL  = 0x03,
  CMD_KEY_COMBO    = 0x10,
  CMD_KEY_RELEASE  = 0x11,
  CMD_TYPE_TEXT    = 0x12,
};

static const uint8_t START_BYTE = 0xAA;

// хелпер для модификаторов (как в HID)
void pressModifiers(uint8_t mods) {
  if (mods & 0x01) Keyboard.press(KEY_LEFT_CTRL);
  if (mods & 0x02) Keyboard.press(KEY_LEFT_SHIFT);
  if (mods & 0x04) Keyboard.press(KEY_LEFT_ALT);
  if (mods & 0x08) Keyboard.press(KEY_LEFT_GUI);
  if (mods & 0x10) Keyboard.press(KEY_RIGHT_CTRL);
  if (mods & 0x20) Keyboard.press(KEY_RIGHT_SHIFT);
  if (mods & 0x40) Keyboard.press(KEY_RIGHT_ALT);
  if (mods & 0x80) Keyboard.press(KEY_RIGHT_GUI);
}

struct FrameParser {
  uint8_t state = 0;
  uint8_t len = 0;
  uint8_t cmd = 0;
  uint8_t buf[256];
  uint8_t index = 0;

  void reset() {
    state = 0;
    len = 0;
    cmd = 0;
    index = 0;
  }

  void feed(uint8_t b) {
    switch (state) {
      case 0: // ждём START
        if (b == START_BYTE) state = 1;
        break;

      case 1: // LEN
        len = b;
        if (len == 0) reset();
        else state = 2;
        break;

      case 2: // CMD
        cmd = b;
        index = 0;
        if (len == 1) state = 4;    // только CMD
        else state = 3;             // есть payload
        break;

      case 3: // PAYLOAD
        buf[index++] = b;
        if (index + 1 == len) {
          state = 4; // дальше checksum
        }
        break;

      case 4: { // CHECKSUM
        uint8_t calc = len ^ cmd;
        for (uint8_t i = 0; i < index; i++) calc ^= buf[i];

        if (calc == b) {
          handleFrame(cmd, buf, index);
        }
        // вне зависимости от успеха сбрасываем состояние
        reset();
        break;
      }
    }
  }

  static void handleFrame(uint8_t cmd, uint8_t *data, uint8_t size) {
    switch (cmd) {
      case CMD_MOUSE_MOVE:
        if (size >= 2) {
          int8_t dx = (int8_t)data[0];
          int8_t dy = (int8_t)data[1];
          Mouse.move(dx, dy); // BlynkGO: move(x, y, wheel=0)
        }
        break;

      case CMD_MOUSE_CLICK:
        if (size >= 1) {
          uint8_t mask = data[0];
          if (mask & 0x01) Mouse.click(MOUSE_LEFT);
          if (mask & 0x02) Mouse.click(MOUSE_RIGHT);
          if (mask & 0x04) Mouse.click(MOUSE_MIDDLE);
        }
        break;

      case CMD_MOUSE_WHEEL:
        if (size >= 1) {
          int8_t v = (int8_t)data[0];
          Mouse.move(0, 0, v); // BlynkGO: третий параметр = вертикальный скролл
        }
        break;

      case CMD_KEY_COMBO:
        if (size >= 2) {
          uint8_t mods  = data[0];
          uint8_t count = data[1];
          if (2 + count <= size && count <= 6) {
            // mods → модификаторы
            pressModifiers(mods);
            // keyX → HID keycodes (KEY_*)
            for (uint8_t i = 0; i < count; i++) {
              uint8_t keycode = data[2 + i];
              Keyboard.press(keycode);
            }
            delay(5);            // дать хосту увидеть нажатие
            Keyboard.releaseAll();
          }
        }
        break;

      case CMD_KEY_RELEASE:
        Keyboard.releaseAll();
        break;

      case CMD_TYPE_TEXT:
        if (size >= 1) {
          uint8_t textLen = data[0];
          if (textLen > 0 && 1 + textLen <= size) {
            for (uint8_t i = 0; i < textLen; i++) {
              char c = (char)data[1 + i];
              Keyboard.print(c);
            }
          }
        }
        break;

      default:
        // неизвестная команда — игнор
        break;
    }
  }
};

FrameParser parser;

void setup() {
  Serial.begin(115200);
  Serial.println("BOOT OK");
  Keyboard.begin();
  Mouse.begin();
}

void loop() {
  if (!Keyboard.isConnected()) {
    delay(50);
    return;
  }

  while (Serial.available()) {
    parser.feed((uint8_t)Serial.read());
  }
}
