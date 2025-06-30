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
	int64_t year;
	int8_t month;
	int8_t day;
} TZ_Date;

typedef struct {
	int8_t hours;
	int8_t minutes;
	int8_t seconds;
} TZ_HMS;

typedef struct {
	int64_t time;
	TZ_Region *tz;
} TZ_Time;

bool region_load(char *region_name, TZ_Region **region);
bool parse_posix_tz(char *posix_tz, TZ_RRule *rrule);

TZ_Time tz_time_new(int64_t time);
TZ_Time tz_time_to_utc(TZ_Time dt);
TZ_Time tz_time_to_tz(TZ_Time in_dt, TZ_Region *tz);
char *tz_time_to_str(TZ_Time dt);

TZ_Date tz_get_date(TZ_Time t);
TZ_HMS  tz_get_hms(TZ_Time t);
