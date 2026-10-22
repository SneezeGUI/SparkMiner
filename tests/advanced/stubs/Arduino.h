#pragma once
#include <cstdint>
#include <mutex>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
#define pdMS_TO_TICKS(n) (n)
#define pdPASS 1
using TaskHandle_t = void*;
using TaskFunction_t = void(*)(void*);
extern TaskFunction_t test_task;
struct StopTask {};
inline int xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, int, void*, int, TaskHandle_t* handle, int) {
    test_task=fn;*handle=reinterpret_cast<void*>(1);return pdPASS;
}
inline void vTaskDelay(int) { throw StopTask(); }
struct TestSerial { void println(const char*) {} };
extern TestSerial Serial;
