# Video (A3)

Del H.264 que entrega el core a la pantalla del iPad.

| Archivo | Qué es |
|---|---|
| `CKVideoSink.h` | Contrato C2 con A2 |
| `ck_h264.h`, `ck_h264.c` | A3.1: de Annex B a AVCC y extracción de SPS y PPS, en C portable y probado en Linux |

## Cómo usa el sumidero `ck_h264` (A3.2)

En cada llamada a `ck_video_sink_sample_cb`:

1. `ck_h264_annexb_to_avcc(buf, buf_size, CK_H264_DROP_FOR_VIDEOTOOLBOX, out, capacidad, &info)`. `out` es un búfer del sumidero que se reserva una vez con `ck_h264_avcc_max_size()` y solo crece si llega un cuadro más grande, así no hay reservas por cuadro.
2. `ck_h264_param_store_update(&store, &info.param_sets)`. Si devuelve `CK_H264_PARAMS_CHANGED`, se rearma el `CMVideoFormatDescription` con `CMVideoFormatDescriptionCreateFromH264ParameterSets` (desde iOS 7), usando `store.sps`, `store.pps` y `CK_H264_AVCC_LENGTH_SIZE` como largo del prefijo.
3. Si `info.nal_written` es 0, era solo la cabecera del perfil: no hay nada que decodificar.
4. Si no, se decodifican los `info.avcc_size` bytes de `out`. Es cuadro clave si `info.nal_types` tiene `CK_H264_NAL_BIT(CK_H264_NAL_IDR)`.
5. Si llega un cuadro antes de tener SPS y PPS (`ck_h264_param_store_ready()` falso), se devuelve `false` y el core lo trata como perdido.

## Pruebas

```
./ios/tests/ck_h264_tests.sh [captura.h264 ...]
```

Corre las pruebas unitarias con ASan y UBSan, las mismas en armv7 bajo `qemu-arm`, revisa la muestra de x264 de `ios/tests/data/` y cada captura que se le pase, y compara el AVCC byte por byte con el que produce ffmpeg. Las capturas de A2.8 se esperan en Annex B crudo, el mismo formato que abre `ffplay`: la cabecera del perfil y luego cada cuadro tal como lo entrega el core.
