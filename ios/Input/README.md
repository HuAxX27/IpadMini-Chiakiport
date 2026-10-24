# Entrada: el DualSense y los controles táctiles (A5)

Todo control llega a `CKSession` como un `CKInputSource` del contrato C4 (`CKInputSource.h`): una función `poll` que la sesión llama cada 8 ms y combina con `chiaki_controller_state_or()`. Lo que se puede probar sin iPad es C portable con pruebas en Linux (`ios/tests/input_tests.sh`). Dueño: A5.

| Archivo | Qué es |
|---|---|
| `ck_ds5.h` | Convierte los reportes del DualSense (USB y Bluetooth) en `ChiakiControllerState` (A5.7) |
| `ck_hci_filter.h`, `ck_ds5_shared.h` | El filtro de `BTServer` que deja emparejar el pad y el archivo donde comparte sus reportes (A5.7, A5.8) |
| `ck_ds5_source.h` | `CKDS5Source`, la fuente del DualSense conectado directo al iPad (A5.8) |
| `ck_pad_map.h` | El mapeo: tabla de botones, zonas muertas de sticks y gatillos, touchpad (A5.4) |
| `ckdirect/` | El paquete `com.thehuax.ckdirect`: `ckbtfilter` y `ckhidprobe`. Su guía está en `docs/ipad/dualsense-directo.md` |

## El mapeo (A5.4)

`CKPadMap` se aplica al estado de un control físico antes de que llegue a la consola. `CKPadMapSource` envuelve cualquier fuente con un mapeo, y la sesión la ve con el mismo nombre que la fuente de adentro.

- **Botones.** Una entrada por cada botón de `ChiakiControllerButton`: los botones que presiona. Por defecto cada uno se presiona a sí mismo. `ck_pad_map_set_button()` cambia uno: por ejemplo, cruz por círculo, apagar un botón (0) o hacer que R1 apriete R2 a fondo (`CHIAKI_CONTROLLER_ANALOG_BUTTON_R2`).
- **Botón PS y clic del touchpad.** Son entradas normales de la tabla y por defecto llegan tal cual: el PS abre el centro de control de la PS5 y, sostenido, regresa al inicio. El botón de silencio del DualSense no tiene lugar en el protocolo de Remote Play, así que no se manda.
- **Sticks.** Zona muerta radial, por defecto 1600 de 32767 (cerca del 5 %). Dentro de ella el stick marca 0; fuera, el resto del recorrido se estira hasta el máximo, así que no hay salto en el borde, un empujón completo sigue siendo completo y una diagonal no cambia de dirección. En reposo, el DualSense marca unos cientos de unidades fuera del centro (T8): con la zona muerta, la consola lo ve quieto y la sesión no manda nada mientras no se mueve. Es pequeña a propósito: cada juego aplica la suya encima.
- **Gatillos.** Zona muerta de 0 a 255, por defecto 0: los del DualSense descansan en 0.
- **Touchpad.** `touchpad = false` deja de mandar los dedos; el clic sigue en la tabla.
- El movimiento (giroscopio y acelerómetro) no se toca.

El mapeo se copia al crear la fuente y se lee en el hilo de entrada de `CKSession`: se cambia solo con la sesión detenida.

Así lo usa la pantalla de juego (A6.5):

```objc
CKDS5Source _ds5;      // ivars: viven mientras la sesión consulte sus fuentes
CKPadMapSource _pad;

ck_ds5_source_init(&_ds5, CK_DS5_SHARED_PATH);
CKPadMap map;
ck_pad_map_set_default(&map);
[session addInputSource:ck_pad_map_source_input(&_pad, ck_ds5_source_input(&_ds5), &map)];
```

`ck_ds5_source_connected(&_ds5)` dice desde cualquier hilo si hay un DualSense conectado, por ejemplo para ocultar los controles táctiles mientras se usa.

## Probar el mapeo en el iPad

Con `com.thehuax.ckdirect` 0.2.1 instalado y el DualSense conectado, por SSH:

```sh
ckhidprobe -c -m
```

Imprime una línea cada vez que cambia el estado, ya con el mapeo por defecto. Con los sticks sueltos, `LX`, `LY`, `RX` y `RY` deben quedar en 0. Movidos a fondo hacia cada lado deben llegar a -32768 o 32767, y apenas movidos deben salir valores pequeños, sin saltos. Sin `-m` se ven los valores crudos, para comparar.
