# Audio (A4)

Del Opus que entrega el core a la bocina del iPad.

| Archivo | Qué es |
|---|---|
| `CKAudioSink.h` | Contrato C3 con A2: el reproductor que usa la app |
| `ck_audio_output.h`, `ck_audio_output_aq.c` | A4.2: la salida con AudioQueue. Pide PCM de 16 bits por un callback, un búfer a la vez, en el hilo de AudioQueue |
| `ck_audio_session.h`, `ck_audio_session.m` | A4.2: la sesión de audio del proceso (AVAudioSession): categoría Playback, interrupciones y cambios de salida |
| `ck_audio_tone.h`, `ck_audio_tone.c` | A4.2: el seno del tono de prueba, en C portable y probado en Linux |
| `ckaudio/` | A4.2: la herramienta `ckaudio` para probar el audio en el iPad por SSH, sin la app |

## Quién configura la sesión de audio

El reproductor, no la app (acordado con A6). Usa la categoría Playback: suena aunque el interruptor de silencio esté puesto y calla a las otras apps. Cuando iOS interrumpe el audio (una alarma, Siri, otra app), la cola se detiene sola; al terminar la interrupción, la sesión se reactiva y la cola vuelve a arrancar. Si el servidor de audio de iOS se reinicia, la cola se tiene que crear de nuevo.

## La salida con AudioQueue (A4.2)

- La cola toca PCM intercalado de 16 bits a la frecuencia del stream (48 kHz en la PS5); AudioQueue lo convierte a lo que use el hardware.
- Por defecto son 3 búferes de 10 ms (480 cuadros) y se le pide a iOS un búfer de hardware de 10 ms. Los búferes se reservan una sola vez, al crear la salida: al tocar no hay reservas.
- El callback de relleno corre en el hilo de AudioQueue y no puede bloquear, tomar candados ni reservar memoria. Si no tiene audio, rellena con silencio.
- `ck_audio_output_take_stats()` dice cuántos búferes se rellenaron, el hueco más largo entre dos rellenos, cuántas veces un relleno llegó después de que toda la cola tuvo tiempo de sonar (la cola se quedó sin audio: un corte), los saltos de la línea de tiempo de AudioQueue y cuánto audio hay en cola.

## Probar en el iPad: `ckaudio tono`

El paquete `com.thehuax.ckaudio` instala `/usr/bin/ckaudio`. El CI lo arma en cada PR (artefacto `ipad-debs`).

```
ckaudio tono [-s SEGUNDOS] [-f HZ] [-d DBFS] [-b MS] [-n BÚFERES] [-i MS]
```

Toca un tono de 440 Hz a -12 dBFS durante 120 s por la misma salida y la misma sesión de audio que usará el reproductor. Cada 10 s imprime los búferes rellenados, el hueco máximo, los posibles cortes y cuánto audio hubo en cola; al final, una línea `RESULTADO`. La prueba P2-5 son 2 minutos sin cortes, medidos y al oído.

Si marca cortes, la siguiente prueba es `ckaudio tono -b 40`: cuatro veces más audio en cola. Un proceso lanzado por SSH despierta con retraso de sus temporizadores (ver `docs/ipad/dualsense-directo.md`, T9); la cola de AudioQueue no depende de temporizadores, pero si ese retraso la alcanzara, en la app no pasaría.

## Pruebas en Linux

```
./ios/tests/audio_tests.sh
```

Corre las pruebas con ASan y UBSan, las mismas en armv7 bajo `qemu-arm` y, con `THEOS` definido y el core ya compilado para el iPad (`ios/toolchain/ci-ipad.sh`), arma el paquete `ckaudio`.
