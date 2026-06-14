#include "feeder_slots.h"

int feeder_window_minutes(int start_min, int end_min)
{
    if (start_min < end_min) {
        return end_min - start_min;
    }
    return (24 * 60 - start_min) + end_min;
}

int feeder_slot_minute(int slot, int start_min, int end_min, int times)
{
    if (times <= 0) {
        return start_min;
    }
    if (times == 1) {
        return end_min;
    }
    if (slot <= 0) {
        return start_min;
    }
    if (slot >= times - 1) {
        return end_min;
    }

    const int interior_count = times - 2;
    const int window = feeder_window_minutes(start_min, end_min);
    const int offset = (window * slot) / (interior_count + 1);
    int minute = start_min + offset;
    while (minute >= 24 * 60) {
        minute -= 24 * 60;
    }
    return minute;
}

bool feeder_in_feeding_window(int now_min, int start_min, int end_min, int times)
{
    if (times == 1) {
        return true;
    }
    if (start_min < end_min) {
        return now_min >= start_min && now_min <= end_min;
    }
    return now_min >= start_min || now_min <= end_min;
}
