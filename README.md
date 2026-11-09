# Zephyr Training Environment

Welcome to the Zephyr RTOS training! This repository includes a ready-to-use
development environment based on Zephyr **4.4.0**, which you can set up in one of
three ways:

---

## Manual Zephyr Setup

Follow the following guide:
- [Getting Started Guide](https://docs.zephyrproject.org/4.4.0/develop/getting_started/index.html#).

Make sure to select appropriate OS and to perform all steps till
[Build the Blinky Sample](https://docs.zephyrproject.org/4.4.0/develop/getting_started/index.html#build-the-blinky-sample).

## Lecture 3: de polling a workqueue

[Enunciado original](https://github.com/iomico-public/zephyr-intermediate/blob/l3-homework/app/src/main.c).

Se reemplazó el hilo de polling por un trabajo definido con `K_WORK_DEFINE`.
El simulador produce diez eventos, separados por unos 100 ms, y llama a
`k_work_submit()`. La workqueue del sistema ejecuta `sensor_handler()`, que
registra cada ejecución con `k_uptime_get_32()`. Ya no hay una bandera compartida
ni un hilo que despierte periódicamente para revisarla.

`main()` espera el fin del productor con `k_thread_join()` y una notificación
de finalización del handler antes de leer las estadísticas. El semáforo
`work_done` se usa exclusivamente para esa notificación; el sensor entrega
su trabajo mediante la workqueue.

### Task 1: medición del starter

Se ejecutó el starter antes de reemplazarlo. En `native_sim/native/64`,
con `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`, se obtuvo:

```text
[SUMMARY] events=10  total_wakeups=92  wasted=82
[SUMMARY] wasted wakeups = 89% of all wakeups
```

Son 9,2 despertares por evento, cerca de los diez esperados. Los intervalos
reales dependen de la resolución del reloj: la configuración inicial del
simulador, con ticks de 10 ms, dio 55 despertares y 45 innecesarios. La aplicación
fija ticks de 1 ms para esta comparación y para generar las ráfagas del bonus.
Esto mide los despertares del consumidor, no todos los del sistema ni el consumo
de energía.

### Tasks 2 y 3: ejecutar y verificar la workqueue

Desde el repositorio y con el entorno Zephyr del workspace activado:

```sh
west build -d build -b native_sim/native/64 app -- -DZEPHYR_TOOLCHAIN_VARIANT=host
./build/zephyr/zephyr.exe -stop_at=2
```

Resultado medido:

```text
[SENSOR] event 1 tick=101
[HANDLER] processed event 1 tick=101
[SENSOR] event 2 tick=202
[HANDLER] processed event 2 tick=202
...
[SUMMARY] events=10 handler_calls=10 polling_wakeups=0
[SUMMARY] verification passed
```

La verificación comprobó diez ejecuciones, una por evento, con intervalos de
101 ms y sin llamadas adicionales durante los dos segundos de simulación.

Un único `k_work` puede agrupar envíos que lleguen mientras ya está pendiente.
En este ejemplo el handler es breve y termina antes del siguiente evento.
Para conservar cada muestra de un productor más rápido habría que almacenar
los datos, por ejemplo en una `k_msgq`.

### Bonus: debounce

`app/debounce.conf` activa `CONFIG_HOMEWORK_DEBOUNCE`. En ese modo se usa
`K_WORK_DELAYABLE_DEFINE`: cada evento llama a `k_work_reschedule()` desde el
productor y reinicia el plazo de 30 ms. El handler se ejecuta una sola vez cuando
la ráfaga termina. Reprogramar desde el productor permite que cada nuevo evento
posponga la ejecución.

```sh
west build -d build/debounce -b native_sim/native/64 app -- -DZEPHYR_TOOLCHAIN_VARIANT=host -DEXTRA_CONF_FILE=debounce.conf
./build/debounce/zephyr/zephyr.exe -stop_at=2
```

La prueba comprobó diez ráfagas, cada una con cinco eventos en 20 ms.
Primera ráfaga medida:

```text
[SENSOR] burst=1 edge=1 reschedule tick=101 delay=30ms
[SENSOR] burst=1 edge=2 reschedule tick=106 delay=30ms
[SENSOR] burst=1 edge=3 reschedule tick=111 delay=30ms
[SENSOR] burst=1 edge=4 reschedule tick=116 delay=30ms
[SENSOR] burst=1 edge=5 reschedule tick=121 delay=30ms
[HANDLER] processed event 1 tick=152
...
[SUMMARY] events=50 handler_calls=10 polling_wakeups=0
[SUMMARY] verification passed
```

En las diez ráfagas hubo exactamente una ejecución, 31 ms después del último
evento. El milisegundo adicional procede del redondeo de los timeouts a ticks.
