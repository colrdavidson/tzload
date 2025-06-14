#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define TZIF_MAGIC 0x545A6966
#define BIG_BANG_ISH -0x800000000000000

typedef struct {
	uint8_t *data;
	uint64_t len;
} Slice;

Slice slice_sub(Slice s, uint64_t start_idx) {
	return (Slice){.data = s.data + start_idx, .len = s.len - start_idx};
}

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
} TZ_Region;

typedef struct {
	time_t time;
	TZ_Region *tz;
} DateTime;

void print_tz_record(TZ_Record record) {
	printf("record:\n");
	printf("- time:      %lld\n", record.time);
	printf("- offset:    %lld\n", record.utc_offset);
	printf("- shortname: %s\n", record.shortname);
	printf("- dst?:      %s\n", record.dst ? "true" : "false");
}

void print_tz_region(TZ_Region *region) {
	printf("Region: %s\n", region->name);
	printf("records:\n");
	for (int i = 0; i < region->record_count; i++) {
		TZ_Record record = region->records[i];
		print_tz_record(record);
	}
}

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

void tzif_hdr_to_native(TZif_Header *hdr) {
	hdr->magic = ntohl(hdr->magic);
	hdr->isutcnt = ntohl(hdr->isutcnt);
	hdr->isstdcnt = ntohl(hdr->isstdcnt);
	hdr->leapcnt = ntohl(hdr->leapcnt);
	hdr->timecnt = ntohl(hdr->timecnt);
	hdr->typecnt = ntohl(hdr->typecnt);
	hdr->charcnt = ntohl(hdr->charcnt);
}

void print_tzif_hdr(TZif_Header *hdr) {
	printf("TZif_Header\n");
	printf("- version:  %u\n", hdr->version);
	printf("- isutcnt:  %u\n", hdr->isutcnt);
	printf("- isstdcnt: %u\n", hdr->isstdcnt);
	printf("- leapcnt:  %u\n", hdr->leapcnt);
	printf("- timecnt:  %u\n", hdr->timecnt);
	printf("- typecnt:  %u\n", hdr->typecnt);
	printf("- charcnt:  %u\n", hdr->charcnt);
}

int tzif_data_block_size(TZif_Header *hdr, TZif_Version version) {
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

	int real_block_size = tzif_data_block_size(real_hdr, v1_hdr->version);
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

	int end_idx = 0;
	for (int i = 0; i < s.len; i++) {
		char ch = (char)s.data[i];
		if (ch == '\n') {
			break;
		}

		if (ch == 0) {
			return false;
		}

		end_idx += 1;
	}
	char *footer_str = (char *)s.data;

	// UTC is a special case, we don't need to alloc
	if (real_hdr->typecnt == 1 && local_time_types[0].utoff == 0) {
		*out_region = NULL;
		return true;
	}

	Slice str_table = {.data = (uint8_t *)timezone_string_table, .len = real_hdr->charcnt};
	char **ltt_names = malloc(sizeof(char *) * real_hdr->typecnt);
	for (int i = 0; i < real_hdr->typecnt; i++) {
		Local_Time_Type ltt = local_time_types[i];
		char *ltt_name = timezone_string_table + ltt.idx;

		Slice str = slice_sub(str_table, ltt.idx);
		ltt_names[i] = strndup((char *)str.data, str.len);
	}

	TZ_Record *records = malloc(real_hdr->timecnt * sizeof(TZ_Record));
	for (int i = 0; i < real_hdr->timecnt; i++) {
		int64_t trans_time = transition_times[i];
		int trans_idx = transition_types[i];
		Local_Time_Type ltt = local_time_types[trans_idx];

		records[i] = (TZ_Record){
			.time       = trans_time,
			.utc_offset = ltt.utoff,
			.shortname  = ltt_names[trans_idx],
			.dst        = ltt.dst,
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

typedef struct {
	char **strs;
	uint64_t len;
	uint64_t cap;
} DynArr;

void dynarr_append(DynArr *dyn, char *str) {
	if (dyn->len + 1 > dyn->cap) {
		dyn->cap = MAX(8, dyn->cap * 2);
		dyn->strs = realloc(dyn->strs, sizeof(char *) * dyn->cap);
	}
	dyn->strs[dyn->len] = str;
	dyn->len += 1;
}

char *local_tz_name(void) {
	char *local_str = getenv("TZ");
	if (local_str != NULL) {
		return strdup(local_str);
	}

	char *local_path = realpath("/etc/localtime", NULL);

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

bool load_tzif_file(char *path, char *name, TZ_Region **region) {
	int zone_fd = open(path, O_RDONLY);
	if (zone_fd < 0) {
		printf("Failed to open %s\n", path);
		return false;
	}

	uint64_t file_length = lseek(zone_fd, 0, SEEK_END);
	lseek(zone_fd, 0, SEEK_SET);

	uint8_t *buffer = malloc(file_length);
	read(zone_fd, buffer, file_length);

	bool ret = parse_tzif(buffer, file_length, name, region);

	free(buffer);
	close(zone_fd);

	return ret;
}

bool region_load(char *region_name, TZ_Region **region) {
	if (!strcmp(region_name, "UTC")) {
		*region = NULL;
		return true;
	}

	char *reg_str = NULL;
	if (!strcmp(region_name, "local")) {
		reg_str = local_tz_name();
		if (!strcmp(reg_str, "UTC")) {
			free(reg_str);

			*region = NULL;
			return true;
		}
	} else {
		reg_str = strdup(region_name);
	}

	char *region_path;
	asprintf(&region_path, "%s/%s", "/usr/share/zoneinfo", reg_str);

	bool ret = load_tzif_file(region_path, reg_str, region);

	free(reg_str);
	free(region_path);

	return ret;
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

	printf("%s\n", local_region->name);
	return 0;
}
