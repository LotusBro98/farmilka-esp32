#include <Arduino.h>
#include <BleCombo.h>

void setup() {
    Serial.begin(115200);
    Serial.println("Booting ESP32-S3 BLE Combo...");

    Keyboard.begin();
    Mouse.begin();

    Serial.println("BLE HID started. Pair this device as a keyboard/mouse.");
}

void loop() {
    // Ждём, пока хост по Bluetooth подключится
    if (!Keyboard.isConnected()) {
        delay(200);
        return;
    }

    if (Serial.available()) {
        char c = Serial.read();

        switch (c) {
            // Движение мыши
            case 'w': Mouse.move(0, -5); break;
            case 's': Mouse.move(0,  5); break;
            case 'a': Mouse.move(-5, 0); break;
            case 'd': Mouse.move( 5, 0); break;

            // Клики
            case 'l':
                Mouse.click(MOUSE_LEFT);
                break;
            case 'r':
                Mouse.click(MOUSE_RIGHT);
                break;

            // Печать строки: kтекст\n
            case 'k': {
                String text = Serial.readStringUntil('\n');
                Keyboard.print(text);
                break;
            }

            default:
                // можно добавить свои команды
                break;
        }
    }
}
