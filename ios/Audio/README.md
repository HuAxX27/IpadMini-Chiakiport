# Audio (A4)

Del Opus que entrega el core a la bocina del iPad.

| Archivo | Qué es |
|---|---|
| `CKAudioSink.h` | Contrato C3 con A2: el reproductor que usa la app |
| `ck_audio_player.h`, `ck_audio_player.c` | A4.3: el reproductor de C3: decodificador, búfer y salida. C portable |
| `ck_audio_buffer.h`, `ck_audio_buffer.c` | A4.3: el búfer anti-jitter entre el hilo de audio del core y el de la salida, sin candados. C portable |
| `ck_audio_output.h`, `ck_audio_output_ios.c` | A4.2: la salida. Pide PCM de 16 bits por un callback: RemoteIO en el hilo de tiempo real del audio (la del reproductor) o AudioQueue |
| `ck_audio_session.h`, `ck_audio_session.m` | A4.2: la sesión de audio del proceso (AVAudioSession): categoría Playback, interrupciones y cambios de salida |
| `ck_audio_tone.h`, `ck_audio_tone.c` | A4.2: el seno del tono de prueba, en C portable y probado en Linux |
| `ck_audio_decoder.h`, `ck_audio_decoder.c` | A4.1: de Opus a PCM de 16 bits con libopus, con ocultamiento de cuadros perdidos. C portable |
| `ck_ogg_opus.h`, `ck_ogg_opus.c` | A4.1: lector de Ogg Opus, para las grabaciones de `cksession -g`. C portable |
| `ckaudio/` | A4.2: la herramienta `ckaudio` para probar el audio en el iPad por SSH, sin la app |

## Quién configura la sesión de audio

El reproductor, no la app (acordado con A6). Usa la categoría Playback: suena aunque el interruptor de silencio esté puesto y calla a las otras apps. Cuando iOS interrumpe el audio (una alarma, Siri, otra app), la cola se detiene sola; al terminar la interrupción, la sesión se reactiva y la cola vuelve a arrancar. Si el servidor de audio de iOS se reinicia, la cola se tiene que crear de nuevo.

## La salida (A4.2)

- Toca PCM intercalado de 16 bits a la frecuencia del stream (48 kHz en la PS5); iOS lo convierte a lo que use el hardware. Se le pide a iOS un búfer de hardware de 10 ms.
- El callback de relleno no puede bloquear, tomar candados ni reservar memoria. Si no tiene audio, rellena con silencio. Lo que se reserva, se reserva al crear la salida.
- **RemoteIO**, la del reproductor: el callback corre en el hilo de tiempo real del sistema de audio, una vez por búfer de hardware, con los cuadros que iOS decida (unos 512 del hardware, hasta 4096 con la pantalla bloqueada).
- **AudioQueue**: una cola de búferes que se rellenan en el hilo de AudioQueue, que no es de tiempo real. En la prueba P2-5 sonó bien con el iPad quieto (3 búferes de 40 ms), pero al girar el iPad se cortó con 3 búferes de 10 ms y con 3 de 40 ms, mientras YouTube en Safari no se cortaba (2026-10-27). Por eso el reproductor usa RemoteIO; AudioQueue queda para comparar (`-o aq`).
- `ck_audio_output_take_stats()` dice cuántos búferes se rellenaron, el hueco más largo entre dos rellenos y cuántas veces un relleno llegó tarde: después de que toda la cola tuvo tiempo de sonar (AudioQueue) o más de dos búferes después del anterior (RemoteIO). Con AudioQueue, también los saltos de su línea de tiempo y cuánto audio hay en cola; con RemoteIO, cuántos cuadros pidió cada llamada.

## El reproductor (A4.3)

`ck_audio_player.c` cumple C3 (`CKAudioSink.h`): el hilo de audio del core le pasa cada cuadro Opus, lo decodifica y lo escribe en el búfer, y el hilo de la salida lo lee. Configura la sesión de audio él mismo, la reactiva tras una interrupción y crea la salida de nuevo si el servidor de audio de iOS se reinicia.

El búfer (`ck_audio_buffer.c`) es un anillo de un escritor y un lector sin candados. Espera 50 ms de audio antes de tocar, que cubren los cuadros que llegan tarde, y cuida que nada suene como un chasquido:

- Si se vacía (la red se detuvo), apaga en 5 ms lo último que tiene y toca silencio hasta juntar 50 ms; luego entra en 5 ms. Lo que llega de golpe tras la pausa se descarta hasta quedar en 50 ms.
- Un cuadro que lo pasaría de 120 ms se descarta, y el sonido de antes y el de después se funden en 5 ms. Lo normal es no llegar ahí: si el hilo de la salida se atrasa, el búfer sube y la salida baja otro tanto, y el sonido no se retrasa.
- El reloj de la PS5 y el del iPad nunca van igual. Una vez por segundo: si hasta el nivel más bajo pasó de 55 ms, se descarta un cuadro; si el promedio bajó de 45 ms, el reproductor inventa uno con el ocultamiento de Opus, pero no en los 4 cuadros que siguen a uno ocultado, porque ocultar justo después de ocultar suena a chasquido.
- Un cuadro perdido (`NULL`) se oculta con el PLC de Opus, y cuenta en `frames_concealed` de C3.

Lo que prueba `player_test.c`, con el reproductor tal cual sobre una salida y una sesión falsas, 30 s por escenario, con paquetes como los de la PS5 y con la captura real: sin chasquidos con 2 % de pérdida y 10 ms de jitter, ni con los relojes 0.1 % separados, ni con la salida 30 ms atrasada; el búfer entre 40 y 80 ms cada segundo; un solo apagado en una pausa de 300 ms. Con 5 y 10 % de pérdida, los únicos chasquidos son los que el PLC de Opus hace solo, sin el búfer.

## Decodificar Opus (A4.1)

La PS5 manda Opus solo CELT, banda completa, estéreo, en cuadros de 10 ms (480 muestras a 48 kHz) a 64 kbps constantes: 80 bytes por cuadro, todos con el primer byte `0xf4`. Así venían los 6004 paquetes de la captura del 2026-10-22.

`ck_audio_decoder` reserva todo en `ck_audio_decoder_init()`, con el tamaño de libopus para estéreo, así que ni la cabecera ni los cuadros reservan memoria. Cada cuadro que entra da un cuadro de sonido: uno perdido en la red (`NULL` con tamaño 0, como dice C3) y uno que libopus rechaza se ocultan con el PLC de Opus, y la cuenta de tiempo no se corre.

Lo que prueba `audio_tests.sh` con `check_decode.py`: `opus_wav` decodifica una grabación a WAV con este decodificador, y el WAV tiene que ser válido, durar lo mismo que lo que decodifica ffmpeg y dar las mismas muestras que ffmpeg con libopus. En x86_64 son idénticas, muestra por muestra, también en los cuadros ocultados; en armv7 bajo qemu, a más de 60 dB. Contra el decodificador propio de ffmpeg, que no comparte código con libopus, pide 20 dB fuera de las pérdidas, porque ese decodificador no oculta: vuelve desde el silencio.

Con la captura real de la PS5 (60 s, un cuadro perdido): idéntica a ffmpeg con libopus, y a 72 dB de su decodificador propio. Para repetirlo:

```
./ios/tests/audio_tests.sh captura.opus
```

## Probar en el iPad: `ckaudio`

El paquete `com.thehuax.ckaudio` instala `/usr/bin/ckaudio`. El CI lo arma en cada PR y lo publica en el pre-release `ipad-pr-N`; la laptop lo baja con `ios/toolchain/fetch-debs.ps1 -Pr N -Package ckaudio`.

```
ckaudio tono [-o rio|aq] [-m frente|fondo] [-s SEGUNDOS] [-f HZ] [-d DBFS] [-b MS] [-n BÚFERES] [-i MS]
ckaudio opus ARCHIVO.opus [-o rio|aq] [-m frente|fondo] [-s SEGUNDOS] [-j MS] [-p POR_CIENTO] [-b MS] [-n BÚFERES]
```

`tono` toca 440 Hz a -12 dBFS durante 120 s por la misma salida y la misma sesión de audio que usa el reproductor: RemoteIO, o AudioQueue con `-o aq` (`-b` y `-n` dan sus búferes). Cada 10 s imprime los búferes rellenados, el hueco máximo y los posibles cortes; al final, una línea `RESULTADO`. La prueba P2-5 son 2 minutos sin cortes, medidos y al oído, girando el iPad a los 30, 60 y 90 s.

Lanzado por SSH, iOS trata a `ckaudio` como un proceso de fondo: sus temporizadores y su audio llegan tarde. El 2026-10-27, con RemoteIO, iOS duplicó el búfer de hardware a los pocos segundos (de 512 a 1024 cuadros) y llamó hasta 16 ms tarde, y se oyó estática; la app, al frente, no recibe ese trato. Por eso `ckaudio` le pide a iOS los temporizadores de una app al frente (`-m frente`, por omisión) y lo dice en la primera línea; con ellos el búfer se quedó en 512 cuadros. El papel de app al frente, que SpringBoard le da a la app, iOS no deja que un proceso se lo ponga a sí mismo. `-m fondo` deja el trato de SSH, para comparar. Aun así, el temporizador con que `ckaudio opus` entrega los cuadros despertaba en ráfagas de unos 100 ms, así que espera en un temporizador crítico de `kevent`, que iOS casi no retrasa, y al final dice cuánto despertó tarde. Al final imprime otra vez la sesión de audio, por si iOS cambió el búfer de hardware.

`opus` toca una grabación de `cksession -g` por el reproductor, como si llegara de la PS5: un hilo de tiempo real entrega un cuadro cada 10 ms, tarde al azar hasta `-j` ms y con `-p` por ciento de pérdida, y oculta los huecos como el core. Cada 10 s imprime el nivel del búfer, los cuadros ocultados, las veces que se vació y los posibles cortes de la salida.

## Pruebas en Linux

```
./ios/tests/audio_tests.sh [captura.opus ...]
```

Necesita `libopus-dev`, ffmpeg y python3. Corre las pruebas con ASan y UBSan, `buffer_test` también con ThreadSanitizer, las mismas en armv7 bajo `qemu-arm` (necesita `libopus-dev:armhf`) y, con `THEOS` definido y el core ya compilado para el iPad (`ios/toolchain/ci-ipad.sh`), arma el paquete `ckaudio`. Las capturas que se le den pasan también por `player_test`.
