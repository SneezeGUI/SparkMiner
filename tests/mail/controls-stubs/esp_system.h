#pragma once
#include <cstdint>
inline uint32_t esp_random(){static uint32_t n=100;return ++n;}
