#pragma once

#include <Arduino.h>

#define LOGI(fmt, ...) Serial.printf("[%10lu] [I] " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGW(fmt, ...) Serial.printf("[%10lu] [W] " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGE(fmt, ...) Serial.printf("[%10lu] [E] " fmt "\n", millis(), ##__VA_ARGS__)
