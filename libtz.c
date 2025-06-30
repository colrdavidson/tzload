#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "libtz.h"

// SECTION: Utilities
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
	uint8_t *data;
	uint64_t len;
} Slice;

static Slice slice_sub(Slice s, uint64_t start_idx) {
	return (Slice){.data = s.data + start_idx, .len = s.len - start_idx};
}

typedef struct {
	char **strs;
	uint64_t len;
	uint64_t cap;
} DynArr;

static void dynarr_append(DynArr *dyn, char *str) {
	if (dyn->len + 1 > dyn->cap) {
		dyn->cap = MAX(8, dyn->cap * 2);
		dyn->strs = (char **)realloc(dyn->strs, sizeof(char *) * dyn->cap);
	}
	dyn->strs[dyn->len] = str;
	dyn->len += 1;
}

static bool load_entire_file(char *path, uint8_t **out_buf, size_t *len) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		return false;
	}

	uint64_t file_length = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	uint8_t *buffer = (uint8_t *)malloc(file_length);
	read(fd, buffer, file_length);

	close(fd);

	*out_buf = buffer;
	*len = file_length;
	return true;
}

static bool is_whitespace(uint8_t ch) {
	return (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r');
}

static bool is_alphabetic(uint8_t ch) {
	//     ('A'   ->    'Z')        || ('a'    ->    'z')
	return (ch > 0x40 && ch < 0x5B) || (ch > 0x60 && ch < 0x7B);
}

static bool is_numeric(uint8_t ch) {
	//     ('0'   ->    '9')
	return (ch > 0x2f && ch < 0x3A);
}

static bool parse_i64(char *str, int64_t *val, int64_t *len) {
	char *endptr = NULL;
	int64_t ret = strtoll(str, &endptr, 10);
	if (ret == 0 && errno == EINVAL) {
		return false;
	}

	*val = ret;
	*len = endptr - str;
	return true;
}

// TIME MATH
#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_HOUR (60 * SECONDS_PER_MINUTE)
#define SECONDS_PER_DAY (24 * SECONDS_PER_HOUR)

#define DAYS_PER_400_YEARS ((365 * 400) + 97)
#define DAYS_PER_100_YEARS ((365 * 100) + 24)
#define DAYS_PER_4_YEARS   ((365 * 4)   +  1)

#define ABSOLUTE_ZERO_YEAR   ((int64_t)(-292277022399ll))
#define ABSOLUTE_TO_INTERNAL ((int64_t)(-9223371966579724800ll))
#define INTERNAL_TO_ABSOLUTE (-ABSOLUTE_TO_INTERNAL)

#define UNIX_TO_INTERNAL ((int64_t)((1969 * 365) + (1969 / 4) - (1969 / 100) + (1969 / 400)) * SECONDS_PER_DAY)
#define UNIX_TO_ABSOLUTE (UNIX_TO_INTERNAL + INTERNAL_TO_ABSOLUTE)

static int32_t days_before[] = {
    0,
    31,
    31 + 28,
    31 + 28 + 31,
    31 + 28 + 31 + 30,
    31 + 28 + 31 + 30 + 31,
    31 + 28 + 31 + 30 + 31 + 30,
    31 + 28 + 31 + 30 + 31 + 30 + 31,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30,
    31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30 + 31,
};

static int64_t month_to_seconds(int64_t month, bool is_leap) {
	int64_t month_seconds[] = {
		0,                      31 * SECONDS_PER_DAY,  59 * SECONDS_PER_DAY,  90 * SECONDS_PER_DAY,
		120 * SECONDS_PER_DAY, 151 * SECONDS_PER_DAY, 181 * SECONDS_PER_DAY, 212 * SECONDS_PER_DAY,
		243 * SECONDS_PER_DAY, 273 * SECONDS_PER_DAY, 304 * SECONDS_PER_DAY, 334 * SECONDS_PER_DAY,
	};

	int64_t t = month_seconds[month];
	if (is_leap && month >= 2) {
		t += SECONDS_PER_DAY;
	}

	return t;
}

static bool is_leap_year(int64_t year) {
	return year % 4 == 0 && ((year % 100) != 0 || (year % 400) == 0);
}

static int64_t leap_years_before(int64_t year) {
	year -= 1;
	return (year / 4) - (year / 100) + (year / 400);
}

static int64_t leap_years_between(int64_t start, int64_t end) {
	return leap_years_before(end) - leap_years_before(start + 1);
}

static int64_t year_to_time(int64_t year) {
	int64_t year_gap = year - 1970;
	int64_t leap_count = leap_years_between(1970, year);
	return ((year_gap * 365) + leap_count) * SECONDS_PER_DAY;
}

static int64_t last_day_of_month(int64_t year, int64_t month) {
	int8_t month_days[] = {-1, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int64_t day = month_days[month];
	if (month == 2 && is_leap_year(year)) {
		day += 1;
	}

	return day;
}

// SECTION: TZif Parsing
#define TZIF_MAGIC 0x545A6966
#define BIG_BANG_ISH -0x800000000000000ll
#define TWO_AM 2 * 60 * 60

typedef enum {
	V1 = 0,
	V2 = '2',
	V3 = '3',
	V4 = '4',
} TZif_Version;

typedef enum {
	Standard = 0,
	DST      = 1,
} TZif_Sun_Shift;

typedef struct __attribute__((packed)) {
	int64_t occur;
	int32_t corr;
} Leapsecond_Record;

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint8_t  version;
	uint8_t  reserved[15];
	uint32_t isutcnt;
	uint32_t isstdcnt;
	uint32_t leapcnt;
	uint32_t timecnt;
	uint32_t typecnt;
	uint32_t charcnt;
} TZif_Header;

typedef struct __attribute__((packed)) {
	int32_t utoff;
	uint8_t dst;
	uint8_t idx;
} Local_Time_Type;

static void tzif_hdr_to_native(TZif_Header *hdr) {
	hdr->magic = ntohl(hdr->magic);
	hdr->isutcnt = ntohl(hdr->isutcnt);
	hdr->isstdcnt = ntohl(hdr->isstdcnt);
	hdr->leapcnt = ntohl(hdr->leapcnt);
	hdr->timecnt = ntohl(hdr->timecnt);
	hdr->typecnt = ntohl(hdr->typecnt);
	hdr->charcnt = ntohl(hdr->charcnt);
}

static int tzif_data_block_size(TZif_Header *hdr, TZif_Version version) {
	int time_size;

	if (version == V1) {
		time_size = 4;
	} else if (version == V2 || version == V3 || version == V4) {
		time_size = 8;
	} else {
		return 0;
	}

	return (hdr->timecnt * time_size)              +
		   hdr->timecnt                            + 
		   hdr->typecnt  * sizeof(Local_Time_Type) +
		   hdr->charcnt                            +
		   hdr->leapcnt  * (time_size + 4)         +
		   hdr->isstdcnt                           +
		   hdr->isutcnt;
}

static bool is_valid_quoted_char(uint8_t ch) {
	return is_alphabetic(ch) || is_numeric(ch) || ch == '+' || ch == '-';
}

static bool parse_posix_tz_shortname(char *str, char **out, int64_t *idx) {
	bool was_quoted = false;
	bool quoted = false;
	int i = 0;

	char *s = str;
	for (; *s != '\0'; s++,i++) {
		char ch = *s;

		if (!quoted && ch == '<') {
			quoted = true;
			was_quoted = true;
			continue;
		}

		if (quoted && ch == '>') {
			quoted = false;
			break;
		}

		if (!is_valid_quoted_char(ch) && ch != ',') {
			return false;
		}

		if (!quoted && !is_alphabetic(ch)) {
			break;
		}
	}

	// We never got the trailing quote?
	if (was_quoted && quoted) {
		return false;
	}

	int end_idx = i;
	if (was_quoted) {
		end_idx += 1;
		asprintf(out, "%.*s", end_idx, str+1);
		*idx = end_idx;
	} else {
		asprintf(out, "%.*s", end_idx, str);
		*idx = end_idx;
	}

	return true;
}

static bool parse_posix_tz_offset(char *str, int64_t *offset, int64_t *idx) {
	int64_t sign = 1;
	int start_idx = 0;

	if (*str == '+') {
		sign = 1;
		start_idx = 1;
	} else if (*str == '-') {
		sign = -1;
		start_idx = 1;
	}

	char *s = str + start_idx;

	int64_t ret_sec = 0;
	int64_t hours = 0;
	int64_t mins = 0;
	int64_t secs = 0;
	int64_t len = 0;

	if (!parse_i64(s, &hours, &len)) { return false; }
	if (hours > 167 || hours < -167) { return false; }
	ret_sec += hours * (60 * 60);
	s += len;

	if (*s != ':') {
		goto end_parse;
	}
	s += 1;

	if (!parse_i64(s, &mins, &len)) { return false; }
	if (len != 2) { return false; }
	if (mins > 59 || mins < 0) { return false; }
	ret_sec += mins * 60;
	s += len;

	if (*s != ':') {
		goto end_parse;
	}
	s += 1;

	if (!parse_i64(s, &secs, &len)) { return false; }
	if (len != 2) { return false; }
	if (secs > 59 || secs < 0) { return false; }
	ret_sec += secs;
	s += len;

end_parse:
	*offset = ret_sec * sign;
	*idx = s - str;
	return true;
}

static bool parse_posix_rrule(char *rrule_str, int rrule_str_len, TZ_Transition_Date *date, int64_t *idx) {
	if (rrule_str_len < 2) { return false; }

	char *str = rrule_str;
	int64_t off = 0;

	// No leap
	if (*str == 'J') {
		str += 1;

		int64_t day = 0;
		if (!parse_i64(str, &day, &off)) { return false; }
		if (day < 1 || day > 365) { return false; }
		str += off;

		int64_t offset = TWO_AM;
		if (*str == '/') {
			str += 1;
			if (!parse_posix_tz_offset(str, &offset, &off)) { return false; }
			str += off;
		}
		if (*str == ',') {
			str += 1;
		}

		*date = (TZ_Transition_Date){
			.type  = TZ_No_Leap,
			.day   = (uint16_t)day,
			.time  = offset,
		};
		*idx = str - rrule_str;
		return true;

	} else if (*str == 'M') {
		str += 1;

		int64_t month, week, day = 0;
		if (!parse_i64(str, &month, &off)) { return false; }
		if (month < 1 || month > 12) { return false; }
		str += off + 1;

		if (!parse_i64(str, &week, &off)) { return false; }
		if (week < 1 || week > 5) { return false; }
		str += off + 1;

		if (!parse_i64(str, &day, &off)) { return false; }
		if (day < 0 || day > 6) { return false; }
		str += off;

		int64_t offset = TWO_AM;
		if (*str == '/') {
			str += 1;
			if (!parse_posix_tz_offset(str, &offset, &off)) { return false; }
			str += off;
		}

		if (*str == ',') {
			str += 1;
		}

		*date = (TZ_Transition_Date){
			.type  = TZ_Month_Week_Day,
			.month = (uint8_t)month,
			.week  = (uint8_t)week,
			.day   = (uint16_t)day,
			.time  = offset,
		};
		*idx = str - rrule_str;
		return true;

	// Leap
	} else if (is_numeric(*str)) {
		int64_t day = 0;
		if (!parse_i64(str, &day, &off)) { return false; }
		if (day < 1 || day > 365) { return false; }
		str += off;

		int64_t offset = TWO_AM;
		if (*str == '/') {
			str += 1;
			if (!parse_posix_tz_offset(str, &offset, &off)) { return false; }
			str += off;
		}
		if (*str == ',') {
			str += 1;
		}

		*date = (TZ_Transition_Date){
			.type  = TZ_Leap,
			.day   = (uint16_t)day,
			.time  = offset,
		};
		*idx = str - rrule_str;
		return true;
	}

	return false;
}

bool tz_parse_posix_tz(char *posix_tz, int tz_str_len, TZ_RRule *rrule) {
	if (tz_str_len < 4) { return false; }

	char *tz_str = posix_tz;

	char *std_name = NULL;
	int64_t end_idx = 0;
	if (!parse_posix_tz_shortname(tz_str, &std_name, &end_idx)) { return false; }

	int64_t std_offset = 0;
	tz_str += end_idx;
	if (!parse_posix_tz_offset(tz_str, &std_offset, &end_idx)) { return false; }
	std_offset *= -1;
	tz_str += end_idx;

	int64_t rem_len = tz_str_len - (tz_str - posix_tz);
	if (rem_len == 0) {
		*rrule = (TZ_RRule){
			.has_dst = false,
			.std_name = std_name,
			.std_offset = std_offset,
			.std_date = (TZ_Transition_Date){
				.type = TZ_Leap,
				.day  = 0,
				.time = TWO_AM,
			},
		};
		return true;
	}

	char *dst_name = NULL;
	int64_t dst_offset = std_offset + (60 * 60);
	if (*tz_str != ',') {
		if (!parse_posix_tz_shortname(tz_str, &dst_name, &end_idx)) { return false; }
		tz_str += end_idx;

		if (*tz_str != ',') {
			if (!parse_posix_tz_offset(tz_str, &dst_offset, &end_idx)) { return false; }
			dst_offset *= -1;
			tz_str += end_idx;
		}
	}
	if (*tz_str != ',') { return false; }
	tz_str += 1;

	TZ_Transition_Date std_td;
	rem_len = tz_str_len - (tz_str - posix_tz);
	if (!parse_posix_rrule(tz_str, rem_len, &std_td, &end_idx)) { return false; }
	tz_str += end_idx;

	TZ_Transition_Date dst_td;
	rem_len = tz_str_len - (tz_str - posix_tz);
	if (!parse_posix_rrule(tz_str, rem_len, &dst_td, &end_idx)) { return false; }
	tz_str += end_idx;

	*rrule = (TZ_RRule){
		.has_dst = true,

		.std_name = std_name,
		.std_offset = std_offset,
		.std_date   = std_td,

		.dst_name = dst_name,
		.dst_offset = dst_offset,
		.dst_date   = dst_td,
	};

	return true;
}

bool parse_tzif(uint8_t *buffer, size_t size, char *region_name, TZ_Region **out_region) {
	Slice s = {.data = buffer, .len = size};

	TZif_Header *v1_hdr = (TZif_Header *)s.data;
	tzif_hdr_to_native(v1_hdr);

	if (v1_hdr->magic != TZIF_MAGIC) {
		return false;
	}
	if (v1_hdr->typecnt == 0 || v1_hdr->charcnt == 0) {
		return false;
	}
	if (v1_hdr->isutcnt != 0 && v1_hdr->isutcnt != v1_hdr->typecnt) {
		return false;
	}

	if (v1_hdr->version == V1) {
		return false;
	}

	if (v1_hdr->version != V2 && v1_hdr->version != V3) {
		return false;
	}

	int first_block_size = tzif_data_block_size(v1_hdr, V1);
	if (s.len <= sizeof(TZif_Header) + first_block_size) {
		return false;
	}
	s = slice_sub(s, sizeof(TZif_Header) + first_block_size);

	TZif_Header *real_hdr = (TZif_Header *)s.data;
	tzif_hdr_to_native(real_hdr);

	if (real_hdr->magic != TZIF_MAGIC) {
		return false;
	}
	if (real_hdr->typecnt == 0 || real_hdr->charcnt == 0) {
		return false;
	}
	if (real_hdr->isutcnt != 0 && real_hdr->isutcnt != real_hdr->typecnt) {
		return false;
	}
	if (real_hdr->isstdcnt != 0 && real_hdr->isstdcnt != real_hdr->typecnt) {
		return false;
	}

	int real_block_size = tzif_data_block_size(real_hdr, (TZif_Version)v1_hdr->version);
	if (s.len <= sizeof(TZif_Header) + real_block_size) {
		return false;
	}
	s = slice_sub(s, sizeof(TZif_Header));

	// Scan and flip all the tzif arrays
	int64_t *transition_times = (int64_t *)s.data;
	for (int i = 0; i < real_hdr->timecnt; i++) {
		int64_t *time = &transition_times[i];
		*time = ntohll(*time);
		if (*time < BIG_BANG_ISH) {
			return false;
		}
	}
	s = slice_sub(s, real_hdr->timecnt * sizeof(int64_t));

	uint8_t *transition_types = s.data;
	for (int i = 0; i < real_hdr->timecnt; i++) {
		uint8_t type = transition_types[i];
		if ((int)type > ((int)real_hdr->typecnt - 1)) {
			return false;
		}
	}
	s = slice_sub(s, real_hdr->timecnt);

	Local_Time_Type *local_time_types = (Local_Time_Type *)s.data;
	for (int i = 0; i < real_hdr->typecnt; i++) {
		Local_Time_Type *ltt = &local_time_types[i];
		ltt->utoff = ntohl(ltt->utoff);

		// UT offset should be > -25 and < 26 hours
		if ((int)ltt->utoff < -89999 || (int)ltt->utoff > 93599) {
			return false;
		}

		if (ltt->dst != DST && ltt->dst != Standard) {
			return false;
		}

		if ((int)ltt->idx > ((int)real_hdr->charcnt - 1)) {
			return false;
		}
	}
	s = slice_sub(s, real_hdr->typecnt * sizeof(Local_Time_Type));

	char *timezone_string_table = (char *)s.data;
	s = slice_sub(s, real_hdr->charcnt);

	Leapsecond_Record *leapsecond_records = (Leapsecond_Record *)s.data;
	for (int i = 0; i < real_hdr->leapcnt; i++) {
		Leapsecond_Record *record = &leapsecond_records[i];
		record->occur = ntohll(record->occur);
		record->corr = ntohl(record->corr);
	}
	if (real_hdr->leapcnt > 0 && leapsecond_records[0].occur < 0) {
		return false;
	}
	s = slice_sub(s, real_hdr->leapcnt * sizeof(Leapsecond_Record));

	uint8_t *standard_wall_tags = s.data;
	for (int i = 0; i < real_hdr->isstdcnt; i++) {
		uint8_t stdwall_tag = standard_wall_tags[i];
		if (stdwall_tag != 0 && stdwall_tag != 1) {
			return false;
		}
	}
	s = slice_sub(s, real_hdr->isstdcnt);

	uint8_t *ut_tags = s.data;
	for (int i = 0; i < real_hdr->isutcnt; i++) {
		uint8_t ut_tag = ut_tags[i];
		if (ut_tag != 0 && ut_tag != 1) {
			return false;
		}
	}
	s = slice_sub(s, real_hdr->isutcnt);

	// Start of footer
	if (s.data[0] != '\n') {
		return false;
	}
	s = slice_sub(s, 1);

	if (s.data[0] == ':') {
		return false;
	}

	for (int i = 0; i < s.len; i++) {
		char ch = (char)s.data[i];
		if (ch == '\n') {
			break;
		}

		if (ch == 0) {
			return false;
		}
	}
	char *footer_str = (char *)s.data;

	TZ_RRule rrule;
	if (!tz_parse_posix_tz(footer_str, s.len - 1, &rrule)) { return false; }

	// UTC is a special case, we don't need to alloc
	if (real_hdr->typecnt == 1 && local_time_types[0].utoff == 0) {
		*out_region = NULL;
		return true;
	}

	Slice str_table = {.data = (uint8_t *)timezone_string_table, .len = real_hdr->charcnt};
	char **ltt_names = (char **)malloc(sizeof(char *) * real_hdr->typecnt);
	for (int i = 0; i < real_hdr->typecnt; i++) {
		Local_Time_Type ltt = local_time_types[i];

		Slice ltt_name_str = slice_sub(str_table, ltt.idx);
		ltt_names[i] = strndup((char *)ltt_name_str.data, ltt_name_str.len);
	}

	TZ_Record *records = (TZ_Record *)malloc(real_hdr->timecnt * sizeof(TZ_Record));
	for (int i = 0; i < real_hdr->timecnt; i++) {
		int64_t trans_time = transition_times[i];
		int trans_idx = transition_types[i];
		Local_Time_Type ltt = local_time_types[trans_idx];

		records[i] = (TZ_Record){
			.time       = trans_time,
			.utc_offset = ltt.utoff,
			.shortname  = ltt_names[trans_idx],
			.dst        = !!ltt.dst,
		};
	}

	TZ_Region *region = (TZ_Region *)malloc(sizeof(TZ_Region));
	*region = (TZ_Region){
		.records         = records,
		.record_count    = real_hdr->timecnt,
		.shortnames      = ltt_names,
		.shortname_count = real_hdr->typecnt,
		.name            = strdup(region_name),
	};
	*out_region = region;
	return true;
}

static bool load_tzif_file(char *path, char *name, TZ_Region **region) {
	uint8_t *buffer = NULL;
	size_t file_length = 0;
	if (!load_entire_file(path, &buffer, &file_length)) return false;

	bool ret = parse_tzif(buffer, file_length, name, region);
	free(buffer);

	return ret;
}

// SECTION: Platform-specific TZ_Region Functions
#if !defined(_WIN64) || !defined(_WIN32)
static char *local_tz_name(bool check_env) {
	if (check_env) {
		char *local_str = getenv("TZ");
		if (local_str != NULL) {
			return strdup(local_str);
		}
	}

	char *orig_localtime_path = (char *)"/etc/localtime";
	char *local_path = realpath(orig_localtime_path, NULL);
	if (local_path == NULL) {
		return strdup((char *)"UTC");
	}

	// FreeBSD copies rather than softlinks the local timezone file occasionally,
	// Find the name of the timezone in /var/db/zoneinfo
	if (strcmp(orig_localtime_path, local_path) == 0) {
		uint8_t *buffer = NULL;
		size_t len = 0;
		if (!load_entire_file("/var/db/zoneinfo", &buffer, &len)) return strdup((char *)"UTC");

		// trim the whitespace off the path
		int i = len - 1;
		for (; i >= 0; i--) {
			if (!is_whitespace(buffer[i])) {
				break;
			}
		}

		char *local_tz = NULL;
		asprintf(&local_tz, "%.*s", i + 1, buffer);
		return local_tz;
	}

	DynArr path_chunks = {};
	char *path_ptr = NULL;
	char *chunk = NULL;
	for (chunk = strtok_r(local_path, "/", &path_ptr); chunk != NULL; chunk = strtok_r(NULL, "/", &path_ptr)) {
		dynarr_append(&path_chunks, chunk);
	}

	char *local_tz = NULL;
	char *path_file = path_chunks.strs[path_chunks.len - 1];
	char *path_dir = path_chunks.strs[path_chunks.len - 2];
	if (strstr(path_dir, "zoneinfo")) {
		local_tz = strdup(path_file);
	} else {
		asprintf(&local_tz, "%s/%s", path_dir, path_file);
	}

	free(path_chunks.strs);
	free(local_path);

	return local_tz;
}

static bool load_region(char *region_name, TZ_Region **region) {
	if (!strcmp(region_name, "UTC")) {
		*region = NULL;
		return true;
	}

	char *reg_str = strdup(region_name);

	char *region_path;
	asprintf(&region_path, "%s/%s", "/usr/share/zoneinfo", reg_str);

	bool ret = load_tzif_file(region_path, reg_str, region);

	free(reg_str);
	free(region_path);

	return ret;
}

static bool load_local_region(bool check_env, TZ_Region **region) {
	char *reg_str = local_tz_name(check_env);
	if (!strcmp(reg_str, "UTC")) {
		free(reg_str);
		*region = NULL;
		return true;
	}

	bool ret = load_region(reg_str, region);
	free(reg_str);

	return ret;
}
#else
typedef struct {
	char *std;
	char *dst;
} TZ_Abbrev;

typedef struct {
	char *key;
	TZ_Abbrev value;
} TZ_AbbrevMap;

static TZ_AbbrevMap tz_abbrevs[] = {
	{"Egypt Standard Time",             {"EET", "EEST"}},    // Africa/Cairo
	{"Morocco Standard Time",           {"+00", "+01"}},     // Africa/Casablanca
	{"South Africa Standard Time",      {"SAST", "SAST"}},   // Africa/Johannesburg
	{"South Sudan Standard Time",       {"CAT", "CAT"}},     // Africa/Juba
	{"Sudan Standard Time",             {"CAT", "CAT"}},     // Africa/Khartoum
	{"W. Central Africa Standard Time", {"WAT", "WAT"}},     // Africa/Lagos
	{"E. Africa Standard Time",         {"EAT", "EAT"}},     // Africa/Nairobi
	{"Sao Tome Standard Time",          {"GMT", "GMT"}},     // Africa/Sao_Tome
	{"Libya Standard Time",             {"EET", "EET"}},     // Africa/Tripoli
	{"Namibia Standard Time",           {"CAT", "CAT"}},     // Africa/Windhoek
	{"Aleutian Standard Time",          {"HST", "HDT"}},     // America/Adak
	{"Alaskan Standard Time",           {"AKST", "AKDT"}},   // America/Anchorage
	{"Tocantins Standard Time",         {"-03", "-03"}},     // America/Araguaina
	{"Paraguay Standard Time",          {"-04", "-03"}},     // America/Asuncion
	{"Bahia Standard Time",             {"-03", "-03"}},     // America/Bahia
	{"SA Pacific Standard Time",        {"-05", "-05"}},     // America/Bogota
	{"Argentina Standard Time",         {"-03", "-03"}},     // America/Buenos_Aires
	{"Eastern Standard Time (Mexico)",  {"EST", "EST"}},     // America/Cancun
	{"Venezuela Standard Time",         {"-04", "-04"}},     // America/Caracas
	{"SA Eastern Standard Time",        {"-03", "-03"}},     // America/Cayenne
	{"Central Standard Time",           {"CST", "CDT"}},     // America/Chicago
	{"Central Brazilian Standard Time", {"-04", "-04"}},     // America/Cuiaba
	{"Mountain Standard Time",          {"MST", "MDT"}},     // America/Denver
	{"Greenland Standard Time",         {"-03", "-02"}},     // America/Godthab
	{"Turks And Caicos Standard Time",  {"EST", "EDT"}},     // America/Grand_Turk
	{"Central America Standard Time",   {"CST", "CST"}},     // America/Guatemala
	{"Atlantic Standard Time",          {"AST", "ADT"}},     // America/Halifax
	{"Cuba Standard Time",              {"CST", "CDT"}},     // America/Havana
	{"US Eastern Standard Time",        {"EST", "EDT"}},     // America/Indianapolis
	{"SA Western Standard Time",        {"-04", "-04"}},     // America/La_Paz
	{"Pacific Standard Time",           {"PST", "PDT"}},     // America/Los_Angeles
	{"Mountain Standard Time (Mexico)", {"MST", "MST"}},     // America/Mazatlan
	{"Central Standard Time (Mexico)",  {"CST", "CST"}},     // America/Mexico_City
	{"Saint Pierre Standard Time",      {"-03", "-02"}},     // America/Miquelon
	{"Montevideo Standard Time",        {"-03", "-03"}},     // America/Montevideo
	{"Eastern Standard Time",           {"EST", "EDT"}},     // America/New_York
	{"US Mountain Standard Time",       {"MST", "MST"}},     // America/Phoenix
	{"Haiti Standard Time",             {"EST", "EDT"}},     // America/Port-au-Prince
	{"Magallanes Standard Time",        {"-03", "-03"}},     // America/Punta_Arenas
	{"Canada Central Standard Time",    {"CST", "CST"}},     // America/Regina
	{"Pacific SA Standard Time",        {"-04", "-03"}},     // America/Santiago
	{"E. South America Standard Time",  {"-03", "-03"}},     // America/Sao_Paulo
	{"Newfoundland Standard Time",      {"NST", "NDT"}},     // America/St_Johns
	{"Pacific Standard Time (Mexico)",  {"PST", "PDT"}},     // America/Tijuana
	{"Yukon Standard Time",             {"MST", "MST"}},     // America/Whitehorse
	{"Central Asia Standard Time",      {"+06", "+06"}},     // Asia/Almaty
	{"Jordan Standard Time",            {"+03", "+03"}},     // Asia/Amman
	{"Arabic Standard Time",            {"+03", "+03"}},     // Asia/Baghdad
	{"Azerbaijan Standard Time",        {"+04", "+04"}},     // Asia/Baku
	{"SE Asia Standard Time",           {"+07", "+07"}},     // Asia/Bangkok
	{"Altai Standard Time",             {"+07", "+07"}},     // Asia/Barnaul
	{"Middle East Standard Time",       {"EET", "EEST"}},    // Asia/Beirut
	{"India Standard Time",             {"IST", "IST"}},     // Asia/Calcutta
	{"Transbaikal Standard Time",       {"+09", "+09"}},     // Asia/Chita
	{"Sri Lanka Standard Time",         {"+0530", "+0530"}}, // Asia/Colombo
	{"Syria Standard Time",             {"+03", "+03"}},     // Asia/Damascus
	{"Bangladesh Standard Time",        {"+06", "+06"}},     // Asia/Dhaka
	{"Arabian Standard Time",           {"+04", "+04"}},     // Asia/Dubai
	{"West Bank Standard Time",         {"EET", "EEST"}},    // Asia/Hebron
	{"W. Mongolia Standard Time",       {"+07", "+07"}},     // Asia/Hovd
	{"North Asia East Standard Time",   {"+08", "+08"}},     // Asia/Irkutsk
	{"Israel Standard Time",            {"IST", "IDT"}},     // Asia/Jerusalem
	{"Afghanistan Standard Time",       {"+0430", "+0430"}}, // Asia/Kabul
	{"Russia Time Zone 11",             {"+12", "+12"}},     // Asia/Kamchatka
	{"Pakistan Standard Time",          {"PKT", "PKT"}},     // Asia/Karachi
	{"Nepal Standard Time",             {"+0545", "+0545"}}, // Asia/Katmandu
	{"North Asia Standard Time",        {"+07", "+07"}},     // Asia/Krasnoyarsk
	{"Magadan Standard Time",           {"+11", "+11"}},     // Asia/Magadan
	{"N. Central Asia Standard Time",   {"+07", "+07"}},     // Asia/Novosibirsk
	{"Omsk Standard Time",              {"+06", "+06"}},     // Asia/Omsk
	{"North Korea Standard Time",       {"KST", "KST"}},     // Asia/Pyongyang
	{"Qyzylorda Standard Time",         {"+05", "+05"}},     // Asia/Qyzylorda
	{"Myanmar Standard Time",           {"+0630", "+0630"}}, // Asia/Rangoon
	{"Arab Standard Time",              {"+03", "+03"}},     // Asia/Riyadh
	{"Sakhalin Standard Time",          {"+11", "+11"}},     // Asia/Sakhalin
	{"Korea Standard Time",             {"KST", "KST"}},     // Asia/Seoul
	{"China Standard Time",             {"CST", "CST"}},     // Asia/Shanghai
	{"Singapore Standard Time",         {"+08", "+08"}},     // Asia/Singapore
	{"Russia Time Zone 10",             {"+11", "+11"}},     // Asia/Srednekolymsk
	{"Taipei Standard Time",            {"CST", "CST"}},     // Asia/Taipei
	{"West Asia Standard Time",         {"+05", "+05"}},     // Asia/Tashkent
	{"Georgian Standard Time",          {"+04", "+04"}},     // Asia/Tbilisi
	{"Iran Standard Time",              {"+0330", "+0330"}}, // Asia/Tehran
	{"Tokyo Standard Time",             {"JST", "JST"}},     // Asia/Tokyo
	{"Tomsk Standard Time",             {"+07", "+07"}},     // Asia/Tomsk
	{"Ulaanbaatar Standard Time",       {"+08", "+08"}},     // Asia/Ulaanbaatar
	{"Vladivostok Standard Time",       {"+10", "+10"}},     // Asia/Vladivostok
	{"Yakutsk Standard Time",           {"+09", "+09"}},     // Asia/Yakutsk
	{"Ekaterinburg Standard Time",      {"+05", "+05"}},     // Asia/Yekaterinburg
	{"Caucasus Standard Time",          {"+04", "+04"}},     // Asia/Yerevan
	{"Azores Standard Time",            {"-01", "+00"}},     // Atlantic/Azores
	{"Cape Verde Standard Time",        {"-01", "-01"}},     // Atlantic/Cape_Verde
	{"Greenwich Standard Time",         {"GMT", "GMT"}},     // Atlantic/Reykjavik
	{"Cen. Australia Standard Time",    {"ACST", "ACDT"}},   // Australia/Adelaide
	{"E. Australia Standard Time",      {"AEST", "AEST"}},   // Australia/Brisbane
	{"AUS Central Standard Time",       {"ACST", "ACST"}},   // Australia/Darwin
	{"Aus Central W. Standard Time",    {"+0845", "+0845"}}, // Australia/Eucla
	{"Tasmania Standard Time",          {"AEST", "AEDT"}},   // Australia/Hobart
	{"Lord Howe Standard Time",         {"+1030", "+11"}},   // Australia/Lord_Howe
	{"W. Australia Standard Time",      {"AWST", "AWST"}},   // Australia/Perth
	{"AUS Eastern Standard Time",       {"AEST", "AEDT"}},   // Australia/Sydney
	{"UTC-11",                          {"-11", "-11"}},     // Etc/GMT+11
	{"Dateline Standard Time",          {"-12", "-12"}},     // Etc/GMT+12
	{"UTC-02",                          {"-02", "-02"}},     // Etc/GMT+2
	{"UTC-08",                          {"-08", "-08"}},     // Etc/GMT+8
	{"UTC-09",                          {"-09", "-09"}},     // Etc/GMT+9
	{"UTC+12",                          {"+12", "+12"}},     // Etc/GMT-12
	{"UTC+13",                          {"+13", "+13"}},     // Etc/GMT-13
	{"UTC",                             {"UTC", "UTC"}},     // Etc/UTC
	{"Astrakhan Standard Time",         {"+04", "+04"}},     // Europe/Astrakhan
	{"W. Europe Standard Time",         {"CET", "CEST"}},    // Europe/Berlin
	{"GTB Standard Time",               {"EET", "EEST"}},    // Europe/Bucharest
	{"Central Europe Standard Time",    {"CET", "CEST"}},    // Europe/Budapest
	{"E. Europe Standard Time",         {"EET", "EEST"}},    // Europe/Chisinau
	{"Turkey Standard Time",            {"+03", "+03"}},     // Europe/Istanbul
	{"Kaliningrad Standard Time",       {"EET", "EET"}},     // Europe/Kaliningrad
	{"FLE Standard Time",               {"EET", "EEST"}},    // Europe/Kiev
	{"GMT Standard Time",               {"GMT", "BST"}},     // Europe/London
	{"Belarus Standard Time",           {"+03", "+03"}},     // Europe/Minsk
	{"Russian Standard Time",           {"MSK", "MSK"}},     // Europe/Moscow
	{"Romance Standard Time",           {"CET", "CEST"}},    // Europe/Paris
	{"Russia Time Zone 3",              {"+04", "+04"}},     // Europe/Samara
	{"Saratov Standard Time",           {"+04", "+04"}},     // Europe/Saratov
	{"Volgograd Standard Time",         {"MSK", "MSK"}},     // Europe/Volgograd
	{"Central European Standard Time",  {"CET", "CEST"}},    // Europe/Warsaw
	{"Mauritius Standard Time",         {"+04", "+04"}},     // Indian/Mauritius
	{"Samoa Standard Time",             {"+13", "+13"}},     // Pacific/Apia
	{"New Zealand Standard Time",       {"NZST", "NZDT"}},   // Pacific/Auckland
	{"Bougainville Standard Time",      {"+11", "+11"}},     // Pacific/Bougainville
	{"Chatham Islands Standard Time",   {"+1245", "+1345"}}, // Pacific/Chatham
	{"Easter Island Standard Time",     {"-06", "-05"}},     // Pacific/Easter
	{"Fiji Standard Time",              {"+12", "+12"}},     // Pacific/Fiji
	{"Central Pacific Standard Time",   {"+11", "+11"}},     // Pacific/Guadalcanal
	{"Hawaiian Standard Time",          {"HST", "HST"}},     // Pacific/Honolulu
	{"Line Islands Standard Time",      {"+14", "+14"}},     // Pacific/Kiritimati
	{"Marquesas Standard Time",         {"-0930", "-0930"}}, // Pacific/Marquesas
	{"Norfolk Standard Time",           {"+11", "+12"}},     // Pacific/Norfolk
	{"West Pacific Standard Time",      {"+10", "+10"}},     // Pacific/Port_Moresby
	{"Tonga Standard Time",             {"+13", "+13"}},     // Pacific/Tongatapu
};

static bool load_region(char *region_name, TZ_Region **region) {
	return false;
}
static bool load_local_region(bool check_env, TZ_Region **region) {
	return false;
}
#endif

// SECTION: Generic TZ_Region Functions
bool tz_region_load(char *region_name, TZ_Region **region) {
	return load_region(region_name, region);
}

bool tz_region_load_local(bool check_env, TZ_Region **region) {
	return load_local_region(check_env, region);
}

bool tz_region_load_from_file(char *file_path, char *reg_str, TZ_Region **region) {
	return load_tzif_file(file_path, reg_str, region);
}

bool tz_region_load_from_buffer(uint8_t *buffer, size_t sz, char *reg_str, TZ_Region **region) {
	return parse_tzif(buffer, sz, reg_str, region);
}

void tz_rrule_destroy(TZ_RRule *rrule) {
	free(rrule->std_name);
	free(rrule->dst_name);
}

void tz_region_destroy(TZ_Region *region) {
	if (region == NULL) return;

	for (int i = 0; i < region->shortname_count; i++) {
		free(region->shortnames[i]);
	}
	free(region->shortnames);
	free(region->records);
	free(region->name);
	tz_rrule_destroy(&region->rrule);
	free(region);
}

TZ_Date tz_get_date(TZ_Time t) {
	uint64_t abs = (uint64_t)(t.time + UNIX_TO_ABSOLUTE);
	uint64_t d = abs / SECONDS_PER_DAY;

	uint64_t n = d / DAYS_PER_400_YEARS;
	uint64_t y = 400 * n;
	d -= DAYS_PER_400_YEARS * n;

	n = d / DAYS_PER_100_YEARS;
	n -= n >> 2;
	y += 100 * n;
	d -= DAYS_PER_100_YEARS * n;

	n = d / DAYS_PER_4_YEARS;
	y += 4 * n;
	d -= DAYS_PER_4_YEARS * n;

	n = d / 365;
	n -= n >> 2;
	y += n;
	d -= 365 * n;

	int64_t year = ((int64_t)y + ABSOLUTE_ZERO_YEAR);
	int64_t year_day = (int64_t)d;

	int64_t day = year_day;

	if (is_leap_year(year)) {
		if (day > 31+29-1) {
			day -= 1;
		} else if (day == 31+29-1) {
			return (TZ_Date){
				.year = year,
				.month = 2,
				.day = 29,
			};
		}
	}

	int64_t month = day / 31;
	int64_t end = days_before[month+1];
	int64_t begin = 0;

	if (day >= end) {
		month += 1;
		begin = end;
	} else {
		begin = days_before[month];
	}
	month += 1;
	day = day - begin + 1;
	return (TZ_Date){
		.year  = year,
		.month = (int8_t)month,
		.day   = (int8_t)day,
	};
}

static int64_t trans_date_to_seconds(int64_t year, TZ_Transition_Date td) {
	bool is_leap = is_leap_year(year);
	int64_t t = year_to_time(year);

	switch (td.type) {
		case TZ_Month_Week_Day: {
			if (td.month < 1) { return 0; }

			t += month_to_seconds(td.month - 1, is_leap);
			int64_t weekday = ((t + (4 * SECONDS_PER_DAY)) % (7 * SECONDS_PER_DAY)) / SECONDS_PER_DAY;
			int64_t days = td.day - weekday;

			if (days < 0) { days += 7; }

			int64_t month_daycount = last_day_of_month(year, td.month);
			int64_t week = td.week;
			if (week == 5 && (days + 28) >= month_daycount) {
				week = 4;
			}

			t += SECONDS_PER_DAY * (days + (7 * (week - 1)));
			t += td.time;

			return t;
		} break;
		case TZ_No_Leap: {
			int64_t day = td.day;
			if (day < 60 || !is_leap) {
				day -= 1;
			}
			t += SECONDS_PER_DAY * day;
			return t;
		} break;
		case TZ_Leap: {
			t += SECONDS_PER_DAY * td.day;
			return t;
		} break;
		default: { return 0; }
	}

	return 0;
}

static TZ_Record process_rrule(TZ_RRule rrule, int64_t cur) {
	if (!rrule.has_dst) {
		return (TZ_Record){
			.time = cur,
			.utc_offset = rrule.std_offset,
			.shortname  = rrule.std_name,
			.dst        = false,
		};
	}

	TZ_Date date = tz_get_date((TZ_Time){.time = cur, .tz = NULL});
	int64_t std_secs = trans_date_to_seconds(date.year, rrule.std_date);
	int64_t dst_secs = trans_date_to_seconds(date.year, rrule.dst_date);

	TZ_Record records[] = {
		{
			.time = std_secs,
			.utc_offset = rrule.std_offset,
			.shortname = rrule.std_name,
			.dst = false,
		},
		{
			.time = dst_secs,
			.utc_offset = rrule.dst_offset,
			.shortname = rrule.dst_name,
			.dst = true,
		}
	};
	if (records[0].time > records[1].time) {
		TZ_Record tmp = records[0];
		records[0] = records[1];
		records[1] = tmp;
	}

	for (int i = 0; i < 2; i++) {
		TZ_Record record = records[i];
		if (cur < record.time) {
			return record;
		}
	}

	return records[0];
}

static TZ_Record region_get_nearest(TZ_Region *tz, int64_t tm) {
	if (tz->record_count == 0) {
		return process_rrule(tz->rrule, tm);
	}

	int64_t n = tz->record_count;

	int64_t tm_sec = tm;
	int64_t last_time = tz->records[tz->record_count-1].time;
	if (tm_sec > last_time) {
		return process_rrule(tz->rrule, tm);
	}

	int64_t left = 0;
	int64_t right = n;
	while (left < right) {
		int64_t mid = (int64_t)((uint64_t)(left + right) >> 1);
		if (tz->records[mid].time < tm_sec) {
			left = mid + 1;
		} else {
			right = mid;
		}
	}

	int64_t idx = MAX(0, left - 1);
	TZ_Record ret = tz->records[idx];
	return ret;
}

TZ_Time tz_time_from_unix_seconds(int64_t time) {
	return (TZ_Time){.time = time, .tz = NULL};
}

TZ_Time tz_time_from_components(TZ_Date date, TZ_HMS hms, TZ_Region *tz) {
	bool is_leap = is_leap_year(date.year);
	int64_t time = year_to_time(date.year);

	time += month_to_seconds(date.month - 1, is_leap);
	time += (date.day - 1) * SECONDS_PER_DAY;

	time += hms.hours * SECONDS_PER_HOUR;
	time += hms.minutes * SECONDS_PER_MINUTE;
	time += hms.seconds;

	return (TZ_Time){.time = time, .tz = tz};
}

TZ_Time tz_time_to_utc(TZ_Time t) {
	if (t.tz == NULL) {
		return t;
	}

	TZ_Record record = region_get_nearest(t.tz, t.time);
	return (TZ_Time){.time = t.time - record.utc_offset, .tz = NULL};
}

int64_t tz_time_to_unix_seconds(TZ_Time t) {
	TZ_Time out_t = tz_time_to_utc(t);
	return out_t.time;
}

TZ_Time tz_time_to_tz(TZ_Time in_t, TZ_Region *tz) {
	TZ_Time t = in_t;
	if (t.tz == tz) {
		return t;
	}
	if (t.tz != NULL) {
		t = tz_time_to_utc(t);
	}
	if (tz == NULL) {
		return t;
	}

	TZ_Record record = region_get_nearest(tz, t.time);
	return (TZ_Time){.time = t.time + record.utc_offset, .tz = tz};
}

char *tz_shortname(TZ_Time t) {
	if (t.tz == NULL) return (char *)"UTC";

	TZ_Record record = region_get_nearest(t.tz, t.time);
	return (record.shortname == NULL) ? (char *)"" : record.shortname;
}

bool tz_is_dst(TZ_Time t) {
	if (t.tz == NULL) return false;

	TZ_Record record = region_get_nearest(t.tz, t.time);
	return record.dst;
}

TZ_HMS tz_get_hms(TZ_Time t) {
	int64_t secs = (t.time + INTERNAL_TO_ABSOLUTE) % SECONDS_PER_DAY;

	int64_t hours = secs / SECONDS_PER_HOUR;
	secs -= hours * SECONDS_PER_HOUR;

	int64_t mins = secs / SECONDS_PER_MINUTE;
	secs -= mins * SECONDS_PER_MINUTE;

	return (TZ_HMS){.hours = (int8_t)hours, .minutes = (int8_t)mins, .seconds = (int8_t)secs};
}
