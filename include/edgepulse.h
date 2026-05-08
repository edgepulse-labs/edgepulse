#ifndef EDGEPULSE_H
#define EDGEPULSE_H

#define EDGEPULSE_VERSION "0.1.0-dev"
#define EDGEPULSE_STATE_DIR "/tmp/edgepulse"
#define EDGEPULSE_STATUS_PATH "/tmp/edgepulse/edgepulse.json"
#define EDGEPULSE_DEFAULT_INTERVAL_SEC 5

struct edgepulse_snapshot {
	double uptime_sec;
	double load1;
	double load5;
	double load15;
	unsigned long mem_total_kb;
	unsigned long mem_available_kb;
	unsigned long mem_free_kb;
};

#endif

