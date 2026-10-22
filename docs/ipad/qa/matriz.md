# Matriz de pruebas

Las configuraciones, los juegos y las corridas que TheHuAx hace en cada punto de control. Cómo se mide y qué se reporta está en [`plan.md`](plan.md).

## Ficha del equipo

La llena TheHuAx antes de P1 (ver [la ficha en el plan](plan.md#ficha-del-equipo-una-sola-vez)).

| Dato | Valor |
|---|---|
| iPad | iPad mini de primera generación, MD531LL/A (A1432: Wi-Fi, 16 GB) |
| iOS y jailbreak | iOS 9.3.5 (13G36); jailbreak activo con Carbon (semi-untethered: se reactiva tras cada reinicio), con Cydia y OpenSSH |
| Router y bandas | Falta confirmar si tiene 5 GHz |
| PS5 y su conexión | Por Wi-Fi; falta confirmar la banda |
| Placa del puente | Por comprar: ESP32 clásico o Pico W |
| Teléfono con cámara lenta | Samsung Galaxy S26 Ultra |

## Configuración base (B)

La más favorable, para que un problema apunte al software y no a la red.

| Factor | Valor |
|---|---|
| Resolución | 540p (960×540), con franjas negras en la pantalla de 1024×768 |
| fps | 30 |
| Bitrate | El del core para 540p: 6000 kbps |
| Códec | H.264, siempre |
| Red del iPad | 5 GHz |
| Red de la PS5 | Wi-Fi, en 5 GHz si el router lo tiene |
| Posición del iPad | Cerca: en el mismo cuarto que el router, a unos 2 m |
| Control | DualSense por el puente ESP32; táctil mientras el puente no exista |
| Energía | Batería sin cargar, brillo a la mitad |
| Otras apps | Ninguna abierta (cerrarlas desde el multitarea) |

El iPad va en 5 GHz porque el ESP32 y el Bluetooth del DualSense usan 2.4 GHz; así no compiten por el mismo aire. Si el router de TheHuAx solo tiene 2.4 GHz, la base pasa a 2.4 GHz y la variante V9 desaparece.

La PS5 va por Wi-Fi porque así está en casa de TheHuAx. Con el iPad también en Wi-Fi, el video cruza el aire dos veces, así que si algún día se puede pasar un cable a la PS5, V12 mide cuánto mejora.

## Variantes

Cada una cambia un solo factor de la base, salvo V4 y V5, que solo se corren cuando las variantes de un factor ya dieron su resultado.

| ID | Cambia | A | Para qué |
|---|---|---|---|
| V1 | Resolución | 360p, 2000 kbps | Respaldo si 540p no aguanta |
| V2 | Resolución | 720p, 10000 kbps | Ver si la nitidez extra compensa el costo |
| V3 | fps | 60 | Fluidez en juegos rápidos |
| V4 | Resolución y fps | 360p a 60 | Respaldo si V3 no aguanta |
| V5 | Resolución y fps | 720p a 60 | El techo del A5 |
| V6 | Bitrate | 540p a 4000 kbps | Menos pérdidas lejos del router, si hacen falta |
| V7 | Posición | Donde TheHuAx juega de verdad (la define antes de P4) | El uso real |
| V8 | Posición | Lo más lejos que TheHuAx jugaría (la define antes de P5) | El límite de la red |
| V9 | Banda del iPad | 2.4 GHz | Interferencia con el ESP32 y el Bluetooth |
| V10 | Control | Solo táctil | El respaldo sin puente |
| V11 | Control | DualSense directo al iPad (A5.7) | Solo si la investigación encuentra camino |
| V12 | Red de la PS5 | Cable Ethernet | Opcional: cuánto mejora si algún día se puede pasar un cable |
| V13 | Energía | Cargando | El calor extra del cargador |

## Juegos

| ID | Juego | Por qué |
|---|---|---|
| G1 | Menú de inicio de la PS5 | Texto chico y poco movimiento: nitidez de 540p y navegación |
| G2 | Astro Bot (o Astro's Playroom, que viene instalado en la PS5) | Acción en 3D con mucho movimiento y vibración |
| G3 | Fortnite | Disparos rápidos y mucho movimiento: el peor caso de movimiento y latencia. Es gratis |
| G4 | Opcional: un juego gratis con mucho texto, como Genshin Impact | Si el texto se lee a 540p. El menú de la PS5 (G1) ya prueba el texto chico, así que se puede saltar |

## Corridas por punto de control

✓ se corre siempre; "si ..." se corre solo en ese caso.

| Corrida | Config | Juego | Duración | P3 | P4 | P5 |
|---|---|---|---|---|---|---|
| R1 | B | G1 | 1 min | ✓ | ✓ | ✓ |
| R2 | B | G2 | 10 min | | ✓ | ✓ |
| R3 | B | G3 | 10 min en P4, 30 min en P5 | | ✓ | ✓ |
| R4 | V10 | G1 | 3 min | | ✓ | |
| R5 | V7 | G3 | 10 min | | ✓ | ✓ |
| R6 | V1 | G3 | 5 min | | si R3 falla | ✓ |
| R7 | V2 | G1 y G3 | 5 min cada uno | | | ✓ |
| R8 | V3 | G2 y G3 | 5 min cada uno | | | ✓ |
| R9 | V4 | G3 | 5 min | | | si R8 falla |
| R10 | V5 | G3 | 5 min | | | si R7 y R8 pasan |
| R11 | V8 | G3 | 5 min | | | ✓ |
| R12 | V9 | G3 | 5 min | | | ✓ |
| R13 | V6 | G3 | 5 min | | | si R5 o R11 pierden más del 0.2 % |
| R14 | B | G4 | 5 min | | | opcional |
| R15 | V13 | G3 | 10 min | | | opcional |
| R16 | V11 | G2 | 10 min | | | cuando exista |
| R17 | V12 | G3 | 10 min | | | opcional, si se puede pasar un cable |

Los ajustes por defecto de la 1.0 (A7.5) salen de R3 y R6 a R10: la resolución y los fps más altos que cumplen las metas de P5 en G3.

## Registro de resultados

A7 agrega una fila por reporte de TheHuAx.

| Fecha | Corrida o prueba | `.deb` | Resultado | fps | Perdidos | Memoria | Impresiones | Notas |
|---|---|---|---|---|---|---|---|---|
| | | | | | | | | |
