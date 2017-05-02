#include <assert.h>
#include <error.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ck_pr.h>

#include <sk_lifecycle.h>

// clang-format off
static const char *state_labels[] =
{
	[SK_STATE_NEW] = "new",
	[SK_STATE_STARTING] = "starting",
	[SK_STATE_RUNNING] = "running",
	[SK_STATE_STOPPING] = "stopping",
	[SK_STATE_TERMINATED] = "terminated",
	[SK_STATE_FAILED] = "failed",
};
// clang-format on

const char *
sk_state_str(enum sk_state state)
{
	return (state < SK_STATE_COUNT) ? state_labels[state] : NULL;
}

static_assert(sizeof(sk_lifecycle_t) % 64 == 0,
	"sk_lifecycle_t must be multiple of a cacheline");

bool
sk_lifecycle_init(sk_lifecycle_t *lfc, sk_error_t *error)
{
	memset(lfc, 0, sizeof(*lfc));

	CK_SLIST_INIT(&lfc->listeners);
	ck_rwlock_init(&lfc->lock);

	time_t now = time(NULL);
	if (now == -1)
		return sk_error_msg_code(error, "time(2) failed", SK_LIFECYCLE_EFAULT);
	static_assert(SK_STATE_NEW == 0, "implicitely set with memset");
	ck_pr_store_64((uint64_t *)&lfc->epochs[SK_STATE_NEW], (uint64_t)now);

	return true;
}

static bool
valid_transition(enum sk_state old, enum sk_state new);

static_assert(sizeof(time_t) == sizeof(uint64_t),
	"required due to the usage of ck_pr_store_64");

bool
sk_lifecycle_set_at_epoch(
	sk_lifecycle_t *lfc, enum sk_state new_state, time_t epoch, sk_error_t *err)
{
	if (epoch <= 0)
		return sk_error_msg_code(
			err, "epoch lower than 0", SK_LIFECYCLE_EINVAL);

	ck_rwlock_write_lock(&lfc->lock);

	const enum sk_state current_state = sk_lifecycle_get(lfc);
	if (!valid_transition(current_state, new_state)) {
		ck_rwlock_write_unlock(&lfc->lock);
		return sk_error_msg_code(
			err, "state machine advanced", SK_LIFECYCLE_EINVAL);
	}

	ck_pr_store_int((int *)&lfc->state, new_state);
	ck_pr_store_64((uint64_t *)&lfc->epochs[new_state], (uint64_t)epoch);

	sk_lifecycle_listener_t *listener;
	CK_SLIST_FOREACH(listener, &lfc->listeners, next)
	{
		listener->callback(listener->ctx, new_state, epoch);
	}

	ck_rwlock_write_unlock(&lfc->lock);

	return true;
}

bool
sk_lifecycle_set(sk_lifecycle_t *lfc, enum sk_state new_state, sk_error_t *err)
{
	time_t now = time(NULL);
	if (now == -1)
		return sk_error_msg_code(err, "time(2) failed", SK_LIFECYCLE_EFAULT);

	return sk_lifecycle_set_at_epoch(lfc, new_state, now, err);
}

static_assert(sizeof(enum sk_state) == sizeof(int),
	"sizeof(enum sk_state) must sizeof(int)");

enum sk_state
sk_lifecycle_get(const sk_lifecycle_t *lfc)
{
	return ck_pr_load_int((int *)&lfc->state);
}

time_t
sk_lifecycle_get_epoch(const sk_lifecycle_t *lfc, enum sk_state state)
{
	const enum sk_state current_state = sk_lifecycle_get(lfc);
	if (state > current_state)
		return 0;

	return ck_pr_load_64((uint64_t *)&lfc->epochs[state]);
}

static inline bool
valid_transition(enum sk_state from, enum sk_state to)
{
	switch (to) {
	case SK_STATE_STARTING:
		return (from == SK_STATE_NEW);
	case SK_STATE_RUNNING:
		return (from == SK_STATE_STARTING);
	case SK_STATE_STOPPING:
		return (from == SK_STATE_RUNNING);
	case SK_STATE_TERMINATED:
		return (from == SK_STATE_STOPPING);
	case SK_STATE_FAILED:
		return from != SK_STATE_FAILED;
	default:
		return false;
	}
}

sk_lifecycle_listener_t *
sk_lifecycle_register_listener(sk_lifecycle_t *lfc, const char *name,
	sk_lifecycle_listener_cb_t callback, void *ctx, sk_error_t *error)
{

	sk_lifecycle_listener_t *listener;
	if ((listener = calloc(1, sizeof(*listener))) == NULL) {
		sk_error_msg_code(error, "listener calloc failed", SK_LIFECYCLE_ENOMEM);
		return NULL;
	}

	if ((listener->name = strdup(name)) == NULL) {
		sk_error_msg_code(error, "name strdup failed", SK_LIFECYCLE_ENOMEM);
		goto name_alloc_failed;
	}

	listener->callback = callback;
	listener->ctx = ctx;

	ck_rwlock_write_lock(&lfc->lock);
	CK_SLIST_INSERT_HEAD(&lfc->listeners, listener, next);
	ck_rwlock_write_unlock(&lfc->lock);

	return listener;

name_alloc_failed:
	free(listener);
	return NULL;
}

void
sk_lifecycle_unregister_listener(
	sk_lifecycle_t *lfc, sk_lifecycle_listener_t *listener)
{
	ck_rwlock_write_lock(&lfc->lock);

	CK_SLIST_REMOVE(&lfc->listeners, listener, sk_lifecycle_listener, next);

	ck_rwlock_write_unlock(&lfc->lock);
}
