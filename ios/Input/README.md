# Entrada: el DualSense y los controles táctiles (A5)

Todo control llega a `CKSession` como un `CKInputSource` del contrato C4 (`CKInputSource.h`): una función `poll` que la sesión llama cada 8 ms y combina con `chiaki_controller_state_or()`. Lo que se puede probar sin iPad es C portable con pruebas en Linux (`ios/tests/input_tests.sh`). Dueño: A5.

| Archivo | Qué es |
|---|---|
| `ck_ds5.h` | Convierte los reportes del DualSense (USB y Bluetooth) en `ChiakiControllerState` (A5.7) |
| `ck_hci_filter.h`, `ck_ds5_shared.h` | El filtro de `BTServer` que deja emparejar el pad y el archivo donde comparte sus reportes (A5.7, A5.8) |
| `ck_ds5_source.h` | `CKDS5Source`, la fuente del DualSense conectado directo al iPad (A5.8) |
| `ck_pad_map.h` | El mapeo: tabla de botones, zonas muertas de sticks y gatillos, touchpad (A5.4) |
| `ck_touch_controls.h` | Los controles táctiles en pantalla: su acomodo y lo que hace cada dedo (A5.5) |
| `CKTouchControlsView.h` | La vista de UIKit que dibuja los controles táctiles y les pasa los toques (A5.5) |
| `ckdirect/` | El paquete `com.thehuax.ckdirect`: `ckbtfilter` y `ckhidprobe`. Su guía está en `docs/ipad/dualsense-directo.md` |
| `ckcontrol/` | El paquete `com.thehuax.ckcontrol`, la app de prueba CK Control (A5.4, A5.5) |

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

## Los controles táctiles (A5.5)

Para jugar o navegar sin control físico. `ck_touch_controls.h` es todo C portable: el acomodo, qué control toca cada dedo y el estado que sale; `CKTouchControlsView` solo lo dibuja y le pasa los toques de UIKit.

- **Acomodo.** Pensado para 1024 × 768 de lado, con dos manos: L1 y L2 arriba a la izquierda y R1 y R2 a la derecha; Crear, el panel táctil, Opciones y PS arriba al centro; la cruceta y el stick izquierdo a la izquierda; los cuatro botones y el stick derecho a la derecha; L3 y R3 en las esquinas de abajo. Cada control se pega a su orilla y todo se escala parejo con el lado menor, así que sirve de pie o en otro tamaño. En 1024 × 768 ningún control baja de 44 puntos. La franja de 40 puntos de abajo queda libre para el Centro de control, que en iOS 9 se abre deslizando desde abajo, y el centro queda libre para lo que la app quiera poner ahí.
- **Botones.** Se presionan mientras el dedo esté encima, y se puede deslizar de uno a otro (de ✕ a ○, por ejemplo). L2 y R2 se aprietan a fondo.
- **Cruceta.** Una sola zona: la dirección sale del ángulo del dedo respecto al centro, con las diagonales en sectores de 30°, y el centro mismo no presiona nada.
- **Sticks.** Flotantes: el centro queda donde cae el dedo, así que no hay salto al tocar, y el recorrido completo es el 28 % del lado menor. Se puede seguir moviendo el dedo fuera de la zona.
- **Panel táctil.** Hasta dos dedos, con sus posiciones en las unidades del touchpad del DualSense (1920 × 942). Tocarlo también aprieta el clic del panel: en una pantalla no hay cómo separar tocar de hundir, y los juegos usan sobre todo el clic.
- **Multitáctil.** Hasta 10 dedos a la vez. Un segundo dedo en el mismo stick, o un tercero en el panel, no hace nada.
- **Hilos.** Los toques llegan en el hilo principal y la sesión consulta la fuente en su hilo de entrada: cada estado se publica completo bajo un mutex y no reserva memoria.

Así lo usa la pantalla de juego (A6.5), con la vista encima de la imagen y del mismo tamaño:

```objc
CKTouchControlsView *touch = [[CKTouchControlsView alloc] initWithFrame:self.view.bounds];
touch.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
[self.view addSubview:touch];
[session addInputSource:touch.inputSource];
```

- La vista toma solo los toques que caen en un control; los demás pasan a las vistas de abajo, así que la app conserva sus propios botones y gestos en el centro.
- Oculta (`hidden`), fuera de la ventana o después de `-releaseAllControls`, no presiona nada. Para esconderla mientras se usa el DualSense: `ck_ds5_source_connected()`.
- Su fuente apunta a la vista: se quita de la sesión, o se detiene la sesión, antes de soltar la vista.
- Con la pantalla encendida durante la sesión: `[UIApplication sharedApplication].idleTimerDisabled = YES`.

## CK Control, la app de prueba

`ckcontrol/` arma `com.thehuax.ckcontrol`: una pantalla negra con los controles táctiles encima y, al centro, un texto que dice lo que da cada fuente (el DualSense ya con el mapeo por defecto, y los controles táctiles) y lo que le llegaría a la PS5 con las dos juntas. **Conectar con la PS5** abre una sesión con la primera consola registrada por `ckctl`, con las dos fuentes y sin imagen: lo que hacen los controles se ve en la tele de la PS5. Sirve para probar A5.4 y A5.5 antes de que exista la pantalla de juego.

Escribe lo que pasa en `/tmp/ckcontrol.log`, que se lee por SSH: cada cambio de lo que tiene presionado cada fuente, qué pruebas le faltan todavía (los 16 botones, cada gatillo a fondo, cada stick a fondo hacia cada lado y un dedo en el panel; 27 en total) y, durante la sesión, sus estados, los cuadros por segundo que llegan y las consultas por segundo a la entrada (125 esperadas).

## Probar en el iPad (A5.4 y A5.5)

**En la laptop, el agente:** instala `com.thehuax.ckdirect` 0.2.1 y `com.thehuax.ckcontrol` 0.1.0 (`deploy.ps1 -Ip ipad`), despierta la PS5 si hace falta (`ckctl wakeup`), y después de cada paso lee el registro:

```sh
ssh ipad "cat /tmp/ckcontrol.log"
```

**En el iPad y la PS5, TheHuAx:**

1. Con el DualSense conectado al iPad, abre **CK Control**. Aprieta cada botón del DualSense, incluidos PS y el clic del panel, los dos gatillos a fondo, cada stick a fondo hacia cada lado y un dedo en el touchpad. El texto de en medio los muestra mientras los aprietas.
2. Haz lo mismo con los controles de la pantalla, incluido un dedo en el panel táctil.
3. Con la tele en la PS5, toca **Conectar con la PS5**. Sin tocar el DualSense, navega el menú de la PS5 solo con la pantalla: muévete con la cruceta y el stick, entra con ✕, regresa con ○, abre el centro de control con PS y abre un juego.
4. Ya en el juego, aprieta cada botón del DualSense y mueve los sticks, y di si alguno no responde.

**Qué reportar:** si en el paso 3 pudiste navegar sin el DualSense, si en el paso 4 algún botón no respondió, y si algún control de la pantalla queda incómodo de alcanzar o se dispara solo. Lo demás sale del registro: en los pasos 1 y 2 cada fuente debe llegar a `listo, hizo las 27 pruebas`, y en la sesión las consultas a la entrada deben andar cerca de 125 por segundo.
