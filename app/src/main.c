#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define WORKER_PRIORITY 5
#define WORKER_COUNT 2
#define INCREMENTS 10000U
#define ROUNDS 3U
#define EXPECTED_COUNT (WORKER_COUNT * INCREMENTS)

K_THREAD_STACK_ARRAY_DEFINE(worker_stacks, WORKER_COUNT, STACK_SIZE);
static struct k_thread workers[WORKER_COUNT];
K_MUTEX_DEFINE(counter_mutex);

/* volatile makes accesses visible; it does not make an increment atomic. */
static volatile uint32_t counter;

static void increment_counter(void *p1, void *p2, void *p3)
{
	const bool use_mutex = *(const bool *)p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (uint32_t i = 0; i < INCREMENTS; i++) {
		if (use_mutex) {
			k_mutex_lock(&counter_mutex, K_FOREVER);
		}

		uint32_t value = counter;

		/* Expose the read/modify/write race, even on a single CPU. */
		k_yield();
		counter = value + 1U;

		if (use_mutex) {
			k_mutex_unlock(&counter_mutex);
		}
	}
}

static uint32_t run_round(bool use_mutex)
{
	counter = 0;

	/* Create both workers before allowing either to run. */
	for (int i = 0; i < WORKER_COUNT; i++) {
		k_thread_create(&workers[i], worker_stacks[i],
				K_THREAD_STACK_SIZEOF(worker_stacks[i]),
				increment_counter, &use_mutex, NULL, NULL,
				WORKER_PRIORITY, 0, K_FOREVER);
		k_thread_name_set(&workers[i], i == 0 ? "counter_a" : "counter_b");
	}

	for (int i = 0; i < WORKER_COUNT; i++) {
		k_thread_start(&workers[i]);
	}

	/*
	 * Join before reading the result or reusing thread objects and stacks.
	 * This also keeps use_mutex alive until both workers have finished.
	 */
	for (int i = 0; i < WORKER_COUNT; i++) {
		k_thread_join(&workers[i], K_FOREVER);
	}

	return counter;
}

int main(void)
{
	LOG_INF("Two workers, priority %d, %u increments each",
		WORKER_PRIORITY, INCREMENTS);

	for (int phase = 0; phase < 2; phase++) {
		bool use_mutex = (phase == 1);

		for (unsigned int round = 1; round <= ROUNDS; round++) {
			uint32_t actual = run_round(use_mutex);

			LOG_INF("%s round %u: expected=%u actual=%u lost=%u",
				use_mutex ? "mutex" : "unprotected", round,
				EXPECTED_COUNT, actual, EXPECTED_COUNT - actual);

			if (use_mutex && actual != EXPECTED_COUNT) {
				LOG_ERR("Mutex verification failed");
				return 1;
			}
		}
	}

	LOG_INF("Race condition demonstration complete");
	return 0;
}
