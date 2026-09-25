#ifndef LOCAL_FEED_H
#define LOCAL_FEED_H

#include <stdint.h>
#include <stdbool.h>

#include "cfg_store.h"

struct local_feed_usage {
	char provider[16];
	char provider2[16];
	char state[16];
	char label[28];
	double session_pct;
	double weekly_pct;
	double fable_pct;
	double p2_session_pct;
	double p2_weekly_pct;
	double burn_pph;
	int32_t session_resets_in_s;
	int32_t weekly_resets_in_s;
	int32_t p2_session_resets_in_s;
	int32_t p2_weekly_resets_in_s;
	int32_t age_s;
	int32_t p2_age_s;
	int32_t active_age_s;
	int n_sess;
	int n_agents;
	int label_count;
	int n_run;
	int n_wait;
	int n_stuck;
	bool stale;
	bool p2_stale;
};

/* Fetch and authenticate one host frame. The key is raw 32-byte pairing data. */
int local_feed_fetch(const char *host, uint16_t port,
		     const uint8_t key[CFG_FEED_KEY_LEN],
		     struct local_feed_usage *out);

#endif
