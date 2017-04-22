#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include <sk_lifecycle.h>

#include "test.h"

void
lifecycle_test_basic()
{
	sk_lifecycle_t lfc;

	assert_int_equal(sk_lifecycle_get(NULL), SK_STATE_NEW);

	assert_true(sk_lifecycle_init(&lfc));
	assert_int_equal(sk_lifecycle_get(&lfc), SK_STATE_NEW);

	/* Test transition matrix and epoch */
	for (size_t i = 1; i < SK_LIFECYCLE_COUNT; i++) {
		assert_true(sk_lifecycle_set_at_epoch(&lfc, i, i));
		assert_int_equal(sk_lifecycle_get(&lfc), i);
		assert_int_equal(sk_lifecycle_get_epoch(&lfc, i), i);

		for (size_t j = 0; j < i; j++)
			assert_false(sk_lifecycle_set(&lfc, j));
	}
}

struct lifecycle_worker_ctx {
	atomic_bool *thread_started;
	atomic_bool *workers_ready;
	enum sk_state state;
	sk_lifecycle_t *lfc;
};

void *
lifecycle_worker(void *opaque)
{
	struct lifecycle_worker_ctx *ctx = (struct lifecycle_worker_ctx *)opaque;
	const enum sk_state state = ctx->state;

	*ctx->thread_started = true;

	for (;;)
		if (*ctx->workers_ready)
			break;

	for (;;)
		if (sk_lifecycle_set_at_epoch(ctx->lfc, state, (int)state))
			break;

	return NULL;
}

/* Simulates 4 workers that concurrently try to advanced the state machine
 * to their unique assigned state in {STARTING,RUNNING,STOPPING,TERMINATED}. */
void
lifecycle_test_threaded()
{
	sk_lifecycle_t lfc;
	const uint8_t n_threads = 4;
	pthread_t workers[n_threads];
	struct lifecycle_worker_ctx contexes[n_threads];
	atomic_bool workers_ready = false, thread_started = false;

	assert_true(sk_lifecycle_init(&lfc));

	/* Start all threads, but stall them until ready */
	for (size_t i = 0; i < n_threads; i++) {
		thread_started = false;
		contexes[i].thread_started = &thread_started;
		contexes[i].workers_ready = &workers_ready;
		contexes[i].state = i + 1;
		contexes[i].lfc = &lfc;
		pthread_create(&workers[i], NULL, lifecycle_worker, &contexes[i]);
		while (!thread_started)
			;
	}

	/* Unlock all threads */
	workers_ready = true;

	/* Wait for completion */
	for (size_t i = 0; i < n_threads; i++) {
		pthread_join(workers[i], NULL);
	}

	for (size_t i = 0; i < n_threads; i++) {
		int state = i + 1;
		assert_int_equal(sk_lifecycle_get_epoch(&lfc, state), state);
	}

	assert_int_equal(sk_lifecycle_get(&lfc), SK_STATE_TERMINATED);
}

int
main()
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(lifecycle_test_basic),
	    cmocka_unit_test(lifecycle_test_threaded),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}