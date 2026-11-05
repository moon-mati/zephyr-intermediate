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

## L2 Task 1: condición de carrera y mutex

La aplicación crea dos hilos con la misma prioridad (`5`). Cada hilo intenta
incrementar un contador compartido 10 000 veces: el resultado esperado es 20 000.
Se ejecutan tres rondas sin protección y luego tres con mutex; el contador se
reinicia en cada ronda.

El incremento se descompone en lectura, suma y escritura. Un `k_yield()` entre
lectura y escritura permite que el otro hilo lea el mismo valor, incluso con una
sola CPU. Si ambos leen `0`, ambos escriben `1`: se pierde un incremento.
Por eso el resultado sin protección es menor que el esperado y depende del
orden de ejecución. `volatile` obliga a acceder al contador, pero no sincroniza
los hilos ni hace atómica la operación.

La corrección usa `K_MUTEX_DEFINE`, `k_mutex_lock(..., K_FOREVER)` y
`k_mutex_unlock()` para proteger **toda** la operación de lectura, suma y
escritura. Se conserva el mismo `k_yield()` para comprobar que el mutex evita
perder incrementos aunque haya cambios de contexto dentro de la sección crítica.
`k_thread_join()` espera a ambos hilos antes de consultar el resultado y de
reutilizar sus estructuras y pilas.

Desde este repositorio, con el entorno de Zephyr activado:

```sh
west build -p always -b native_sim/native/64 app
west build -t run
```

Ejemplo de resultados (se omiten las marcas de tiempo):

```text
unprotected round 1: expected=20000 actual=10000 lost=10000
mutex round 1: expected=20000 actual=20000 lost=0
```

El valor sin protección puede variar o repetirse entre rondas; lo que evidencia
la carrera es que no coincide con los 20 000 incrementos realizados. Con mutex,
las tres rondas deben dar exactamente 20 000. Al aparecer
`Race condition demonstration complete`, se puede detener el simulador con
`Ctrl+C`.

Tag de entrega: `l2-task1`.
