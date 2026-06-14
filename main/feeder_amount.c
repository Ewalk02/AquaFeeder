#include "feeder_amount.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

uint32_t feeder_amount_tenths_to_ms(uint16_t tenths)
{
    return (uint32_t)tenths * 100U;
}

float feeder_amount_tenths_to_seconds(uint16_t tenths)
{
    return (float)tenths / 10.0f;
}

bool feeder_amount_seconds_value_to_tenths(double seconds, uint16_t *tenths_out)
{
    if (tenths_out == NULL || seconds < 0.1 || seconds > 120.0) {
        return false;
    }

    const uint32_t tenths = (uint32_t)(seconds * 10.0 + 0.5);
    if (tenths < FEEDER_AMOUNT_TENTHS_MIN || tenths > FEEDER_AMOUNT_TENTHS_MAX) {
        return false;
    }

    *tenths_out = (uint16_t)tenths;
    return true;
}

bool feeder_amount_parse_seconds_text(const char *text, uint16_t *tenths_out)
{
    if (text == NULL || tenths_out == NULL) {
        return false;
    }

    char *end = NULL;
    const double seconds = strtod(text, &end);
    if (end == text) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (*end != '\0') {
        return false;
    }

    return feeder_amount_seconds_value_to_tenths(seconds, tenths_out);
}

void feeder_amount_format_seconds_text(char *buf, size_t len, uint16_t tenths)
{
    if (buf == NULL || len == 0) {
        return;
    }

    const float seconds = feeder_amount_tenths_to_seconds(tenths);
    if (fabsf(seconds - roundf(seconds)) < 0.01f) {
        snprintf(buf, len, "%.0f", seconds);
    } else {
        snprintf(buf, len, "%.1f", seconds);
    }
}

uint16_t feeder_amount_migrate_legacy_seconds(uint16_t legacy_value)
{
    if (legacy_value >= 1 && legacy_value <= FEEDER_LEGACY_SECONDS_MAX) {
        return (uint16_t)(legacy_value * 10U);
    }
    return legacy_value;
}
