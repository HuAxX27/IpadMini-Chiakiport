# Plan de pruebas

Qué se prueba, dónde, con qué meta y cómo se reporta, desde el hola mundo (P1) hasta la versión 1.0 (P5). Es la actividad A7.1 (#46) y entra en vigor cuando TheHuAx lo aprueba. Las configuraciones, los juegos y qué corrida va en cada punto de control están en [`matriz.md`](matriz.md).

## Principios

- **Lo que se puede probar en Linux se prueba en Linux**, en CI, antes de llegar al iPad. TheHuAx solo prueba lo que necesita el iPad, la PS5 o el DualSense.
- **Una configuración base y variantes de un solo cambio.** Cada variante cambia un factor respecto a la base, así un mal resultado apunta a una causa.
- **Cada prueba en el iPad tiene pasos, duración y meta.** Las impresiones de TheHuAx cuentan, con una escala del 1 al 5 para poder compararlas entre versiones.
- **El tiempo de TheHuAx es el recurso escaso.** Cada punto de control dice cuánto tiempo le pide (ver [Tiempo de TheHuAx](#tiempo-de-thehuax)).

## Niveles

| Nivel | Quién | Dónde | Qué cubre |
|---|---|---|---|
| 1. Linux | Agentes y CI | Linux x86, y armv7 emulado | Lógica en C portable: core, video, audio, paquete del puente. Con las capturas reales de A2.8 |
| 2. Hardware sin iPad | TheHuAx | PC, ESP32 y DualSense | Firmware del puente contra un receptor en la PC (A5.2) |
| 3. iPad | TheHuAx | iPad, PS5 y la red de su casa | Puntos de control P1 a P5 y la prueba de humo de cada `.deb` |

### Nivel 1: pruebas en Linux

Cada agente escribe las pruebas de su módulo en `ios/tests/` (o en `test/` si tocan el core). A7 revisa que existan antes de que la actividad pase al iPad.

| Actividad | Prueba | Meta |
|---|---|---|
| A0.2 | `ios/tests/contracts_check.c` compila | Tamaño y offsets de `CKPadPacket` sin cambios |
| A2.1 | `./tools/native_tests.sh` con holepunch apagado | Todo pasa |
| A2.2 | Core compilado contra el SDK 9.3 con `-mios-version-min=9.0 -Werror=unguarded-availability` | Sin APIs de iOS 10 o posteriores |
| A3.1 | Annex B a AVCC y extracción de SPS y PPS: códigos de inicio de 3 y 4 bytes, varios NAL por cuadro, cabecera de perfil sola, y las capturas de A2.8 | Salida idéntica a la referencia |
| A3.3 | Parte portable de la recuperación: cuadros perdidos, cuadro clave y cambio de SPS | Nunca se queda esperando un cuadro que no llega |
| A4.1 | Opus de la captura de A2.8 a WAV | Duración correcta, sin silencios ni saturación que no estén en la captura |
| A4.3 | Búfer de jitter con 2 % de pérdida simulada | Búfer entre 40 y 80 ms (C3), sin huecos |
| A5.2, A5.3 | Paquete de C4: magic o versión inválidos, `seq` que da la vuelta de 65535 a 0, paquetes viejos o repetidos | Los inválidos y viejos se descartan |
| A5.4 | Mapeo y zonas muertas | Cada botón llega a su bit de `ChiakiControllerButton`; los sticks en reposo dan 0 |
| Todas | Sin reservas de memoria por cuadro (regla de `CLAUDE.md`): contador de `malloc` con `-Wl,--wrap=malloc` en las rutas de video, audio y entrada | Cero reservas después del arranque |
| Todas | Pruebas con AddressSanitizer y UndefinedBehaviorSanitizer | Sin errores ni fugas |

Las pruebas en x86 de 64 bits no ven los errores de 32 bits que prohíbe `CLAUDE.md` (punteros y `size_t` de 64 bits, accesos desalineados en estructuras empaquetadas). Propuesta para A1.5: correr también las pruebas de video, audio y entrada en armv7 con `qemu-arm` en CI. El contador de `malloc` lo prepara A7 en `ios/tests/` con A7.2.

### Nivel 2: el puente sin iPad (A5.2)

TheHuAx empareja el DualSense con el ESP32 y abre el receptor de prueba en la PC.

- Pasos: mover cada stick hasta el tope en las cuatro direcciones, apretar cada botón y cada gatillo, apagar el control y volver a encenderlo.
- Meta: todos los botones y gatillos cambian de valor, los sticks llegan a los extremos y vuelven a 0, el control se reconecta solo.
- Reportar: qué botón falló, paquetes por segundo que muestra el receptor y si hubo cortes.

### Nivel 3: en el iPad

Solo TheHuAx prueba en el iPad. Cada punto de control tiene su lista; las corridas con video (R1, R2...) están en `matriz.md`.

#### P1: el hola mundo abre en el iPad

| ID | Prueba | Meta |
|---|---|---|
| P1-1 | Instalar el `.deb` con el script de A1.6 y abrir la app | Abre en horizontal y se cierra sin colgarse |
| P1-2 | Reiniciar el iPad, reactivar el jailbreak y abrir la app | Sigue abriendo después de reactivar el jailbreak |

#### P2: la PS5 despierta desde el iPad

| ID | Prueba | Meta |
|---|---|---|
| P2-1 | `ckctl` por SSH busca consolas (A2.4) | La PS5 aparece en menos de 5 s |
| P2-2 | `ckctl` registra la PS5 con Account ID y PIN | Registra al primer intento |
| P2-3 | Con la PS5 en reposo, `ckctl` la despierta | Enciende en menos de 30 s, 3 de 3 veces |
| P2-4 | Reiniciar el iPad y repetir P2-3 sin registrar de nuevo (A2.5) | El registro sobrevive |
| P2-5 | Tono de prueba de A4.2 por 2 minutos | Sin cortes ni chasquidos |
| P2-6 | Latencia del ESP32 al iPad con la herramienta de A5.3 | Mediana menor a 10 ms |
| P2-7 | App con datos simulados (A6.1); cambiar ajustes y reiniciar la app (A6.4) | Navega en horizontal; los ajustes se conservan |

#### P3: primer cuadro

| ID | Prueba | Meta |
|---|---|---|
| P3-1 | Reproducir la captura de A2.8 en el iPad (A3.2) | Fluida y sin artefactos, igual que en la PC |
| P3-2 | La PS5 aparece en la lista (A6.2) y se registra sin terminal (A6.3) | Registro completo desde la app |
| P3-3 | Errores a propósito: PIN equivocado, PS5 apagada, Wi-Fi apagado | Mensaje que se entiende; la app no se cierra |
| P3-4 | Corrida R1 de la matriz | Primera imagen menos de 10 s después de tocar conectar |

#### P4: sesión jugable de 10 minutos

| ID | Prueba | Meta |
|---|---|---|
| P4-1 | Corridas de P4 en la matriz | Metas de la columna P4 en [Metas](#metas) |
| P4-2 | Todos los botones en un juego (A5.4): cruz, círculo, cuadrado, triángulo, L1, R1, L2 y R2 a medio apretar y a fondo, L3, R3, ambos sticks, cruceta, Options, Create, PS y clic del touchpad | Todos responden |
| P4-3 | Sincronía de audio y video (A4.4) en una escena con golpes o saltos | TheHuAx no nota desfase |
| P4-4 | Salir y volver a conectar 5 veces; botón de inicio durante la sesión y volver; bloquear y desbloquear la pantalla | Reconecta siempre y no se cierra |
| P4-5 | Solo controles táctiles (corrida R4): recorrer el menú de la PS5 (A5.5) | Se navega sin control físico |

#### P5: versión 1.0, 30 minutos estables

| ID | Prueba | Meta |
|---|---|---|
| P5-1 | Corrida R3 de 30 minutos, con batería y sin cargar | Metas de la columna P5; cero cierres |
| P5-2 | Variantes de resolución y fps (A7.5): corridas R6 a R10 | Decidir los ajustes por defecto con datos |
| P5-3 | Variantes de red: corridas R5, R11 a R13 | Saber dónde y en qué banda se puede jugar |
| P5-4 | Latencia con cámara lenta (A7.4): ver [abajo](#latencia-con-cámara-lenta) | Meta de latencia en [Metas](#metas) |
| P5-5 | Fallas a media sesión: apagar el Wi-Fi del iPad 5 s, mandar la PS5 a reposo, desconectar el ESP32 | Mensaje claro, sin cierre; el táctil sigue sirviendo sin el ESP32 |
| P5-6 | Presión de memoria: abrir 5 pestañas en Safari, después la app, 5 minutos de R2 | La app no se cierra |
| P5-7 | Vibración (A5.6, opcional) y conexión en un toque desde la lista (A6.6) | El control vibra; de la lista al juego en un toque |
| P5-8 | Aceptación (A7.6): 30 minutos de juego libre | TheHuAx responde que sí lo usaría para jugar |

#### Prueba de humo de cada `.deb`

Desde P3, cada `.deb` nuevo que cambia algo en el iPad pasa esto antes de pruebas más largas (5 minutos): instalar, abrir, conectar con la configuración base, 2 minutos de G2, salir, volver a conectar y salir. Si falla, la versión no avanza.

## Metas

Las métricas salen de `CKMetrics` (contrato C5), que `CKSession` llena una vez por segundo, y del resumen por SSH de A7.2. Los porcentajes se calculan sobre la corrida completa.

| Métrica | Fuente | P4 (mínimo) | P5 (meta de la 1.0) |
|---|---|---|---|
| Cuadros decodificados por segundo, a 30 fps | `fps_decoded` | Promedio de 28 o más | Promedio de 29.5 o más; a lo sumo un segundo por minuto bajo 25 |
| Lo mismo a 60 fps (V3) | `fps_decoded` | No aplica | Promedio de 57 o más |
| Tiempo de decodificación | `decode_ms_avg` | Menos de 25 ms | Menos de 16 ms (a 60 fps, menos de 12 ms) |
| Cuadros perdidos | `frames_lost` | Menos del 1 % | Menos del 0.2 % en la posición cerca |
| Audio ocultado | `audio_frames_concealed` | Menos del 2 % | Menos del 0.5 % |
| Búfer de audio | `audio_buffered_ms` | Entre 40 y 80 ms (C3) | Entre 40 y 80 ms |
| Memoria residente | `resident_kb` | Menos de 150 MB | Menos de 150 MB y no crece más de 5 MB entre el minuto 5 y el 30 |
| Bitrate recibido | `bitrate_kbps` | Se registra | Se registra |
| CPU | Resumen de A7.2 por SSH | Se registra | Se registra; la meta se fija en A7.2 con los primeros datos |
| Del ESP32 al iPad | Herramienta de A5.3 | Mediana menor a 10 ms | Mediana menor a 10 ms |
| Del botón a la pantalla | Cámara lenta (A7.4) | Se registra | Mediana menor a 120 ms |
| Cierres de la app | TheHuAx | Cero en 10 minutos | Cero en 30 minutos |
| Batería | TheHuAx, porcentaje al inicio y al final | Se registra | Se registra |
| Calor | TheHuAx: frío, tibio, caliente o incómodo | Se registra | No llega a incómodo en 30 minutos |
| Impresiones del 1 al 5: imagen, fluidez, control y audio | TheHuAx | 3 o más en todas | 4 o más en fluidez y control |

Si `decode_ms_avg` no se puede medir con la API que elija A3.2 (`AVSampleBufferDisplayLayer` no lo expone), esa fila se marca como no medible y manda `fps_decoded`.

### Latencia con cámara lenta

Hace falta un teléfono que grabe a 240 cuadros por segundo. En el Galaxy S26 Ultra de TheHuAx es el modo Cámara lenta, no la Súper cámara lenta, que solo graba un instante.

1. Configuración base, juego G2, en una parte donde saltar se vea al instante.
2. Grabar en cámara lenta el DualSense y la pantalla del iPad juntos en el cuadro.
3. Apretar cruz 10 veces, con un segundo de pausa entre cada una.
4. Mandar el video. A7 cuenta los cuadros entre que el botón baja y la pantalla cambia (a 240 fps, cada cuadro son 4.2 ms).

La referencia es la misma prueba con la app oficial de PS Remote Play en una PC, grabada con el mismo teléfono: lo que tarda ahí lo pone la PS5 y la red, y la diferencia con el iPad es lo que agrega el port.

## Cómo se reporta

### Ficha del equipo, una sola vez

Antes de P1, TheHuAx llena esto y A7 lo pasa a `matriz.md`. Sin datos de PSN, IPs ni direcciones MAC.

| Dato | Para qué |
|---|---|
| Modelo del iPad (A1432, A1454 o A1455, en Ajustes > General > Información) y versión de iOS | Confirmar el objetivo del port |
| Jailbreak usado y si está activo | P1-2 |
| Espacio libre | El `.deb` y las capturas |
| Router: marca y modelo, y si da 2.4 GHz y 5 GHz | Variante V9 |
| PS5: modelo (estándar, digital o slim) y si va por cable o Wi-Fi | Configuración base |
| Dónde está el router y dónde juega TheHuAx, con distancia y paredes de por medio | Posiciones cerca, juego y lejos |
| Placa del ESP32 que compró | A5.2 |
| Teléfono con cámara lenta a 240 fps | Latencia (A7.4) |
| Juegos que tiene para G3 y G4 | La matriz |

### Reporte de cada prueba

TheHuAx lo pega en el issue de la actividad o lo dice en el chat del proyecto, y A7 lo pasa al registro de `matriz.md`.

```
Prueba: P4-1, corrida R3   ·   .deb: (versión o número de PR)
Resultado: pasa / falla / bloqueada
Duración: 10 min   ·   Batería: 85 % → 78 %   ·   Calor: tibio
Métricas: (las que muestra la app o el resumen de A7.2)
Impresiones del 1 al 5: imagen _, fluidez _, control _, audio _
Qué viste: cortes, artefactos, desfase o cierres, con el minuto aproximado
```

Antes de A7.2 no hay resumen automático: TheHuAx reporta lo que muestre la app y sus impresiones, y si algo se ve mal, un video corto con el teléfono vale más que una descripción.

Cuando una prueba falla, A7 abre un issue con el reporte, la corrida y la versión, etiquetado con el agente dueño del módulo, y la prueba se repite con el arreglo.

## Tiempo de TheHuAx

| Punto | Tiempo aproximado |
|---|---|
| P1 | 15 minutos |
| P2 | 30 minutos |
| P3 | 25 minutos |
| P4 | 1 hora |
| P5 | 3 horas, repartidas en dos o tres días |
| Prueba de humo | 5 minutos por `.deb` |
