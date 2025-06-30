#include <stdio.h>

#include "libtz.h"

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

	TZ_Region *est_region = NULL;
	if (!region_load("Pacific/Tongatapu", &est_region)) {
		printf("Failed to load %s!\n", "Pacific/Tongatapu");
		return 1;
	}

	DateTime utc_now   = datetime_now();
	DateTime local_now = datetime_to_tz(utc_now, local_region);
	DateTime est_now   = datetime_to_tz(utc_now, est_region);
	DateTime back_now  = datetime_to_tz(est_now, NULL);

	printf("%s\n", datetime_to_str(utc_now));
	printf("%s\n", datetime_to_str(local_now));
	printf("%s\n", datetime_to_str(est_now));
	printf("%s\n", datetime_to_str(back_now));
	return 0;
}
