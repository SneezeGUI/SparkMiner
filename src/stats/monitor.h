/*
 * SparkMiner - Monitor Task
 * Coordinates display updates and live stats fetching
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <Arduino.h>
#include "../display/display.h"

/**
 * Initialize monitor subsystem
 */
void monitor_init();

/**
 * Monitor task (runs on Core 0)
 * Updates display and fetches live stats periodically
 */
void monitor_task(void *param);

/**
 * Reset screen timeout activity timer
 * Call on user input (button press, touch) to keep screen on
 */
void monitor_reset_activity();

/**
 * Copy the last display snapshot into caller-provided buffer.
 * @param out pointer to display_data_t to fill
 */
void monitor_get_display_snapshot(display_data_t *out);

#endif // MONITOR_H
