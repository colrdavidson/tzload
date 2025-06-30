# Threadsafe Timezone Conversion Library for C

`tz_region_load` loads a timezone using IANA names and your system's IANA tzdb - (currently supported on Linux, FreeBSD, and OSX)
-- special case: use region "local" to get your local timezone

`tz_region_load_from_file` and `tz_region_load_from_buffer` allow you to bundle your own IANA tzdb into your application if desired

`tz_time_new`    creates a TZ_Time, taking seconds from unix-epoch in UTC
`tz_time_to_utc` converts a TZ_Time to UTC
`tz_time_to_tz`  converts a TZ_Time to the provided timezone

`tz_get_date`  gets the year, month and day from the TZ_Time
`tz_get_hms`   gets the hour, minute and second from the TZ_Time
`tz_shortname` gets the shortname (ex: PST / PDT) from the TZ_Time
`tz_is_dst`    checks if the time is in daylight savings

`tz_time_to_str` convienience timestamp printer for debugging
