/* Authenticated, local-only HTTP bridge. No provider token crosses this link. */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <mbedtls/md.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "local_feed.h"
#include "msg_parse.h"

#define FEED_PATH "/v1/usage"
#define FEED_BODY_MAX 768
#define FEED_REPLY_MAX 1600

static char reply[FEED_REPLY_MAX];

static int hmac_hex(const uint8_t key[CFG_FEED_KEY_LEN], const char *msg,
		    size_t len, char out[65])
{
	uint8_t digest[32];
	const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	if (!info || mbedtls_md_hmac(info, key, CFG_FEED_KEY_LEN,
				    (const unsigned char *)msg, len, digest) != 0) {
		return -EIO;
	}
	for (size_t i = 0; i < sizeof(digest); i++) {
		snprintf(out + i * 2, 3, "%02x", digest[i]);
	}
	memset(digest, 0, sizeof(digest));
	return 0;
}

static bool hex_equal(const char *actual, size_t actual_len,
		     const char *expected)
{
	uint8_t diff = 0;
	if (actual_len != 64) {
		return false;
	}
	for (size_t i = 0; i < 64; i++) {
		diff |= (uint8_t)(actual[i] ^ expected[i]);
	}
	return diff == 0;
}

static int send_all(int fd, const char *data, size_t len)
{
	while (len) {
		ssize_t n = zsock_send(fd, data, len, 0);
		if (n <= 0) {
			return -EIO;
		}
		data += n;
		len -= (size_t)n;
	}
	return 0;
}

static int request_frame(const char *host, uint16_t port,
			const char *nonce, const char *auth,
			size_t *used)
{
	struct sockaddr_in addr = { .sin_family = AF_INET,
					    .sin_port = htons(port) };
	if (zsock_inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		return -EINVAL;
	}
	int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		return -errno;
	}
	struct timeval timeout = { .tv_sec = 8, .tv_usec = 0 };
	(void)zsock_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	(void)zsock_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	if (zsock_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		zsock_close(fd);
		return -EHOSTUNREACH;
	}
	char req[320];
	int n = snprintf(req, sizeof(req),
			 "GET " FEED_PATH " HTTP/1.1\r\nHost: %s:%u\r\n"
			 "X-Blink-Nonce: %s\r\nX-Blink-Auth: %s\r\n"
			 "Connection: close\r\n\r\n", host, port, nonce, auth);
	if (n < 0 || (size_t)n >= sizeof(req) || send_all(fd, req, (size_t)n) != 0) {
		zsock_close(fd);
		return -EIO;
	}
	*used = 0;
	while (*used < sizeof(reply) - 1) {
		ssize_t got = zsock_recv(fd, reply + *used,
					 sizeof(reply) - 1 - *used, 0);
		if (got == 0) {
			break;
		}
		if (got < 0) {
			zsock_close(fd);
			return -EIO;
		}
		*used += (size_t)got;
	}
	zsock_close(fd);
	if (*used >= sizeof(reply) - 1) {
		return -E2BIG;
	}
	reply[*used] = '\0';
	return 0;
}

static int parse_verified(const char *nonce, const uint8_t key[CFG_FEED_KEY_LEN],
			  size_t reply_len, struct local_feed_usage *out)
{
	char *split = strstr(reply, "\r\n\r\n");
	if (!split) {
		return -EBADMSG;
	}
	*split = '\0';
	char *body = split + 4;
	size_t body_len = reply_len - (size_t)(body - reply);
	if (body_len == 0 || body_len > FEED_BODY_MAX) {
		return -E2BIG;
	}
	if (strncmp(reply, "HTTP/1.1 200 ", 13) != 0 &&
	    strncmp(reply, "HTTP/1.0 200 ", 13) != 0) {
		return -EHOSTDOWN;
	}
	char *echo = strstr(reply, "\r\nX-Blink-Nonce: ");
	char *mac = strstr(reply, "\r\nX-Blink-Auth: ");
	if (!echo || !mac) {
		return -EACCES;
	}
	echo += strlen("\r\nX-Blink-Nonce: ");
	mac += strlen("\r\nX-Blink-Auth: ");
	char *eol = strstr(echo, "\r\n");
	char *mend = strstr(mac, "\r\n");
	if (!eol || !mend || (size_t)(eol - echo) != strlen(nonce) ||
	    memcmp(echo, nonce, strlen(nonce)) != 0 ||
	    (size_t)(mend - mac) != 64) {
		return -EACCES;
	}
	char signed_body[FEED_BODY_MAX + 40];
	memcpy(signed_body, nonce, strlen(nonce));
	signed_body[strlen(nonce)] = '\n';
	memcpy(signed_body + strlen(nonce) + 1, body, body_len);
	char expected[65];
	int rc = hmac_hex(key, signed_body, strlen(nonce) + 1 + body_len, expected);
	memset(signed_body, 0, sizeof(signed_body));
	if (rc || !hex_equal(mac, (size_t)(mend - mac), expected)) {
		memset(expected, 0, sizeof(expected));
		return -EACCES;
	}
	memset(expected, 0, sizeof(expected));

	/* The host wrapper and the inner object are both JSON. Check the wrapper
	 * explicitly before using the shared flat-key parser on the usage member. */
	if (!strstr(body, "\"usage\":{") && !strstr(body, "\"usage\" : {")) {
		return -EBADMSG;
	}
	double v;
	if (!msg_get_double(body, "session_pct", &v) || v < 0 || v > 1000) {
		return -EBADMSG;
	}
	out->session_pct = v;
	if (!msg_get_double(body, "weekly_pct", &v) || v < 0 || v > 1000) {
		return -EBADMSG;
	}
	out->weekly_pct = v;
	out->session_resets_in_s = -1;
	out->weekly_resets_in_s = -1;
	out->fable_pct = -1;
	out->provider[0] = '\0';
	out->provider2[0] = '\0';
	out->state[0] = '\0';
	out->label[0] = '\0';
	out->p2_session_pct = -1;
	out->p2_weekly_pct = -1;
	out->p2_session_resets_in_s = -1;
	out->p2_weekly_resets_in_s = -1;
	out->p2_age_s = -1;
	out->active_age_s = -1;
	out->burn_pph = 0;
	out->n_sess = 0;
	out->n_agents = 0;
	out->label_count = 0;
	out->n_run = 0;
	out->n_wait = 0;
	out->n_stuck = 0;
	out->stale = false;
	out->p2_stale = false;
	out->age_s = -1;
	if (msg_get_double(body, "session_resets_in_s", &v) && v >= -1 && v <= 2147483647.0) {
		out->session_resets_in_s = (int32_t)v;
	}
	if (msg_get_double(body, "weekly_resets_in_s", &v) && v >= -1 && v <= 2147483647.0) {
		out->weekly_resets_in_s = (int32_t)v;
	}
	if (msg_get_double(body, "fable_pct", &v) && v >= -1 && v <= 1000) {
		out->fable_pct = v;
	}
	(void)msg_get_str(body, "provider", out->provider, sizeof(out->provider));
	(void)msg_get_str(body, "p2", out->provider2, sizeof(out->provider2));
	(void)msg_get_str(body, "state", out->state, sizeof(out->state));
	(void)msg_get_str(body, "label", out->label, sizeof(out->label));
	if (msg_get_double(body, "p2_session_pct", &v) && v >= -1 && v <= 1000) {
		out->p2_session_pct = v;
	}
	if (msg_get_double(body, "p2_weekly_pct", &v) && v >= -1 && v <= 1000) {
		out->p2_weekly_pct = v;
	}
	if (msg_get_double(body, "p2_s_in_s", &v) && v >= -1 && v <= 2147483647.0) {
		out->p2_session_resets_in_s = (int32_t)v;
	}
	if (msg_get_double(body, "p2_w_in_s", &v) && v >= -1 && v <= 2147483647.0) {
		out->p2_weekly_resets_in_s = (int32_t)v;
	}
	if (msg_get_double(body, "p2_age_s", &v) && v >= -1 && v <= 2147483647.0) {
		out->p2_age_s = (int32_t)v;
	}
	if (msg_get_double(body, "active_age_s", &v) && v >= -1 && v <= 2147483647.0) {
		out->active_age_s = (int32_t)v;
	}
	if (msg_get_double(body, "burn_pph", &v) && v >= 0 && v <= 9999) {
		out->burn_pph = v;
	}
	if (msg_get_double(body, "n_sess", &v) && v >= 0 && v <= 9999) {
		out->n_sess = (int)v;
	}
	if (msg_get_double(body, "n_agents", &v) && v >= 0 && v <= 9999) {
		out->n_agents = (int)v;
	}
	if (msg_get_double(body, "n", &v) && v >= 0 && v <= 9999) {
		out->label_count = (int)v;
	}
	if (msg_get_double(body, "n_run", &v) && v >= 0 && v <= 9999) {
		out->n_run = (int)v;
	}
	if (msg_get_double(body, "n_wait", &v) && v >= 0 && v <= 9999) {
		out->n_wait = (int)v;
	}
	if (msg_get_double(body, "n_stuck", &v) && v >= 0 && v <= 9999) {
		out->n_stuck = (int)v;
	}
	(void)msg_get_bool(body, "stale", &out->stale);
	(void)msg_get_bool(body, "p2_stale", &out->p2_stale);
	if (msg_get_double(body, "age_s", &v) && v >= 0 && v <= 2147483647.0) {
		out->age_s = (int32_t)v;
	}
	return 0;
}

int local_feed_fetch(const char *host, uint16_t port,
		     const uint8_t key[CFG_FEED_KEY_LEN],
		     struct local_feed_usage *out)
{
	uint8_t rnd[16];
	char nonce[33], request_data[64], auth[65];
	sys_rand_get(rnd, sizeof(rnd));
	for (size_t i = 0; i < sizeof(rnd); i++) {
		snprintf(nonce + i * 2, 3, "%02x", rnd[i]);
	}
	memset(rnd, 0, sizeof(rnd));
	int n = snprintf(request_data, sizeof(request_data),
			 "GET\n" FEED_PATH "\n%s", nonce);
	if (n < 0 || (size_t)n >= sizeof(request_data) ||
	    hmac_hex(key, request_data, (size_t)n, auth) != 0) {
		return -EIO;
	}
	memset(request_data, 0, sizeof(request_data));
	size_t reply_len;
	int rc = request_frame(host, port, nonce, auth, &reply_len);
	memset(auth, 0, sizeof(auth));
	if (rc != 0) {
		return rc;
	}
	return parse_verified(nonce, key, reply_len, out);
}
