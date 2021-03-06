#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include <sk_cc.h>
#include <sk_error.h>
#include <sk_listener.h>

/*
 * Lifecycle is a thread safe state machine representing the operational state
 * of a component. A lifecycle might serve many purposes:
 *
 * - Improve auditing via event logs
 * - Automatically toggles healthcheck in STARTING and STOPPING transitions
 * - Centralize the exit condition of a main loop
 *
 * The possibles transitions are given by the following state machine:
 *
 *   NEW → STARTING → RUNNING → STOPPING → TERMINATED
 *    └───────┴──────────┴─────────┴─────→ FAILED
 */

enum sk_state {
	/*
	 * A component in this state is inactive. It does minimal work and consumes
	 * minimal resources.
	 */
	SK_STATE_NEW = 0,
	/* A component in this state is transitioning to SK_STATE_RUNNING. */
	SK_STATE_STARTING,
	/* A service in this state is operational. */
	SK_STATE_RUNNING,
	/* A service in this state is transitioning to SK_STATE_TERMINATED. */
	SK_STATE_STOPPING,
	/*
	 * A service in this state has completed execution normally. It does
	 * minimal work and consumes minimal resources.
	 */
	SK_STATE_TERMINATED,
	/*
	 * A service in this state has encountered a problem and may not be
	 * operational. It cannot be started nor stopped.
	 */
	SK_STATE_FAILED,

	/* Do not use, leave at the end */
	SK_STATE_COUNT,
};

/*
 * String representation of a state.
 *
 * @param state, state for which the string representation is requested
 *
 * @return pointer to const string representation, NULL on error
 */
const char *
sk_state_str(enum sk_state state);

struct sk_lifecycle {
	/* The current state */
	enum sk_state state;

	/* Lock protecting the state transition */
	pthread_mutex_t lock;

	/* Listeners observing transitions */
	sk_listeners_t *listeners;

	/* Epochs at which state were transitioned to */
	time_t epochs[SK_STATE_COUNT];
} sk_cache_aligned;
typedef struct sk_lifecycle sk_lifecycle_t;

/*
 * Initialize a `sk_lifecycle_t`.
 *
 * @param lfc, lifecycle to initialize
 * @param error, error to store failure information
 *
 * @return true on success, false otherwise and set error
 *
 * @errors SK_ERROR_ENOMEN, if memory allocation failed
 *         SK_ERROR_EFAULT, if call to time(2) failed.
 */
bool
sk_lifecycle_init(sk_lifecycle_t *lfc, sk_error_t *error) sk_nonnull(1, 2);

/*
 * Free a lifecycle.
 *
 * @param lifecycle, lifecycle to free
 */
void
sk_lifecycle_destroy(sk_lifecycle_t *lfc) sk_nonnull(1);

/*
 * Get the current state of a `sk_lifecycle_t`.
 *
 * @param lfc, lifecycle to return the state from
 *
 * @return state
 */
enum sk_state
sk_lifecycle_get(const sk_lifecycle_t *lfc) sk_nonnull(1);

/*
 * Get the epoch at which the lifecycle transition to a given state.
 *
 * @param lfc, lifecycle to query
 * @param state, state to ask epoch for
 *
 * @return -1 on failure,
 *          0 if the state is not yet transitioned to
 *         or the transition time
 *
 * This method is used to view the history of when transitions happened.
 */
time_t
sk_lifecycle_get_epoch(const sk_lifecycle_t *lfc, enum sk_state state)
	sk_nonnull(1);

/*
 * Transition the state of a `sk_lifecycle_t`.
 *
 * @param lfc, lifecycle to affect
 * @param new_state, state to transition to
 * @param error, error to store failure information
 *
 * @return true on success, false otherwise and set error
 *
 * @errors SK_ERROR_EFAULT, if call to time(2) failed
 *         SK_ERROR_EINVAL, if state transition is invalid
 */
bool
sk_lifecycle_set(sk_lifecycle_t *lfc, enum sk_state new_state,
	sk_error_t *error) sk_nonnull(1, 3);

/*
 * Transition the state of a `sk_lifecycle_t` at a given epoch.
 *
 * @param lfc, lifecycle to affect
 * @param new_state, state to transition to
 * @param epoch, time at which the transition occurred
 * @param error, error to store failure information
 *
 * @return true on success, false otherwise and set error
 *
 * @errors SK_ERROR_EFAULT, if call to time(2) failed
 *         SK_ERROR_EINVAL, if state transition is invalid or if epoch is
 *                              invalid
 */
bool
sk_lifecycle_set_at_epoch(sk_lifecycle_t *lfc, enum sk_state new_state,
	time_t epoch, sk_error_t *error) sk_nonnull(1, 4);

/* Lifecycle listener callback information */
struct sk_lifecycle_listener_ctx {
	/* The new state transitioning to */
	enum sk_state state;
	/* The time the transition happened */
	time_t epoch;
};
typedef struct sk_lifecycle_listener_ctx sk_lifecycle_listener_ctx_t;

/*
 * Register a listener.
 *
 * @param lfc, lifecycle to register a listener to
 * @param name, name of the listener
 * @param callback, callback to invoke on transitions
 * @param ctx, context to pass to callback when invoked, ownership is
 *             transferred to the listener and will be freed with the listener
 * @param error, error to store failure information
 *
 * @return pointer to listener on success , NULL on failure and set error
 *
 * @errors SK_HEALTHCHECK_ENOMEN, if memory allocation failed
 */
sk_listener_t *
sk_lifecycle_register_listener(sk_lifecycle_t *lfc, const char *name,
	sk_listener_cb_t callback, void *ctx, sk_error_t *error)
	sk_nonnull(1, 2, 3, 5);

/*
 * Unregister a listener.
 *
 * @param lfc, lifecycle to unregister the listener from
 * @param listener, listener to unregister
 */
void
sk_lifecycle_unregister_listener(sk_lifecycle_t *lfc, sk_listener_t *listener)
	sk_nonnull(1, 2);
