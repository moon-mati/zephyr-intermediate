/* Lecture 4: one zbus channel, a fast listener, and a slow message subscriber. */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

#define STACK_SIZE 2048
#define SENSOR_COUNT 18
#define SENSOR_PERIOD_MS 100
#define LOGGER_DELAY_MS 350

struct sensor_data {
	int32_t temperature_mc;
	uint32_t timestamp_ms;
	uint8_t seq;
};

static unsigned int published_count;
static unsigned int displayed_count;
static unsigned int received_count;
static bool publisher_failed;
static bool logger_failed;

static void display_listener_cb(const struct zbus_channel *chan);

ZBUS_LISTENER_DEFINE(display_lis, display_listener_cb);

/* Copies preserve each sample while the logger is busy with older ones. */
ZBUS_MSG_SUBSCRIBER_DEFINE(logger_sub);

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data,
		 NULL, NULL,
		 ZBUS_OBSERVERS(display_lis, logger_sub),
		 ZBUS_MSG_INIT(.temperature_mc = 0, .timestamp_ms = 0, .seq = 0));

static void display_listener_cb(const struct zbus_channel *chan)
{
	const struct sensor_data *msg = zbus_chan_const_msg(chan);

	/* Runs in the publishing thread, with the channel locked: never sleep here. */
	displayed_count++;
	LOG_INF("[DISPLAY-LIS] thread=%s seq=%u temp=%d mC tick=%u",
		k_thread_name_get(k_current_get()), msg->seq,
		msg->temperature_mc, k_uptime_get_32());
}

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_thread_name_set(k_current_get(), "sensor");

	int64_t next_sample_ms = k_uptime_get();

	for (int i = 0; i < SENSOR_COUNT; i++) {
		/* Absolute deadlines avoid adding publish time to each period. */
		next_sample_ms += SENSOR_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_sample_ms));

		struct sensor_data data = {
			.temperature_mc = 24000 + (i * 350),
			.timestamp_ms = k_uptime_get_32(),
			.seq = (uint8_t)i,
		};

		LOG_INF("[SENSOR] publish seq=%u temp=%d mC tick=%u",
			data.seq, data.temperature_mc, data.timestamp_ms);

		int ret = zbus_chan_pub(&sensor_chan, &data, K_MSEC(100));

		if (ret != 0) {
			LOG_ERR("[SENSOR] publish failed ret=%d", ret);
			publisher_failed = true;
			break;
		}
		published_count++;
	}

	LOG_INF("[SENSOR] done published=%u", published_count);
}

static void logger_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_thread_name_set(k_current_get(), "logger");

	while (received_count < SENSOR_COUNT) {
		const struct zbus_channel *chan;
		struct sensor_data msg;
		int ret = zbus_sub_wait_msg(&logger_sub, &chan, &msg, K_SECONDS(2));

		if (ret != 0) {
			LOG_ERR("[LOGGER-MSG] receive failed ret=%d", ret);
			logger_failed = true;
			break;
		}
		if (chan != &sensor_chan || msg.seq != received_count) {
			LOG_ERR("[LOGGER-MSG] unexpected channel or sequence");
			logger_failed = true;
			break;
		}

		received_count++;
		LOG_INF("[LOGGER-MSG] thread=%s seq=%u temp=%d mC latency=%ums tick=%u",
			k_thread_name_get(k_current_get()), msg.seq,
			msg.temperature_mc, k_uptime_get_32() - msg.timestamp_ms,
			k_uptime_get_32());

		/* Slow work happens in the subscriber's own thread. */
		if (received_count < SENSOR_COUNT) {
			k_msleep(LOGGER_DELAY_MS);
		}
	}

	LOG_INF("[LOGGER-MSG] done received=%u", received_count);
}

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_thread_fn,
		NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_thread_fn,
		NULL, NULL, NULL, 6, 0, 0);

int main(void)
{
	LOG_INF("=== L4 Task 1: Zbus sensor system ===");
	LOG_INF("Sensor period=%dms; logger processing delay=%dms",
		SENSOR_PERIOD_MS, LOGGER_DELAY_MS);

	/* Each statistic has one writer; read only after both threads finish. */
	k_thread_join(sensor_thread, K_FOREVER);
	k_thread_join(logger_thread, K_FOREVER);

	LOG_INF("[SUMMARY] published=%u displayed=%u logged=%u",
		published_count, displayed_count, received_count);
	if (publisher_failed || logger_failed ||
	    published_count != SENSOR_COUNT || displayed_count != SENSOR_COUNT ||
	    received_count != SENSOR_COUNT) {
		LOG_ERR("[SUMMARY] verification failed");
		return 1;
	}

	LOG_INF("[SUMMARY] verification passed");
	return 0;
}
