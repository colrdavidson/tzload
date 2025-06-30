#include <stdio.h>
#include <string.h>
#include <time.h>

#include "libtz.h"

void print_time(TZ_Time t) {
	TZ_Date date = tz_get_date(t);
	TZ_HMS hms = tz_get_hms(t);
	printf("%02d-%02d-%04lld @ %02d:%02d:%02d %s\n",
		date.month, date.day,    date.year,
		hms.hours,  hms.minutes, hms.seconds,
		tz_shortname(t)
	);
}

int main(int argc, char **argv) {
	char *tz_name;
	if (argc < 2) {
		tz_name = (char *)"local";
	} else {
		tz_name = argv[1];
	}

	TZ_Region *local_region = NULL;
	if (!tz_region_load(tz_name, &local_region)) {
		printf("Failed to load %s!\n", tz_name);
		return 1;
	}

	TZ_Region *other_region = NULL;
	if (!tz_region_load((char *)"US/Eastern", &other_region)) {
		printf("Failed to load %s!\n", "US/Eastern");
		return 1;
	}

	TZ_Time utc_now   = tz_time_new(time(NULL));
	TZ_Time local_now = tz_time_to_tz(utc_now, local_region);
	TZ_Time other_now = tz_time_to_tz(local_now, other_region);
	TZ_Time back_now  = tz_time_to_utc(other_now);

	print_time(utc_now);
	print_time(local_now);
	print_time(other_now);
	print_time(back_now);

	tz_region_destroy(local_region);
	tz_region_destroy(other_region);

	return 0;
}
