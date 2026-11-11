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

## Lecture 4 - Task 1: sistema de sensores con zbus

La aplicación parte del ejemplo `l4-demo2` y conecta un productor con dos
observadores mediante un único canal, `sensor_chan`.

Cada mensaje contiene temperatura en miligrados Celsius, timestamp en
milisegundos y número de secuencia. Se generan 18 muestras, una cada 100 ms.
El sensor usa plazos absolutos para que el tiempo de publicación no se sume
al periodo; el reloj tiene resolución de 1 ms.

- **Display:** `ZBUS_LISTENER_DEFINE` ejecuta el callback sincrónicamente en el
  hilo `sensor`. Consulta el mensaje con `zbus_chan_const_msg()` y registra
  la actualización. El callback es breve y no duerme.
- **Logger:** `ZBUS_MSG_SUBSCRIBER_DEFINE` recibe una copia de cada mensaje
  mediante `zbus_sub_wait_msg()`, en su propio hilo. Una pausa de 350 ms
  simula el registro lento sin retrasar al productor ni al display.

Un subscriber regular recibe una notificación del canal y después lee su valor
actual. Aquí usamos el subscriber de mensajes del ejemplo para conservar cada
muestra antigua mientras el logger está ocupado. El pool de 32 buffers es
suficiente para esta demostración finita de 18 muestras. Un flujo continuo con
un consumidor permanentemente más lento necesitaría una política para limitar
la acumulación.

`main()` espera que ambos hilos terminen, comprueba los contadores y muestra
el resultado. El logger también verifica el canal y el orden de las secuencias.

### Compilar y ejecutar

Con el entorno de Zephyr del workspace activado, desde este repositorio:

```sh
west build -d build -b native_sim/native/64 app -- -DZEPHYR_TOOLCHAIN_VARIANT=host
./build/zephyr/zephyr.exe -stop_at=8
```

### Resultado verificado en native_sim

```text
[SENSOR] publish seq=0 temp=24000 mC tick=100
[DISPLAY-LIS] thread=sensor seq=0 temp=24000 mC tick=100
[LOGGER-MSG] thread=logger seq=0 temp=24000 mC latency=0ms tick=100
[SENSOR] publish seq=1 temp=24350 mC tick=200
[DISPLAY-LIS] thread=sensor seq=1 temp=24350 mC tick=200
...
[LOGGER-MSG] thread=logger seq=1 temp=24350 mC latency=251ms tick=451
...
[SENSOR] done published=18
...
[LOGGER-MSG] thread=logger seq=17 temp=29950 mC latency=4267ms tick=6067
[LOGGER-MSG] done received=18
[SUMMARY] published=18 displayed=18 logged=18
[SUMMARY] verification passed
```

La verificación automática del log confirmó las 18 secuencias y temperaturas,
intervalos de publicación de exactamente 100 ms y actualizaciones del display
en el mismo milisegundo de cada publicación, dentro del hilo `sensor`.
El logger recibió todas las copias en orden, con intervalos de 351 ms por el
redondeo del timeout al tick siguiente. El sensor terminó a los 1800 ms y el
logger a los 6067 ms: el retraso del logger no frenó las publicaciones.

Tag de entrega: `l4-task1`.
