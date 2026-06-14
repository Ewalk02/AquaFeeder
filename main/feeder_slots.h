#pragma once

#include <stdbool.h>

#define FEEDER_MAX_SLOTS      12
#define FEEDER_MISSED_GRACE_S 300

int feeder_window_minutes(int start_min, int end_min);
int feeder_slot_minute(int slot, int start_min, int end_min, int times);
bool feeder_in_feeding_window(int now_min, int start_min, int end_min, int times);
