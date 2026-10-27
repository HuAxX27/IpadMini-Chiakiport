# Audio (A4)

Del Opus que entrega el core a la bocina del iPad.

| Archivo | Qué es |
|---|---|
| `CKAudioSink.h` | Contrato C3 con A2: el reproductor que usa la app |
| `ck_audio_output.h`, `ck_audio_output_aq.c` | A4.2: la salida con AudioQueue. Pide PCM de 16 bits por un callback, un búfer a la vez, en el hilo de AudioQueue |
| `ck_audio_session.h`, `ck_audio_session.m` | A4.2: la sesión de audio del proceso (AVAudioSession): categoría Playback, interrupciones y cambios de salida |
| `ck_audio_tone.h`, `ck_audio_tone.c` | A4.2: el seno del tono de prueba, en C portable y probado en Linux |
| `ck_audio_decoder.h`, `ck_audio_decoder.c` | A4.1: de Opus a PCM de 16 bits con libopus, con ocultamiento de cuadros perdidos. C portable |
| `ck_ogg_opus.h`, `ck_ogg_opus.c` | A4.1: lector de Ogg Opus, para las grabaciones de `cksession -g`. C portable |
| `ckaudio/` | A4.2: la herramienta `ckaudio` para probar el audio en el iPad por SSH, sin la app |

## Quién configura la sesión de audio

El reproductor, no la app (acordado con A6). Usa la categoría Playback: suena aunque el interruptor de silencio esté puesto y calla a las otras apps. Cuando iOS interrumpe el audio (una alarma, Siri, otra app), la cola se detiene sola; al terminar la interrupción, la sesión se reactiva y la cola vuelve a arrancar. Si el servidor de audio de iOS se reinicia, la cola se tiene que crear de nuevo.

## La salida con AudioQueue (A4.2)

- La cola toca PCM intercalado de 16 bits a la frecuencia del stream (48 kHz en la PS5); AudioQueue lo convierte a lo que use el hardware.
- Por defecto son 3 búferes de 10 ms (480 cuadros) y se le pide a iOS un búfer de hardware de 10 ms. Los búferes se reservan una sola vez, al crear la salida: al tocar no hay reservas.
- El callback de relleno corre en el hilo de AudioQueue y no puede bloquear, tomar candados ni reservar memoria. Si no tiene audio, rellena con silencio.
- `ck_audio_output_take_stats()` dice cuántos búferes se rellenaron, el hueco más largo entre dos rellenos, cuántas veces un relleno llegó después de que toda la cola tuvo tiempo de sonar (la cola se quedó sin audio: un corte), los saltos de la línea de tiempo de AudioQueue y cuánto audio hay en cola.

## Decodificar Opus (A4.1)

La PS5 manda Opus solo CELT, banda completa, estéreo, en cuadros de 10 ms (480 muestras a 48 kHz) a 64 kbps constantes: 80 bytes por cuadro, todos con el primer byte `0xf4`. Así venían los 6004 paquetes de la captura del 2026-10-22.

`ck_audio_decoder` reserva todo en `ck_audio_decoder_init()`, con el tamaño de libopus para estéreo, así que ni la cabecera ni los cuadros reservan memoria. Cada cuadro que entra da un cuadro de sonido: uno perdido en la red (`NULL` con tamaño 0, como dice C3) y uno que libopus rechaza se ocultan con el PLC de Opus, y la cuenta de tiempo no se corre.

Lo que prueba `audio_tests.sh` con `check_decode.py`: `opus_wav` decodifica una grabación a WAV con este decodificador, y el WAV tiene que ser válido, durar lo mismo que lo que decodifica ffmpeg y dar las mismas muestras que ffmpeg con libopus. En x86_64 son idénticas, muestra por muestra, también en los cuadros ocultados; en armv7 bajo qemu, a más de 60 dB. Contra el decodificador propio de ffmpeg, que no comparte código con libopus, pide 20 dB fuera de las pérdidas, porque ese decodificador no oculta: vuelve desde el silencio.

Con la captura real de la PS5 (60 s, un cuadro perdido): idéntica a ffmpeg con libopus, y a 72 dB de su decodificador propio. Para repetirlo:

```
./ios/tests/audio_tests.sh captura.opus
```

## Probar en el iPad: `ckaudio tono`

El paquete `com.thehuax.ckaudio` instala `/usr/bin/ckaudio`. El CI lo arma en cada PR (artefacto `ipad-debs`).

```
ckaudio tono [-s SEGUNDOS] [-f HZ] [-d DBFS] [-b MS] [-n BÚFERES] [-i MS]
```

Toca un tono de 440 Hz a -12 dBFS durante 120 s por la misma salida y la misma sesión de audio que usará el reproductor. Cada 10 s imprime los búferes rellenados, el hueco máximo, los posibles cortes y cuánto audio hubo en cola; al final, una línea `RESULTADO`. La prueba P2-5 son 2 minutos sin cortes, medidos y al oído.

Si marca cortes, la siguiente prueba es `ckaudio tono -b 40`: cuatro veces más audio en cola. Un proceso lanzado por SSH despierta con retraso de sus temporizadores (ver `docs/ipad/dualsense-directo.md`, T9); la cola de AudioQueue no depende de temporizadores, pero si ese retraso la alcanzara, en la app no pasaría.

## Pruebas en Linux

```
./ios/tests/audio_tests.sh [captura.opus ...]
```

Necesita `libopus-dev`, ffmpeg y python3. Corre las pruebas con ASan y UBSan, las mismas en armv7 bajo `qemu-arm` (necesita `libopus-dev:armhf`) y, con `THEOS` definido y el core ya compilado para el iPad (`ios/toolchain/ci-ipad.sh`), arma el paquete `ckaudio`.
