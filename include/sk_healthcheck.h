#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <ck_rwlock.h>

#include <sk_cc.h>
#include <sk_error.h>
#include <sk_flag.h>

/* The state of a healthcheck. */
enum sk_health {
	/* The check is in an unknown state, probably due to an internal error. */
	SK_HEALTH_UNKNOWN = 0,
	/* The check is healthy. */
	SK_HEALTH_OK,
	/* The check is approaching unhealthy level; action should be taken. */
	SK_HEALTH_WARNING,
	/* The check is unhealthy; action must be taken immediately. */
	SK_HEALTH_CRITICAL,

	/* Do not use, leave at the end */
	SK_HEALTH_COUNT,
};

/*
 * String representation of a health status.
 *
 * @param health, health for which the string representation is requested
 *
 * @return pointer to const string representation, NULL on error
 */
const char *
sk_health_str(enum sk_health health);

/*
 * Health checks are indicator of your application well behaving health.
 *
 * Users implements health checks by mean of a closure. The closure returns
 * the state and optionally provides an error code/message.
 *
 * The callback _must_ be thread-safe as there's no guarantee where and when the
 * closure is run. The callback will likely run in a different thread than
 * where the context was initialized (or modified). A good rule is to ensure
 * that the callback only read atomically in the context.
 *
 * An fictitious example follows:
 *
 * struct db_ctx;
 *
 * enum sk_health db_health(void* ctx, sk_error_t *err) {
 *    if (ctx == NULL)
 *      return SK_HEALTH_UNKNOWN;
 *
 *    struct db_ctx *ctx = (struct db_ctx*)ctx;
 *
 *    if (!db_is_connected(ctx)) {
 *        sk_error_msg_code(err, DB_NOT_CONNECTED, "Not connected to db");
 *        return SK_HEALT_CRITICAL;
 *    }
 *
 *    const float usage = db_connection_usage(ctx);
 *    if (usage > 0.85) {
 *        sk_error_msg_code(err, DB_CONNECTION_POOL, "Pool exhausted");
 *        return (usage > 0.95) ? SK_HEALTH_CRITICAL :
 *                                SK_HEALTH_WARN;
 *    }
 *
 *    return SK_HEALTH_OK;
 * }
 */
typedef enum sk_health (*sk_healthcheck_cb_t)(
	void *ctx, sk_error_t *error);

/* Flags */
enum {
	SK_HEALTHCHECK_ENABLED = 1,
};

struct sk_healthcheck {
	/* The name of the healthcheck. */
	char *name;
	/* A brief description of the healthcheck. */
	char *description;

	sk_flag_t flags;

	/* User provided callback that implements the healthcheck. */
	sk_healthcheck_cb_t callback;
	void *ctx;
};
typedef struct sk_healthcheck sk_healthcheck_t;

/*
 * Initialize a healthcheck.
 *
 * @param healthcheck, healthcheck to initialize
 * @param name, name of the healthcheck
 * @param description, short description of the healthcheck
 * @param flags, flags to initialize the check with
 * @param callback, callback to invoke on polling, see description of type
 *                  sk_healthcheck_cb_t above for more information
 * @param ctx, ctx structure to pass to callback, ownership is transferred to
 *             the created healtcheck (will be freed by sk_healthcheck_destroy)
 * @param error, error to store failure information
 *
 * @return true on success, false otherwise and set error
 *
 * @errors SK_ERROR_ENOMEN, if memory allocation failed
 */
bool
sk_healthcheck_init(sk_healthcheck_t *healthcheck, const char *name,
	const char *description, sk_flag_t flags, sk_healthcheck_cb_t callback,
	void *ctx, sk_error_t *error) sk_nonnull(1, 2, 5, 6);

/*
 * Free a healthcheck.
 *
 * @param healthcheck, healthcheck to free
 */
void
sk_healthcheck_destroy(sk_healthcheck_t *healthcheck) sk_nonnull(1);

/*
 * Poll a healthcheck for status.
 *
 * @param healthcheck, healthcheck to poll
 * @param state, health state of the check
 * @param error, error to store failure information
 *
 * @return true on success, false otherwise and set error
 *
 * @errors SK_ERROR_EAGAIN, if healthcheck is disabled
 *
 * Note that the callback can also set an error, thus depending on the health
 * state, one might also check the error.
 */
bool
sk_healthcheck_poll(sk_healthcheck_t *healthcheck, enum sk_health *state,
	sk_error_t *error) sk_nonnull(1, 2, 3);

#define sk_healthcheck_enable(hc)                                              \
	sk_flag_set((&(hc)->flags), SK_HEALTHCHECK_ENABLED)

#define sk_healthcheck_disable(hc)                                             \
	sk_flag_unset((&(hc)->flags), SK_HEALTHCHECK_ENABLED)
