# Pruebas del core en Linux

Compila el core de verdad (`chiaki-lib`, desde la raíz del repo) como lo usa el iPad, solo red local (`CHIAKI_ENABLE_HOLEPUNCH=OFF`), y corre contra él las pruebas unitarias de Chiaki. Dueño: A2.

```sh
./ios/tests/core/run.sh
```

El script corre tres configuraciones y luego `./tools/native_tests.sh`, las pruebas heredadas de VitaRPS5:

| Configuración | Para qué |
|---|---|
| `openssl` | La referencia: el backend con el que Chiaki escribió las pruebas |
| `mbedtls` | El backend del iPad |
| `armv7-mbedtls` | El backend del iPad en ARM de 32 bits (armv7-a, Cortex-A9 con NEON, como el A5), corriendo en `qemu-arm`. Atrapa errores de punteros y `size_t` de 32 bits que en x86_64 no se ven. Se salta si no están el compilador cruzado y qemu |

Cada configuración tiene dos pruebas:

- `chiaki_core_unit`: 100 pruebas con munit (cifrado, FEC, takion, registro, bitstream y más).
- `chiaki_core_link_check`: enlaza el archivo completo del core (`--whole-archive`), así que cada objeto tiene que resolver aunque ninguna prueba lo llame. Pasa solo si el core enlaza sin curl, json-c ni miniupnpc y si `CHIAKI_DISABLE_HOLEPUNCH` llega a quien usa los headers.

## Dependencias

En Ubuntu 24.04:

```sh
apt install cmake ninja-build protobuf-compiler python3-protobuf libssl-dev libmbedtls-dev libopus-dev
```

Para la corrida en armv7, además:

```sh
dpkg --add-architecture armhf
# Las fuentes de apt para armhf apuntan a http://ports.ubuntu.com/ubuntu-ports/
apt install gcc-arm-linux-gnueabihf qemu-user libssl-dev:armhf libmbedtls-dev:armhf libopus-dev:armhf
```

`linux-armv7.cmake` es un toolchain solo para estas pruebas. El de iOS es de A1 y vive en `ios/toolchain/`.

## De dónde vienen las pruebas

- Las pruebas son las de `test/` de [chiaki-ng](https://github.com/streetpea/chiaki-ng) en el commit `c8bea26` (9 de octubre de 2026), con la misma licencia AGPL. VitaRPS5 las había quitado.
- `munit/` es [µnit](https://github.com/nemequ/munit) en el commit `fbbdf14`, licencia MIT (`munit/COPYING`), copiado tal cual.

Cambios respecto a chiaki-ng, para que corran contra nuestro core:

- `regist.c`: sin holepunch, `chiaki_regist_request_payload_format` no lleva el argumento `holepunch_info`.
- `reorderqueue.c`: el callback de descarte de VitaRPS5 recibe además el motivo.
- `takion.c`: la ruta a `takionsendbuffer.c`.
- `main.c`: sin las suites que se quitaron.
- Se quitaron `feedback.c` (prueba `chiaki_feedback_history_buffer_record_state`, un arreglo de chiaki-ng que nuestro core no tiene: el orden en que se mandan soltar el clic y levantar el dedo del touchpad), `aia.c` (descarga de certificados intermedios, que nuestro core no tiene) y `ffmpegdecoder.c` (el iPad no usa FFmpeg).
- Nuevas: `ecdh.c` (el intercambio de llaves con llaves al azar en los dos lados, como en cada sesión) y `link_check.c`.

## Lo que encontraron estas pruebas

- **`chiaki_ecdh_set_local_key` con mbedTLS tiraba la llave recibida** y generaba otra al azar. Se arregló en `lib/src/ecdh.c`. La sesión no llama a esa función, así que no afectaba el streaming, pero sin el arreglo la prueba con valores conocidos (`gkcrypt/ecdh`) no podía correr con mbedTLS.
- **`chiaki-lib` no enlaza `libm`** aunque `orientation.c` usa `sqrt()`. En iOS `libm` es parte de `libSystem`, así que solo `link_check` en Linux la agrega.
- **El tamaño de `ChiakiSession` depende de dos definiciones**: `CHIAKI_LIB_ENABLE_MBEDTLS` (cambia `ChiakiECDH`) y `CHIAKI_DISABLE_HOLEPUNCH`. Todo lo que incluya los headers del core, como `CKSession`, tiene que compilarse con las mismas que el core, o las estructuras no coinciden. En x86_64 la sesión mide 5072 bytes con OpenSSL y 6064 con mbedTLS.

## La app (A6)

Las pruebas `ui_*.c` son del C portable de la app (`ios/UI/`, dueño A6). Viven aquí para correr con el mismo core y munit en las tres configuraciones. Qué cubre cada una está en `ios/UI/README.md`.
