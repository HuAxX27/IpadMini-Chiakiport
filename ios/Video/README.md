# Video (A3)

Del H.264 que entrega el core a la pantalla del iPad.

| Archivo | Qué es |
|---|---|
| `CKVideoSink.h` | Contrato C2 con A2 |
| `ck_h264.h`, `ck_h264.c` | A3.1: de Annex B a AVCC, extracción de SPS y PPS y separación en cuadros, en C portable y probado en Linux |
| `ck_video_core.h`, `ck_video_core.c` | A3.2 a A3.5: la mitad en C del sumidero, su recuperación de errores y la medida del ritmo, probadas en Linux contra un decodificador falso |
| `CKVideoSink.m` | A3.2: el contrato C2 con VideoToolbox, sobre `ck_video_core.c` |
| `ck_video_sink_stats.h` | Todo lo que cuenta el sumidero, más allá de C2, para CK Video y A7, y las fallas que se pueden simular (A3.3) |
| `ckvideo/` | CK Video, el banco de pruebas del video en el iPad, con una captura o con la PS5 en vivo (no es la app) |

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
5. Si el decodificador espera un cuadro clave (ver [Recuperación](#recuperación-a33)) y este no lo es, devuelve `false` sin llamar a VideoToolbox. El decodificador se crea con el primer cuadro clave, no antes.
6. Si no, decodifica y devuelve si VideoToolbox pudo. Si VideoToolbox dice que la sesión ya no sirve (`kVTInvalidSessionErr`, `kVTVideoDecoderMalfunctionErr`), la suelta y espera un cuadro clave para crear otra.

Todo corre bajo un candado del sumidero: el hilo de video del core, y `ck_video_sink_reset()` y las estadísticas desde el hilo principal, nunca se cruzan.

## Recuperación (A3.3)

Un cuadro que el sumidero no decodifica vuelve al core como `false`, y el core no lo guarda como referencia. Los cuadros P que siguen apuntan a él, así que el core los retiene (no llegan al sumidero) y le pide un IDR a la consola enseguida (`video_receiver_maybe_request_idr()` en `lib/src/videoreceiver.c`: como mucho cada 100 ms, y lo vuelve a pedir si en 1 s no llega). En H.264 el core no sabe hacer que esos cuadros apunten a una imagen anterior: `chiaki_bitstream_slice_set_reference_frame()` solo lo hace en H.265. Así que cualquier cuadro perdido, por la red o por el decodificador, deja la imagen quieta hasta el IDR. En el iPad, con la PS5, fueron de 0,4 a 1,6 s: el IDR tarda, y a veces el primer pedido vence y el core pide otro. Cómo acortarlo se sigue en el issue #103.

El sumidero, aun así, no cuenta con que solo le lleguen cuadros sanos. Cuando el decodificador se queda sin imágenes (VideoToolbox lo invalidó, cambiaron el SPS o el PPS y hay que crearlo otra vez, o falló el propio cuadro clave), un cuadro P apuntaría a imágenes que no tiene y se vería con bloques verdes o grises. Por eso en esos casos el sumidero **espera un cuadro clave**:

- Rechaza (`false`, sin llamar a VideoToolbox) todo cuadro que no sea clave. Clave es un IDR o un cuadro hecho solo de slices I o SI, que `ck_h264_slice_type()` lee de la cabecera de cada slice. Mientras tanto la capa sigue mostrando la última imagen buena.
- Esos cuadros tampoco quedan de referencia, así que el core pide el IDR. La imagen se queda quieta hasta que llega y vuelve sola; nunca se congela para siempre ni muestra basura.
- Cuando llega el cuadro clave, crea el decodificador si hace falta, lo decodifica y deja de esperar.

Con la PS5 esa espera casi nunca rechaza nada, porque el core ya retiene los cuadros P hasta el IDR: en la prueba del iPad hubo 6 decodificadores rotos y ningún cuadro rechazado por esperar. Protege de lo demás: un core que mande los P aunque falte su referencia (`submit_on_missing_ref` en `ChiakiConnectInfo`), o un decodificador que se invalide entre dos cuadros P sin que el core lo sepa. Al empezar una sesión también espera un cuadro clave, pero la PS5 abre cada sesión con su IDR. Si se rechaza un cuadro P suelto (VideoToolbox no pudo con él), no espera: el decodificador conserva las imágenes anteriores y el core retiene lo que sigue hasta el IDR.

Las cifras de la espera están en `CKVideoCoreStats` (y en el panel de CK Video): `frames_waiting`, los cuadros rechazados por esperar; `keyframe_waits`, cuántas veces hubo que esperar; y `keyframe_wait_us_max`, la espera más larga, desde la falla hasta que se decodificó el cuadro clave.

Para probarla sin perder paquetes de verdad, `ck_video_sink_simulate()` (en `ck_video_sink_stats.h`) hace fallar el siguiente cuadro que llegaría a VideoToolbox, como un cuadro rechazado (`CK_VIDEO_SIMULATE_LOST_FRAME`) o como una sesión invalidada (`CK_VIDEO_SIMULATE_BROKEN_DECODER`), y el sumidero se recupera igual que de una falla real. CK Video tiene un botón para cada una.

## Ritmo y letterbox (A3.5)

**Letterbox.** La capa tiene `videoGravity = AVLayerVideoGravityResizeAspect`: escala la imagen sin deformarla hasta que toca un borde y deja negro el resto. A 540p, 960×540 entra en 1024×576, con 96 píxeles negros arriba y abajo de la pantalla de 1024×768. La PS5 codifica 960×544 y recorta a 960×540 en el SPS; VideoToolbox entrega ese recorte como el *clean rect* de cada imagen, y el sumidero informa el tamaño de la imagen y el de la parte a la vista (`ck_video_sink_take_all_stats()`) para revisarlo en el iPad. CK Video los muestra junto al tamaño en pantalla.

**Ritmo.** Cada imagen va a la capa en cuanto se decodifica, marcada para mostrarse de inmediato: no hay búfer que empareje los cuadros, que agregaría hasta un cuadro de latencia (33 ms a 30 cuadros/s). La pantalla refresca a 60 Hz, así que a 30 cuadros/s cada imagen debería quedarse dos refrescos; si un cuadro llega tarde, la anterior se queda más y se nota un tirón. El sumidero lo mide en `CKVideoCoreStats`:

- `frames_late`: cuadros decodificados más de `CK_VIDEO_LATE_US` (50 ms, un cuadro y medio) después del anterior. Se cuentan desde que `decode()` regresa, porque ahí la imagen ya va a la pantalla.
- `interval_us_max`: el tiempo más largo entre dos cuadros, o sea lo más que estuvo quieta la imagen.

La decisión de no emparejar se revisó en el iPad: en 5 min 20 s con Fortnite, 286 de 8311 cuadros (3,4 %) llegaron más de 50 ms después del anterior, de 8 a 17 cada 10 s, también en los tramos sin fallas, y TheHuAx no notó tirones. Se queda sin búfer. Si en la prueba de 10 minutos de P4 se notan, se agrega uno de un cuadro que los muestre al ritmo de la pantalla (`CADisplayLink`), a cambio de esa latencia.

**Cuadros por segundo en las métricas.** `CKSession` llena `CKMetrics.fps_decoded` con los cuadros por segundo que aceptó el callback de video, y el sumidero solo acepta los que decodificó, así que es el fps decodificado. `ck_video_sink_take_stats()` (C2) da lo mismo desde el sumidero, más el tiempo promedio de decodificación, para quien prefiera medirlo ahí.

### Para la pantalla de juego (A6.5)

- Una vista a pantalla completa con `+layerClass` = `AVSampleBufferDisplayLayer`, `videoGravity = AVLayerVideoGravityResizeAspect` y fondo negro (como `ckvideo/CKVVideoView.m`). Su capa va a `ck_video_sink_new()`, que la retiene.
- `chiaki_lib_init()` una vez, antes de la primera sesión.
- `[session setVideoSampleCallback:ck_video_sink_sample_cb user:sink]` antes de `-start:` (`ckvideo/CKVLiveSession.m` lo hace así).
- `ck_video_sink_reset()` en el hilo principal antes de cada sesión, y `ck_video_sink_free()` solo después de que `-[CKSession stop]` regrese.
- Compilar `CKVideoSink.m` (con ARC), `ck_video_core.c` y `ck_h264.c`, con los frameworks AVFoundation, CoreMedia, CoreVideo, VideoToolbox y QuartzCore, y `-DCHIAKI_LIB_ENABLE_MBEDTLS -DCHIAKI_DISABLE_HOLEPUNCH` por los headers del core.

## CK Video

`ckvideo/` es el paquete `com.thehuax.ckvideo`: una app para probar el video en el iPad sin esperar a la app de A6. Muestra, a través de `CKVideoSink`, el mismo sumidero que usará la app, una de dos cosas:

- **Reproducir la captura**: una captura de A2.8 a 30 cuadros por segundo, en un hilo propio como el del core, que vuelve a empezar al terminar. Busca la captura en `/var/mobile/Library/Chiakiport/captura.h264` y, si no está, en `/var/mobile/captura.h264`, donde la dejó la prueba de A2.8.
- **Conectar a la PS5** (A3.4): una sesión de verdad con `CKSession` (contrato C1), la clase que usará la app, a 540p y 30 cuadros por segundo, con su video yendo al sumidero. Usa la primera consola registrada con `ckctl regist` en `/var/mobile/Library/Chiakiport/consoles.plist` y la última IP que guardó `ckctl`. Si la PS5 todavía tiene la sesión anterior y contesta que está ocupada, vuelve a pedirla cada 2 s, hasta 10 veces, como `cksession`. `CKVLiveSession.m` es el ejemplo de cómo conectar el sumidero a una sesión.

Un panel encima del video dice cada segundo cuántos cuadros se decodificaron y mostraron, cuánto tardó decodificar (promedio y máximo), los fallidos y omitidos, la espera de cuadros clave, el ritmo (cuadros tarde y lo más que estuvo quieta la imagen), el tamaño de la imagen y en pantalla, y la memoria al empezar, ahora y como máximo. Con la PS5 suma lo que cuenta `CKSession` (cuadros por segundo, kbps y cuadros perdidos en la red) y cuánto tardó el primer cuadro desde que se pidió la sesión. Tocar el video oculta o muestra el panel.

- **Simular pérdida** y **Simular decodificador roto** hacen fallar el siguiente cuadro (A3.3). Con la PS5 en vivo es la prueba de verdad: el core retiene los cuadros siguientes y pide el IDR, y su log lo dice ("Requesting IDR", "recovery complete"). Con una captura no hay core ni consola, así que el reproductor hace de consola: si el sumidero rechaza 8 cuadros seguidos, vuelve al principio de la captura, a su IDR, y lo cuenta en "keyframes enviados". Una pérdida simulada con la captura tampoco tiene quien retenga lo que sigue, así que lo que haga VideoToolbox con los cuadros siguientes (error o bloques hasta el siguiente IDR) solo dice cómo reacciona a un cuadro faltante.
- Lo que dicen el sumidero, el core y la app queda en `/var/mobile/Library/Chiakiport/ckvideo.log` (uno por arranque), con una línea de cifras cada 10 s: `ssh ipad cat /var/mobile/Library/Chiakiport/ckvideo.log`.
- La pantalla no se apaga mientras muestra video. **Detener**, o salir de la app, cierra la sesión con la PS5 y deja en el log una línea de resumen: segundos, cuadros decodificados y por segundo, fallidos, tarde, lo más que estuvo quieta la imagen, esperas de keyframe y la memoria al empezar, al final y como máximo.

### Prueba en el iPad (A3.2 a A3.5)

En la laptop (lo hace una sesión de Remote Control, sin preguntar):

1. Instalar el `.deb` de CK Video que arma el CI: `ios\toolchain\deploy.ps1 -Ip ipad -Path com.thehuax.ckvideo_<versión>_iphoneos-arm.deb`.
2. Revisar que estén la captura de A2.8 y el registro de la PS5: `ssh ipad ls -l /var/mobile/captura.h264 /var/mobile/Library/Chiakiport/consoles.plist`.
3. Despertar la PS5 y confirmar que está encendida: `ssh ipad ckctl wakeup -w 60`.
4. Después de la prueba física, guardar el log: `ssh ipad cat /var/mobile/Library/Chiakiport/ckvideo.log`.

En el iPad (TheHuAx):

1. Abrir **CK Video** y tocar **Reproducir la captura**. Reportar si se ve la pantalla de la PS5 completa, sin bloques ni colores raros, con barras negras arriba y abajo, y si se mueve con fluidez.
2. Pasados unos 10 s, tocar **Simular decodificador roto**. Lo esperado: la imagen se queda quieta un instante (unos 0,3 s) y vuelve desde el principio de la grabación, sin bloques verdes ni grises.
3. Pasados otros 10 s, tocar **Simular pérdida** y contar qué se ve en los segundos siguientes (nada, bloques que se arrastran, o un salto al principio).
4. Tocar **Conectar a la PS5**. Lo esperado: en unos segundos aparece la pantalla de inicio de la PS5, en vivo (P3). Mover algo en la PS5 con su control para ver que la imagen sigue.
5. Con la PS5 en pantalla, tocar **Simular decodificador roto** y luego **Simular pérdida**. Lo esperado: con los dos, la imagen se queda quieta hasta que llega el IDR (de medio segundo a un segundo y medio) y vuelve donde está la PS5, sin bloques.
6. Dejarla transmitiendo 10 minutos, de preferencia con un juego en movimiento, y tocar **Detener** (A3.5). Lo esperado: 30 cuadros por segundo todo el tiempo, sin cortes ni tirones que se noten, barras negras arriba y abajo, y la memoria sin crecer.
7. Reportar qué se vio en cada paso y las cifras del panel antes de tocar Detener (una foto basta).

Resultado (25 de octubre de 2026, CK Video 0.4.0): la grabación se vio completa a 29,6 cuadros/s y el decodificador roto volvió en 0,3 s. La PS5 en vivo dio el primer cuadro (P3) a los 3,3 s cuando estaba libre y hasta 8,7 s cuando aún tenía la sesión anterior; después, 29 a 31 cuadros/s, 13 a 15 ms por cuadro (máximo 35,6), imagen de 960×540 en 1024×576 y la memoria plana en unos 7,3 MB. Las 10 fallas simuladas en vivo volvieron solas y sin bloques, cada una tras una pausa de 0,4 a 1,6 s hasta el IDR (ver Recuperación). Al tocar Detener la capa se queda con la última imagen y la PS5 avisa "Control desconectado", que es el control de Remote Play que se va.

## Pruebas

```
./ios/tests/ck_h264_tests.sh [captura.h264 ...]
```

Corre las pruebas unitarias de `ck_h264` y del sumidero (`ios/tests/ck_video_tests.c`) con ASan y UBSan, las mismas en armv7 bajo `qemu-arm`, revisa la muestra de x264 de `ios/tests/data/` y cada captura que se le pase (también en armv7), y compara el AVCC byte por byte con el que produce ffmpeg, el de la muestra y el de cada captura. Después manda la muestra, cada captura y un clip de 960×540 por el sumidero, cuadro por cuadro, a un decodificador falso que escribe lo que recibe, y ffmpeg debe decodificar eso a las mismas imágenes que el original, cuadro por cuadro.

Al final repite eso rompiendo el decodificador cada 10 cuadros (A3.3). Cada cuadro tiene que hacer lo que promete la recuperación: el roto se pierde, los siguientes esperan y el siguiente IDR trae la imagen de vuelta. Y ffmpeg tiene que decodificar lo que recibió el decodificador falso a las mismas imágenes que el original en los cuadros que se quedaron: ninguno se armó con una referencia que faltaba. En el clip de 960×540 (un IDR cada 15 cuadros) son 6 rupturas y 5 recuperaciones; en la captura de la PS5, que tiene un solo IDR, la imagen ya no vuelve sin pedirle uno a la consola, que es lo que hace el core.

Las pruebas del sumidero cubren la cabecera sola, un cuadro antes de la cabecera, un cambio de perfil, un cuadro que el decodificador rechaza, un decodificador que se rompe o que no se puede crear, un cuadro más grande que el búfer, las cifras, `reset`, búferes al azar y dos hilos a la vez. Las de A3.3 cubren la espera del cuadro clave con un decodificador nuevo, roto o con un SPS nuevo, un cuadro clave que falla, un cuadro P que falla sin esperar, un cuadro de slices I que no es IDR, la simulación y el tiempo de espera medido. La de A3.5 mide el ritmo con un reloj falso: cuadros a tiempo, uno tarde, uno justo en el límite, un cuadro rechazado entre dos, el tiempo de decodificación y el reinicio. `ck_h264_tests.c` prueba `ck_h264_slice_type()` con slices armados bit a bit, con y sin bytes de prevención de emulación, y cuenta los tipos de slice de cada captura. Las capturas de A2.8 se esperan en Annex B crudo, el mismo formato que abre `ffplay`: la cabecera del perfil y luego cada cuadro tal como lo entrega el core.

## La captura real de la PS5

La captura de un minuto que grabó `cksession -g` en el iPad (A2.8, 2026-10-22) pasa todas las pruebas, en x86_64 y en armv7, y da el mismo AVCC que ffmpeg. No está en el repo porque sus primeros segundos muestran datos de la cuenta de PSN; se le pasa al script como argumento. Lo que dice del video que manda la PS5 a 540p y 30 fps, y que el sumidero (A3.2) debe tener en cuenta:

| Dato | Valor | Qué implica |
|---|---|---|
| Perfil | H.264 Main, nivel 3.2, CABAC | Lo decodifica el A5 por hardware |
| Tamaño | 960×544 codificado, recortado a 960×540 en el SPS | VideoToolbox aplica el recorte |
| Orden | `pic_order_cnt_type` 2, `max_num_reorder_frames` 0 | Cada cuadro sale del decodificador en cuanto entra: no hay que esperar a reordenar |
| Referencias | `max_num_ref_frames` 9, `gaps_in_frame_num_allowed` 0 | El decodificador guarda hasta 9 cuadros, y un cuadro perdido deja un hueco en `frame_num` que el estándar no permite |
| Cuadros clave | Un solo IDR en 60 s (el primero, en 9 slices); el resto son P | Tras un error no llega otro IDR solo: la recuperación depende de que el core pida un IDR |
| Unidades | La cabecera (SPS de 28 bytes y PPS de 4) llega aparte; sin AUD ni SEI | Los cuadros traen solo slices |
| Tamaño de cuadro | Hasta 10,5 KB en la pantalla de ajustes; 1797 cuadros en 59,9 s | |
