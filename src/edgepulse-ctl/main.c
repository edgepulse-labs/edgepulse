#include "edgepulse.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int print_status(void)
{
	struct edgepulse_snapshot snapshot;

	if (edgepulse_collect_snapshot(&snapshot) != 0) {
		fprintf(stderr, "edgepulse-ctl: failed to collect status snapshot\n");
		return 1;
	}

	edgepulse_write_snapshot_json(stdout, &snapshot);
	return 0;
}

static int print_latest(void)
{
	char buffer[4096];
	size_t nread;
	FILE *fp = fopen(EDGEPULSE_STATUS_PATH, "r");

	if (!fp) {
		fprintf(stderr, "edgepulse-ctl: cannot read %s: %s\n",
			EDGEPULSE_STATUS_PATH, strerror(errno));
		return 1;
	}

	while ((nread = fread(buffer, 1, sizeof(buffer), fp)) > 0)
		fwrite(buffer, 1, nread, stdout);

	if (ferror(fp)) {
		fclose(fp);
		return 1;
	}

	fclose(fp);
	return 0;
}

static int print_features(void)
{
	printf("{\n");
	printf("  \"features\": [],\n");
	printf("  \"status\": \"not_implemented\"\n");
	printf("}\n");
	return 0;
}

static int print_export(void)
{
	printf("device_id,window_start,window_end\n");
	return 0;
}

static void print_usage(FILE *fp)
{
	fprintf(fp, "Usage: edgepulse-ctl <status|latest|features|export|version> [--json]\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		print_usage(stderr);
		return 2;
	}

	if (argc >= 3 && strcmp(argv[2], "--json") != 0) {
		print_usage(stderr);
		return 2;
	}

	if (strcmp(argv[1], "status") == 0)
		return print_status();

	if (strcmp(argv[1], "latest") == 0)
		return print_latest();

	if (strcmp(argv[1], "features") == 0)
		return print_features();

	if (strcmp(argv[1], "export") == 0)
		return print_export();

	if (strcmp(argv[1], "version") == 0) {
		printf("%s\n", EDGEPULSE_VERSION);
		return 0;
	}

	print_usage(stderr);
	return 2;
}
