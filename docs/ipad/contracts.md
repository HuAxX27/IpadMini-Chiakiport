# Contratos entre agentes

Los cinco contratos fijan cómo se hablan los módulos, para que cada agente programe y pruebe contra simulaciones sin esperar a los demás. Los headers son la fuente de verdad; este archivo explica el porqué.

| Contrato | Entre | Header | Dueño |
|---|---|---|---|
| C1 Sesión | A2 con A3, A4, A5, A6 | `ios/Bridge/CKSession.h` | A2 |
| C2 Video | A2 con A3 | `ios/Video/CKVideoSink.h` | A3 |
| C3 Audio | A2 con A4 | `ios/Audio/CKAudioSink.h` | A4 |
| C4 Entrada | A5 y A6 con A2 | `ios/Input/CKInputSource.h`, `bridge/protocol/ck_pad_packet.h` | A5 |
| C5 Ajustes y métricas | A6 y A7 con todos | `ios/Bridge/CKSettings.h` | A0 |

Cambiar un contrato: PR propio, aprobado por A0, que avisa en el issue de cada agente afectado.

## C1 Sesión

`CKSession` envuelve `ChiakiSession`. La interfaz le entrega la IP de la consola, el registro guardado (`ChiakiRegisteredHost`) y los ajustes; el video, el audio y las fuentes de entrada se conectan antes de `-start:`. El delegado recibe todo en el hilo principal.

Descubrimiento y registro de consolas (lo que usan A6.2 y A6.3) los define A2 en `ios/Bridge/CKConsoles.h` durante A2.4, antes de que empiece A6.2.

## C2 Video

Se usa tal cual el callback del core, `ChiakiVideoSampleCallback`, para no copiar cuadros. Detalles que el sumidero debe respetar:

- El core manda H.264 en Annex B; VideoToolbox quiere AVCC (longitudes en lugar de códigos de inicio). La conversión vive en C portable (A3.1) para probarla en Linux.
- Tras un cambio de perfil, la primera llamada trae solo la cabecera del perfil (SPS y PPS). El sumidero debe rearmar su `CMVideoFormatDescription` con ella.
- Devolver `false` marca el cuadro como perdido: el core no lo usa de referencia y se recupera como con una pérdida de red.
- El callback corre en el hilo de video del core; no puede bloquear más de un cuadro.

## C3 Audio

El reproductor de A4 llena un `ChiakiAudioSink` del core. `frame_cb` con `buf == NULL` es un cuadro perdido que se oculta con el PLC de Opus. El búfer objetivo es de 40 a 80 ms.

## C4 Entrada

Toda fuente de control (el puente ESP32, los controles táctiles) es un `CKInputSource` con una función `poll`. `CKSession` las consulta cada 8 ms y las combina con `chiaki_controller_state_or()`, así el táctil y el DualSense funcionan a la vez.

Paquete UDP del puente, puerto 9400, 20 bytes en little-endian:

| Offset | Campo | Tipo | Nota |
|---|---|---|---|
| 0 | `magic` | 2 × uint8 | `'C'`, `'K'` |
| 2 | `version` | uint8 | 1 |
| 3 | `flags` | uint8 | bit 0: touchpad presionado |
| 4 | `seq` | uint16 | se descartan paquetes que no sean más nuevos |
| 6 | `buttons` | uint32 | mismos bits que `ChiakiControllerButton` |
| 10 | `l2`, `r2` | 2 × uint8 | gatillos |
| 12 | `lx`, `ly`, `rx`, `ry` | 4 × int16 | sticks, mismo rango que `ChiakiControllerState` |

## C5 Ajustes, métricas y registro guardado

- `CKSettings`: resolución (360p, 540p o 720p), fps (30 o 60) y bitrate. Por defecto 540p a 30 fps. No hay campo de códec: siempre H.264.
- `CKMetrics`: lo que A7 necesita medir, llenado una vez por segundo.
- Registro guardado (lo escribe A2.5, lo lee A6): un plist en el directorio de la app con un diccionario por consola, indexado por `server_mac` en hexadecimal, con los campos de `ChiakiRegisteredHost` (`target`, `server_nickname`, `server_mac`, `rp_regist_key`, `rp_key_type`, `rp_key`) más la última IP conocida. Nunca se sube al repo.
