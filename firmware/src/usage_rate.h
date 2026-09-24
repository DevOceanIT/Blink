#ifndef BLINK_USAGE_RATE_H
#define BLINK_USAGE_RATE_H
#include <stdint.h>
#include <string.h>

/* One hour of minute-spaced source observations, not USB heartbeats.
 * Drops, changed reset boundaries and long gaps start a new series. */
struct usage_rate {
	int64_t at[64];
	double pct[64];
	int n;
	int64_t reset_at;
};

static inline void usage_rate_add(struct usage_rate *r, int64_t now,
				 int32_t age, double pct, int32_t reset)
{
	if (age < 0 || age > 1000 || !(pct >= 0 && pct <= 1000)) return;
	int64_t at = now - age;
	int64_t boundary = reset >= 0 ? now + reset : 0;
	if (r->n && (at < r->at[r->n-1] - 2 ||
		pct < r->pct[r->n-1] || at - r->at[r->n-1] > 1000 ||
		(boundary && r->reset_at &&
		 (boundary > r->reset_at + 120 || boundary < r->reset_at - 120)))) {
		r->n = 0;
		r->reset_at = 0;
	}
	if (boundary) r->reset_at = boundary;
	if (r->n && at - r->at[r->n-1] < 60) return;
	while (r->n && (at - r->at[0] > 3600 || r->n == 64)) {
		--r->n;
		memmove(r->at, r->at+1, r->n * sizeof(r->at[0]));
		memmove(r->pct, r->pct+1, r->n * sizeof(r->pct[0]));
	}
	r->at[r->n] = at;
	r->pct[r->n++] = pct;
}

static inline double usage_rate_get(const struct usage_rate *r, int64_t now)
{
	if (r->n < 3 || now - r->at[r->n-1] > 1000 ||
	    (r->reset_at && now >= r->reset_at)) return -1;
	int64_t span = r->at[r->n-1] - r->at[0];
	if (span < 600) return -1;
	return (r->pct[r->n-1] - r->pct[0]) * 3600.0 / span;
}
#endif
