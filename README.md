# Threadsafe Timezone Conversion Library for C

`tz_region_load` loads a timezone using IANA names and your system's IANA tzdb (currently supported on Linux, FreeBSD, and OSX)  
**NOTE:** use region "local" to get your local timezone  

`tz_region_load_from_file` and `tz_region_load_from_buffer` allow you to bundle your own IANA tzdb into your application if desired  

`tz_time_new`    creates a TZ_Time, taking seconds from unix-epoch in UTC  
`tz_time_to_utc` converts a TZ_Time to UTC  
`tz_time_to_tz`  converts a TZ_Time to the provided timezone  

`tz_get_date`  gets the year, month and day from the TZ_Time  
`tz_get_hms`   gets the hour, minute and second from the TZ_Time  
`tz_shortname` gets the shortname (ex: PST / PDT) from the TZ_Time  
`tz_is_dst`    checks if the time is in daylight savings  

`tz_time_to_str` convienience timestamp printer for debugging  

usage example:
```C
void print_time(TZ_Time t) {
	TZ_Date date = tz_get_date(t);
	TZ_HMS hms = tz_get_hms(t);
	printf("%02d-%02d-%04lld @ %02d:%02d:%02d %s\n",
		date.month, date.day,    date.year,
		hms.hours,  hms.minutes, hms.seconds,
		tz_shortname(t)
	);
}

int main(void) {
	TZ_Region *local = NULL;
	if (!tz_region_load("local", &local)) return 1;

	TZ_Region *est = NULL;
	if (!tz_region_load("US/Eastern", &est)) return 1;

	TZ_Time utc_now     = tz_time_new(time(NULL));
	TZ_Time local_now   = tz_time_to_tz(utc_now, local);
	TZ_Time est_now     = tz_time_to_tz(local_now, est);
	TZ_Time back_to_utc = tz_time_to_utc(est_now);

	print_time(utc_now);
	print_time(local_now);
	print_time(est_now);
	print_time(back_to_utc);

	return 0;
}
```
