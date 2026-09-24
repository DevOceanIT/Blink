#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "usage_rate.h"
int main(void) {
 struct usage_rate r = {0};
 usage_rate_add(&r, 1000, 0, 10, 3600);
 usage_rate_add(&r, 1060, 60, 10, 3540);
 assert(r.n == 1); /* Heartbeats are not new observations. */
 usage_rate_add(&r, 1300, 0, 11, 3300);
 assert(usage_rate_get(&r,1300) < 0);
 usage_rate_add(&r, 1600, 0, 12, 3000);
 assert(fabs(usage_rate_get(&r,1600)-12) < .001);
 assert(usage_rate_get(&r,2601) < 0);
 usage_rate_add(&r, 1700, 0, 0, 2900);
 assert(r.n == 1); /* Quota reset or manual credit. */
 usage_rate_add(&r, 2000, 0, 1, 3600);
 assert(r.n == 1); /* Changed reset boundary. */
 usage_rate_add(&r, 3101, 0, 2, 2499);
 assert(r.n == 1); /* Long observation gap. */
 usage_rate_add(&r, 3200, 0, NAN, 2400);
 assert(r.n == 1);
 usage_rate_add(&r, 3401, 0, 2, 2199);
 usage_rate_add(&r, 3701, 0, 2, 1899);
 assert(usage_rate_get(&r,3701) == 0);
 usage_rate_add(&r, 3801, 0, 0, -1);
 assert(r.n == 1 && r.reset_at == 0);
 usage_rate_add(&r, 4101, 0, 1, -1);
 usage_rate_add(&r, 4401, 0, 2, -1);
 assert(fabs(usage_rate_get(&r,4401)-12) < .001);
 puts("Usage rate checks passed");
}
