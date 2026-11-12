/* Lecture 5: queue pressure and a task watchdog built on the L4 zbus pipeline. */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

#define STACK_SIZE 2048
#define SENSOR_COUNT 40
#define SENSOR_PERIOD_MS 100
#define QUEUE_CAPACITY 8
#define WARNING_PERCENT 75
#define CONSUMER_WORK_MS 50
#define STALL_AFTER 3
#define STALL_MS 2500
#define WATCHDOG_MS 1500
#define HEALTH_PERIOD_MS 100

struct sensor_data {
	int32_t temperature_mc;
	uint32_t timestamp_ms;
	uint8_t seq;
};

K_MSGQ_DEFINE(sensor_queue, sizeof(struct sensor_data), QUEUE_CAPACITY, 4);
K_SEM_DEFINE(health_stop, 0, 1);

/* Single-writer statistics, read by main only after joining all threads. */
static unsigned int published_count;
static unsigned int displayed_count;
static unsigned int enqueued_count;
static unsigned int dropped_count;
static unsigned int processed_count;
static unsigned int peak_queue;
static unsigned int health_warnings;
static bool fault_injected;

/* These flags and callback statistics are shared across execution contexts. */
static atomic_t producer_done;
static atomic_t pipeline_failed;
static atomic_t watchdog_fires;
static atomic_t watchdog_queue;
static atomic_t watchdog_tick;

static void display_listener_cb(const struct zbus_channel *chan);
static void queue_listener_cb(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(display_lis, display_listener_cb);
ZBUS_LISTENER_DEFINE(queue_lis, queue_listener_cb);

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data,
		 NULL, NULL, ZBUS_OBSERVERS(display_lis, queue_lis),
		 ZBUS_MSG_INIT(.temperature_mc = 0, .timestamp_ms = 0, .seq = 0));

static void display_listener_cb(const struct zbus_channel *chan)
{
	const struct sensor_data *msg = zbus_chan_const_msg(chan);

	displayed_count++;
	LOG_INF("[DISPLAY] seq=%u temp=%d mC tick=%u",
		msg->seq, msg->temperature_mc, k_uptime_get_32());
}

static void queue_listener_cb(const struct zbus_channel *chan)
{
	const struct sensor_data *msg = zbus_chan_const_msg(chan);

	/* Listeners run in the producer: never block on a full queue. */
	if (k_msgq_put(&sensor_queue, msg, K_NO_WAIT) != 0) {
		dropped_count++;
		LOG_WRN("[QUEUE] full used=%u/%u dropped_seq=%u tick=%u",
			k_msgq_num_used_get(&sensor_queue), QUEUE_CAPACITY,
			msg->seq, k_uptime_get_32());
		return;
	}

	enqueued_count++;
	unsigned int used = k_msgq_num_used_get(&sensor_queue);

	if (used > peak_queue) {
		peak_queue = used;
	}
}

/* A kernel timer invokes this callback in interrupt context. */
static void consumer_watchdog_cb(int channel_id, void *user_data)
{
	ARG_UNUSED(user_data);

	uint32_t used = k_msgq_num_used_get(&sensor_queue);
	uint32_t now = k_uptime_get_32();

	atomic_set(&watchdog_queue, used);
	atomic_set(&watchdog_tick, now);
	atomic_inc(&watchdog_fires);
	LOG_ERR("[WATCHDOG] callback channel=%d queue=%u/%u isr=%d tick=%u",
		channel_id, used, QUEUE_CAPACITY, k_is_in_isr(), now);
	/* Observe the failure; do not sleep, feed, or reboot in this callback. */
}

static void producer_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	k_thread_name_set(k_current_get(), "producer");

	int64_t next_sample_ms = k_uptime_get();

	for (int i = 0; i < SENSOR_COUNT; i++) {
		next_sample_ms += SENSOR_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_sample_ms));

		struct sensor_data data = {
			.temperature_mc = 24000 + (i * 350),
			.timestamp_ms = k_uptime_get_32(),
			.seq = (uint8_t)i,
		};

		LOG_INF("[PRODUCER] publish seq=%u tick=%u", data.seq, data.timestamp_ms);
		int ret = zbus_chan_pub(&sensor_chan, &data, K_MSEC(20));

		if (ret != 0) {
			LOG_ERR("[PRODUCER] publish failed ret=%d", ret);
			atomic_set(&pipeline_failed, 1);
			break;
		}
		published_count++;
	}

	atomic_set(&producer_done, 1);
	LOG_INF("[PRODUCER] done published=%u queued=%u dropped=%u",
		published_count, enqueued_count, dropped_count);
}

static void consumer_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	k_thread_name_set(k_current_get(), "consumer");

	int channel = task_wdt_add(WATCHDOG_MS, consumer_watchdog_cb, NULL);

	if (channel < 0) {
		LOG_ERR("[CONSUMER] watchdog registration failed ret=%d", channel);
		atomic_set(&pipeline_failed, 1);
		return;
	}

	uint8_t previous_seq = 0;

	while (!atomic_get(&producer_done) || k_msgq_num_used_get(&sensor_queue) != 0) {
		struct sensor_data msg;
		int ret = k_msgq_get(&sensor_queue, &msg, K_MSEC(SENSOR_PERIOD_MS));

		if (ret == -EAGAIN) {
			continue;
		}
		if (ret != 0) {
			LOG_ERR("[CONSUMER] receive failed ret=%d", ret);
			atomic_set(&pipeline_failed, 1);
			break;
		}
		if (processed_count > 0 && msg.seq <= previous_seq) {
			LOG_ERR("[CONSUMER] samples out of order");
			atomic_set(&pipeline_failed, 1);
		}
		previous_seq = msg.seq;

		k_msleep(CONSUMER_WORK_MS);
		processed_count++;

		/* Only this thread feeds its watchdog, after completing actual work. */
		if (task_wdt_feed(channel) != 0) {
			atomic_set(&pipeline_failed, 1);
		}
		LOG_INF("[CONSUMER] processed seq=%u latency=%ums tick=%u",
			msg.seq, k_uptime_get_32() - msg.timestamp_ms, k_uptime_get_32());

		if (IS_ENABLED(CONFIG_HOMEWORK_STUCK_CONSUMER) &&
		    processed_count == STALL_AFTER) {
			fault_injected = true;
			LOG_WRN("[CONSUMER] stuck: sleep=%dms watchdog=%dms tick=%u",
				STALL_MS, WATCHDOG_MS, k_uptime_get_32());
			k_sleep(K_MSEC(STALL_MS));
			LOG_INF("[CONSUMER] resumed tick=%u", k_uptime_get_32());
		}
	}

	task_wdt_delete(channel);
	LOG_INF("[CONSUMER] done processed=%u", processed_count);
}

static void health_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	k_thread_name_set(k_current_get(), "health");

	bool warned = false;

	while (k_sem_take(&health_stop, K_MSEC(HEALTH_PERIOD_MS)) != 0) {
		unsigned int used = k_msgq_num_used_get(&sensor_queue);
		bool high = used * 100U >= QUEUE_CAPACITY * WARNING_PERCENT;

		LOG_INF("[HEALTH] queue=%u/%u fill=%u%% tick=%u",
			used, QUEUE_CAPACITY, used * 100U / QUEUE_CAPACITY,
			k_uptime_get_32());
		if (high && !warned) {
			health_warnings++;
			LOG_WRN("[HEALTH] WARNING queue >= %d%%: %u/%u tick=%u",
				WARNING_PERCENT, used, QUEUE_CAPACITY, k_uptime_get_32());
		}
		/* Warn on each transition into the high-water region. */
		warned = high;
	}
}

/* Delay startup until main initializes the task watchdog. */
K_THREAD_DEFINE(producer_thread, STACK_SIZE, producer_fn,
		NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(consumer_thread, STACK_SIZE, consumer_fn,
		NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(health_thread, STACK_SIZE, health_fn,
		NULL, NULL, NULL, 6, 0, SYS_FOREVER_MS);

int main(void)
{
	LOG_INF("=== L5 Task 1: reliability under pressure ===");
	LOG_INF("Mode=%s queue=%d producer=%dms consumer=%dms",
		IS_ENABLED(CONFIG_HOMEWORK_STUCK_CONSUMER) ? "stuck" : "healthy",
		QUEUE_CAPACITY, SENSOR_PERIOD_MS, CONSUMER_WORK_MS);

	int ret = task_wdt_init(NULL);

	if (ret != 0) {
		LOG_ERR("Task watchdog initialization failed ret=%d", ret);
		return 1;
	}

	k_thread_start(consumer_thread);
	k_thread_start(producer_thread);
	k_thread_start(health_thread);
	k_thread_join(producer_thread, K_FOREVER);
	k_thread_join(consumer_thread, K_FOREVER);
	k_sem_give(&health_stop);
	k_thread_join(health_thread, K_FOREVER);

	int fires = (int)atomic_get(&watchdog_fires);

	LOG_INF("[SUMMARY] published=%u displayed=%u queued=%u processed=%u dropped=%u",
		published_count, displayed_count, enqueued_count, processed_count, dropped_count);
	LOG_INF("[SUMMARY] peak=%u/%u warnings=%u watchdogs=%d watchdog_tick=%u",
		peak_queue, QUEUE_CAPACITY, health_warnings, fires,
		(uint32_t)atomic_get(&watchdog_tick));

	bool failed = atomic_get(&pipeline_failed) || published_count != SENSOR_COUNT ||
		displayed_count != SENSOR_COUNT || enqueued_count != processed_count ||
		enqueued_count + dropped_count != published_count ||
		k_msgq_num_used_get(&sensor_queue) != 0;

	if (IS_ENABLED(CONFIG_HOMEWORK_STUCK_CONSUMER)) {
		failed |= !fault_injected || peak_queue != QUEUE_CAPACITY ||
			health_warnings == 0 || fires != 1 || dropped_count == 0 ||
			atomic_get(&watchdog_queue) != QUEUE_CAPACITY;
	} else {
		failed |= fires != 0 || dropped_count != 0 || health_warnings != 0;
	}

	if (failed) {
		LOG_ERR("[SUMMARY] verification failed");
		return 1;
	}
	LOG_INF("[SUMMARY] verification passed");
	return 0;
}
