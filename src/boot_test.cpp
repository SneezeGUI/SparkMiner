#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("[BOOT-TEST] ESP32-S3 N16R8 minimal firmware is running");
}

void loop() {
    Serial.println("[BOOT-TEST] alive");
    delay(1000);
}
