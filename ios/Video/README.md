# Video (A3)

Del H.264 que entrega el core a la pantalla del iPad.

| Archivo | Qué es |
|---|---|
| `CKVideoSink.h` | Contrato C2 con A2 |
| `ck_h264.h`, `ck_h264.c` | A3.1: de Annex B a AVCC, extracción de SPS y PPS y separación en cuadros, en C portable y probado en Linux |
| `ck_video_core.h`, `ck_video_core.c` | A3.2: la mitad en C del sumidero, probada en Linux contra un decodificador falso |
| `CKVideoSink.m` | A3.2: el contrato C2 con VideoToolbox, sobre `ck_video_core.c` |
| `ck_video_sink_stats.h` | Todo lo que cuenta el sumidero, más allá de C2, para CK Video y A7 |
| `ckvideo/` | CK Video, el banco de pruebas del video en el iPad (no es la app) |

## El sumidero (A3.2)

### VTDecompressionSession y AVSampleBufferDisplayLayer

El issue pedía elegir entre decodificar con `VTDecompressionSession` o darle el H.264 a `AVSampleBufferDisplayLayer` para que lo decodifique ella. Se usan las dos: VideoToolbox decodifica y la capa muestra la imagen ya decodificada.

- **Decodificar con `VTDecompressionSession`, de forma síncrona** (sin `kVTDecodeFrame_EnableAsynchronousDecompression`): el cuadro está decodificado antes de que regrese el callback del core. Así el callback puede devolver `false` cuando un cuadro no se decodifica, como pide C2, y el core lo trata como perdido en vez de usarlo de referencia. Con la capa sola, un error se ve mucho después en `layer.status`, cuando ya pasaron más cuadros. También da el tiempo de decodificación para `CKMetrics` y avisa cuando VideoToolbox invalida la sesión (al ir al fondo), para rearmarla.
- **Mostrar en `AVSampleBufferDisplayLayer`** (desde iOS 8, acepta cuadros sin comprimir): cada imagen va como `CMSampleBuffer` con `kCMSampleAttachmentKey_DisplayImmediately`, sin reloj ni cola, y aparece en el siguiente refresco de la pantalla. La imagen sale del decodificador en memoria IOSurface (NV12), que la capa muestra sin copiarla. Con `videoGravity = AVLayerVideoGravityResizeAspect` la capa hace sola el letterbox de 16:9 en 1024×768.
- No hace falta esperar a reordenar: la PS5 manda `pic_order_cnt_type` 2 y `max_num_reorder_frames` 0 (ver abajo), así que cada cuadro sale del decodificador en cuanto entra. Moonlight para iOS, la referencia del issue, también marca sus cuadros para mostrarse de inmediato.

### Qué hace en cada cuadro

`ck_video_core.c` hace todo lo que se puede probar en Linux y llama a un decodificador por cuatro funciones (`CKVideoDecoder`): crear o adaptar el decodificador para un SPS y PPS, decodificar un cuadro en AVCC, soltar el decodificador y un reloj. `CKVideoSink.m` las implementa con VideoToolbox; las pruebas, con un decodificador falso.

En cada llamada a `ck_video_sink_sample_cb`:

1. `ck_h264_annexb_to_avcc(buf, buf_size, CK_H264_DROP_FOR_VIDEOTOOLBOX, ...)` sobre un búfer del sumidero que se reserva una vez (256 KB de cuadro) y solo crece si llega un cuadro más grande, así no hay reservas por cuadro. El cuadro no se vuelve a copiar: el `CMBlockBuffer` de VideoToolbox apunta a ese búfer. Los envoltorios de CoreMedia (`CMBlockBuffer`, `CMSampleBuffer`) sí se crean por cuadro, porque la API no deja reusarlos.
2. `ck_h264_param_store_update()`. Si el SPS o el PPS cambiaron, el decodificador se rearma antes del siguiente cuadro: si VideoToolbox acepta el formato nuevo (`VTDecompressionSessionCanAcceptFormatDescription`) se queda la misma sesión, si no se crea otra.
3. Si el búfer era solo la cabecera del perfil, devuelve `true` sin decodificar.
4. Si llega un cuadro antes de tener SPS y PPS, o no se pudo crear el decodificador, devuelve `false` y el core lo trata como perdido.
5. Si no, decodifica y devuelve si VideoToolbox pudo. Si VideoToolbox dice que la sesión ya no sirve (`kVTInvalidSessionErr`, `kVTVideoDecoderMalfunctionErr`), la suelta y la crea otra vez en el siguiente cuadro.

Todo corre bajo un candado del sumidero: el hilo de video del core, y `ck_video_sink_reset()` y las estadísticas desde el hilo principal, nunca se cruzan.

### Para la pantalla de juego (A6.5)

- Una vista a pantalla completa con `+layerClass` = `AVSampleBufferDisplayLayer`, `videoGravity = AVLayerVideoGravityResizeAspect` y fondo negro (como `ckvideo/CKVVideoView.m`). Su capa va a `ck_video_sink_new()`, que la retiene.
- `[session setVideoSampleCallback:ck_video_sink_sample_cb user:sink]` antes de `-start:`.
- `ck_video_sink_reset()` en el hilo principal antes de cada sesión, y `ck_video_sink_free()` solo después de que `-[CKSession stop]` regrese.
- Compilar `CKVideoSink.m` (con ARC), `ck_video_core.c` y `ck_h264.c`, con los frameworks AVFoundation, CoreMedia, CoreVideo, VideoToolbox y QuartzCore, y `-DCHIAKI_LIB_ENABLE_MBEDTLS -DCHIAKI_DISABLE_HOLEPUNCH` por los headers del core.

## CK Video

`ckvideo/` es el paquete `com.thehuax.ckvideo`: una app para probar el video en el iPad sin esperar a la app de A6. Reproduce una captura de A2.8 a 30 cuadros por segundo a través de `CKVideoSink`, el mismo sumidero que usará la app, en un hilo propio como el del core, y vuelve a empezar al terminar. Un panel encima del video dice cada segundo cuántos cuadros se decodificaron y mostraron, cuánto tardó decodificar (promedio y máximo), los fallidos y omitidos, y la memoria. Tocar el video oculta o muestra el panel.

- Busca la captura en `/var/mobile/Library/Chiakiport/captura.h264` y, si no está, en `/var/mobile/captura.h264`, donde la dejó la prueba de A2.8.
- Lo que dicen el sumidero y la app queda en `/var/mobile/Library/Chiakiport/ckvideo.log` (uno por arranque), con una línea de cifras cada 10 s: `ssh ipad cat /var/mobile/Library/Chiakiport/ckvideo.log`.
- La pantalla no se apaga mientras reproduce. Al salir de la app se detiene.

### Prueba en el iPad (A3.2)

1. Instalar: `ios\toolchain\deploy.ps1 -Ip ipad -Path com.thehuax.ckvideo_0.1.0_iphoneos-arm.deb`, o `scp` y `dpkg -i` (lo hace una sesión de la laptop).
2. Abrir **CK Video** y tocar **Reproducir la captura**.
3. Qué reportar: si se ve la pantalla de ajustes de la PS5 completa, sin bloques ni colores raros, con barras negras arriba y abajo; si se mueve con fluidez; y las cifras del panel tras un minuto (decodificados por segundo, ms de decodificación, fallidos, rechazados por la capa, memoria). El log tiene lo mismo.

## Pruebas

```
./ios/tests/ck_h264_tests.sh [captura.h264 ...]
```

Corre las pruebas unitarias de `ck_h264` y del sumidero (`ios/tests/ck_video_tests.c`) con ASan y UBSan, las mismas en armv7 bajo `qemu-arm`, revisa la muestra de x264 de `ios/tests/data/` y cada captura que se le pase (también en armv7), y compara el AVCC byte por byte con el que produce ffmpeg, el de la muestra y el de cada captura. Después manda la muestra, cada captura y un clip de 960×540 por el sumidero, cuadro por cuadro, a un decodificador falso que escribe lo que recibe, y ffmpeg debe decodificar eso a las mismas imágenes que el original, cuadro por cuadro.

Las pruebas del sumidero cubren la cabecera sola, un cuadro antes de la cabecera, un cambio de perfil, un cuadro que el decodificador rechaza, un decodificador que se rompe o que no se puede crear, un cuadro más grande que el búfer, las cifras, `reset`, búferes al azar y dos hilos a la vez. Las capturas de A2.8 se esperan en Annex B crudo, el mismo formato que abre `ffplay`: la cabecera del perfil y luego cada cuadro tal como lo entrega el core.

## La captura real de la PS5

La captura de un minuto que grabó `cksession -g` en el iPad (A2.8, 2026-10-22) pasa todas las pruebas, en x86_64 y en armv7, y da el mismo AVCC que ffmpeg. No está en el repo porque sus primeros segundos muestran datos de la cuenta de PSN; se le pasa al script como argumento. Lo que dice del video que manda la PS5 a 540p y 30 fps, y que el sumidero (A3.2) debe tener en cuenta:

| Dato | Valor | Qué implica |
|---|---|---|
| Perfil | H.264 Main, nivel 3.2, CABAC | Lo decodifica el A5 por hardware |
| Tamaño | 960×544 codificado, recortado a 960×540 en el SPS | VideoToolbox aplica el recorte |
| Orden | `pic_order_cnt_type` 2, `max_num_reorder_frames` 0 | Cada cuadro sale del decodificador en cuanto entra: no hay que esperar a reordenar |
| Referencias | `max_num_ref_frames` 9, `gaps_in_frame_num_allowed` 0 | El decodificador guarda hasta 9 cuadros, y un cuadro perdido deja un hueco en `frame_num` que el estándar no permite |
| Cuadros clave | Un solo IDR en 60 s (el primero, en 9 slices); el resto son P | Tras un error no llega otro IDR solo: la recuperación depende de que el core cambie la referencia o pida un IDR |
| Unidades | La cabecera (SPS de 28 bytes y PPS de 4) llega aparte; sin AUD ni SEI | Los cuadros traen solo slices |
| Tamaño de cuadro | Hasta 10,5 KB en la pantalla de ajustes; 1797 cuadros en 59,9 s | |
