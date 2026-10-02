#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_INF);

#define STACK_SIZE 1024

#define PRIO_LOW 7
#define PRIO_MED 5
#define PRIO_HIGH 3
#define PRIO_COOP -1

void t_low_fn(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1) {
    LOG_INF("T_LOW running");
    k_sleep(K_MSEC(300));
  }
}

void t_med_fn(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1) {
    LOG_INF("T_MED running");
    k_sleep(K_MSEC(200));
  }
}

void t_high_fn(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1) {
    LOG_INF("T_HIGH running");
    k_sleep(K_MSEC(100));
  }
}

void t_coop_fn(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);
  /* Aquí K_SECONDS() es válido porque k_sleep() recibe k_timeout_t. */
  k_sleep(K_SECONDS(5));
  while (1) {
    for (int iteration = 1; iteration <= 5; iteration++) {
      LOG_INF("T_COOP busy iteration %d", iteration);

      /*
       * Trabajo activo: el hilo conserva la CPU y no entra
       * en estado Waiting.
       */
      k_busy_wait(50000);
    }

    LOG_INF("T_COOP yielding");
    k_yield();
  }
}

K_THREAD_DEFINE(t_low, STACK_SIZE, t_low_fn, NULL, NULL, NULL, PRIO_LOW, 0, 0);

K_THREAD_DEFINE(t_med, STACK_SIZE, t_med_fn, NULL, NULL, NULL, PRIO_MED, 0, 0);

K_THREAD_DEFINE(t_high, STACK_SIZE, t_high_fn, NULL, NULL, NULL, PRIO_HIGH, 0,
                0);

K_THREAD_DEFINE(t_coop, STACK_SIZE, t_coop_fn, NULL, NULL, NULL, PRIO_COOP, 0,
                0);

int main(void) {
  LOG_INF("Thread scheduling demonstration started");
  return 0;
}
