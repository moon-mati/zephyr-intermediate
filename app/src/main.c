/* Lecture 3: event-driven workqueue, with optional burst debounce. */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define SENSOR_MS 100
#define EVENT_COUNT 10
#define BURST_SIZE 5
#define BURST_INTERVAL_MS 4
#define DEBOUNCE_MS 30

/* Each counter has one writer; main reads only after completion. */
static int total_events;
static int total_processed;

/* Completion notification for main, never used to poll the sensor. */
K_SEM_DEFINE(work_done, 0, 1);

static void sensor_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	total_processed++;
	LOG_INF("[HANDLER] processed event %d tick=%u",
		total_processed, k_uptime_get_32());

	if (total_processed == EVENT_COUNT) {
		k_sem_give(&work_done);
	}
}

#if defined(CONFIG_HOMEWORK_DEBOUNCE)
K_WORK_DELAYABLE_DEFINE(sensor_work, sensor_handler);
#else
K_WORK_DEFINE(sensor_work, sensor_handler);
#endif

static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (int i = 0; i < EVENT_COUNT; i++) {
		k_msleep(SENSOR_MS);

#if defined(CONFIG_HOMEWORK_DEBOUNCE)
		for (int edge = 0; edge < BURST_SIZE; edge++) {
			if (edge > 0) {
				k_msleep(BURST_INTERVAL_MS);
			}

			total_events++;
			LOG_INF("[SENSOR] burst=%d edge=%d reschedule tick=%u delay=%dms",
				i + 1, edge + 1, k_uptime_get_32(), DEBOUNCE_MS);

			/* Restart the quiet period on every edge, at the source. */
			int ret = k_work_reschedule(&sensor_work, K_MSEC(DEBOUNCE_MS));

			if (ret < 0) {
				LOG_ERR("reschedule failed: %d", ret);
				return;
			}
		}
#else
		total_events++;
		LOG_INF("[SENSOR] event %d tick=%u", total_events, k_uptime_get_32());

		int ret = k_work_submit(&sensor_work);

		if (ret < 0) {
			LOG_ERR("submit failed: %d", ret);
			return;
		}
#endif
	}

	LOG_INF("[SENSOR] all events produced");
}

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_sim_fn,
		NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	LOG_INF("=== L3 Homework: Polling to Workqueue ===");
	LOG_INF("Mode: %s", IS_ENABLED(CONFIG_HOMEWORK_DEBOUNCE) ? "debounce" : "workqueue");

	/* Wait on actual completion instead of estimating a sleep duration. */
	k_thread_join(sensor_thread, K_FOREVER);
	if (k_sem_take(&work_done, K_SECONDS(2)) != 0) {
		LOG_ERR("Timed out waiting for all work to complete");
		return 1;
	}

	int expected_events = IS_ENABLED(CONFIG_HOMEWORK_DEBOUNCE) ?
		EVENT_COUNT * BURST_SIZE : EVENT_COUNT;

	LOG_INF("[SUMMARY] events=%d handler_calls=%d polling_wakeups=0",
		total_events, total_processed);
	if (total_events != expected_events || total_processed != EVENT_COUNT) {
		LOG_ERR("Unexpected event or handler count");
		return 1;
	}

	LOG_INF("[SUMMARY] verification passed");
	return 0;
}
