#include <stdio.h>
#include <string.h>
#include <time.h>

#include "libtz.h"

char *region_name(TZ_Region *tz) {
	if (tz == NULL) return "UTC";
	return tz->name;
}

bool tz_trans_date_eq(TZ_Transition_Date a, TZ_Transition_Date b) {
	if (a.type != b.type) return false;
	if (a.month != b.month) return false;
	if (a.week != b.week) return false;
	if (a.day != b.day) return false;
	if (a.time != b.time) return false;

	return true;
}

bool streq(char *a, char *b) {
	if (a == b) return true;
	return strcmp(a, b) == 0;
}

bool rrule_eq(TZ_RRule a, TZ_RRule b) {
	if (a.has_dst != b.has_dst) return false;

	if (!streq(a.std_name, b.std_name)) return false;
	if (a.std_offset != b.std_offset) return false;
	if (!tz_trans_date_eq(a.std_date, b.std_date)) return false;

	if (!streq(a.dst_name, b.dst_name)) return false;
	if (a.dst_offset != b.dst_offset) return false;
	if (!tz_trans_date_eq(a.dst_date, b.dst_date)) return false;

	return true;
}

int main(int argc, char **argv) {
	char *tzif_path = NULL;
	char *tz_dir = "/usr/share/zoneinfo";

	char *tz_name;
	if (argc < 2) {
		tz_name = "local";
	} else {
		tz_name = argv[1];
	}

	TZ_Region *local_region = NULL;
	if (!region_load(tz_name, &local_region)) {
		printf("Failed to load %s!\n", tz_name);
		return 1;
	}

	TZ_Region *other_region = NULL;
	if (!region_load("Asia/Tokyo", &other_region)) {
		printf("Failed to load %s!\n", "Asia/Tokyo");
		return 1;
	}

	DateTime utc_now   = datetime_new(time(NULL));
	DateTime local_now = datetime_to_tz(utc_now, local_region);
	DateTime other_now   = datetime_to_tz(utc_now, other_region);
	DateTime back_now  = datetime_to_tz(other_now, NULL);

	printf("%s | %s\n", datetime_to_str(utc_now),   region_name(utc_now.tz));
	printf("%s | %s\n", datetime_to_str(local_now), region_name(local_now.tz));
	printf("%s | %s\n", datetime_to_str(other_now), region_name(other_now.tz));
	printf("%s | %s\n", datetime_to_str(back_now),  region_name(back_now.tz));

	return 0;
}
