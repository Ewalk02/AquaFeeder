#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Duration is stored in tenths of a second (10 = 1.0 s, 35 = 3.5 s). */
#define FEEDER_AMOUNT_TENTHS_MIN     1
#define FEEDER_AMOUNT_TENTHS_MAX     1200
#define FEEDER_AMOUNT_TENTHS_DEFAULT 30
#define FEEDER_LEGACY_SECONDS_MAX    120

uint32_t feeder_amount_tenths_to_ms(uint16_t tenths);
float feeder_amount_tenths_to_seconds(uint16_t tenths);
bool feeder_amount_seconds_value_to_tenths(double seconds, uint16_t *tenths_out);
bool feeder_amount_parse_seconds_text(const char *text, uint16_t *tenths_out);
void feeder_amount_format_seconds_text(char *buf, size_t len, uint16_t tenths);
uint16_t feeder_amount_migrate_legacy_seconds(uint16_t legacy_value);
