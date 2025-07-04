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

	char std_name[33];
	int64_t std_offset;
	TZ_Transition_Date std_date;

	char dst_name[33];
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
	int64_t record_count;
	char **shortnames;
	int64_t shortname_count;

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

bool tz_region_load(char *region_name, TZ_Region **region);
bool tz_region_load_local(bool check_env, TZ_Region **region);
bool tz_region_load_from_file(char *file_path, char *reg_str, TZ_Region **region);
bool tz_region_load_from_buffer(uint8_t *buffer, size_t sz, char *reg_str, TZ_Region **region);
bool tz_parse_posix_tz(char *posix_tz, int tz_str_len, TZ_RRule *rrule);

void tz_region_destroy(TZ_Region *region);
void tz_rrule_destroy(TZ_RRule *rrule);

TZ_Time tz_time_from_unix_seconds(int64_t time);
TZ_Time tz_time_from_components(TZ_Date date, TZ_HMS hms, TZ_Region *tz);
TZ_Time tz_time_to_utc(TZ_Time t);
TZ_Time tz_time_to_tz(TZ_Time in_t, TZ_Region *tz);
int64_t tz_time_to_unix_seconds(TZ_Time t);

TZ_Date tz_get_date(TZ_Time t);
TZ_HMS  tz_get_hms(TZ_Time t);
char *tz_shortname(TZ_Time t);
bool  tz_is_dst(TZ_Time t);
