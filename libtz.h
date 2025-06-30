#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	TZ_No_Leap,
	TZ_Leap,
	TZ_Month_Week_Day,
} TZ_Date_Kind;

typedef struct {
	TZ_Date_Kind type;

	uint8_t month;
	uint8_t week;
	uint16_t day;

	int64_t time;
} TZ_Transition_Date;

typedef struct {
	bool has_dst;

	char *std_name;
	int64_t std_offset;
	TZ_Transition_Date std_date;

	char *dst_name;
	int64_t dst_offset;
	TZ_Transition_Date dst_date;
} TZ_RRule;

typedef struct {
	int64_t time;
	int64_t utc_offset;
	char *shortname;
	bool dst;
} TZ_Record;

typedef struct {
	char *name;

	TZ_Record *records;
	int record_count;
	char **shortnames;
	int shortname_count;

	TZ_RRule rrule;
} TZ_Region;

typedef struct {
	int64_t time;
	TZ_Region *tz;
} DateTime;

bool region_load(char *region_name, TZ_Region **region);
bool parse_posix_tz(char *posix_tz, TZ_RRule *rrule);

DateTime datetime_now(void);
DateTime datetime_to_utc(DateTime dt);
DateTime datetime_to_tz(DateTime in_dt, TZ_Region *tz);
char *datetime_to_str(DateTime dt);
