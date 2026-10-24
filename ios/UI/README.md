# Chiakiport, la app (A6)

La app UIKit del iPad mini 1: busca la PS5 en la red, la registra, la despierta y abre la pantalla de juego. Es un proyecto de Theos que se instala en `/Applications/Chiakiport.app` con el paquete `com.thehuax.chiakiport`. Dueño: A6.

La lógica que se puede probar sin iPad está en C portable con pruebas en Linux (`ios/tests/core/ui_*.c`); el Objective-C solo dibuja y pasa los toques.

| Archivo | Qué es |
|---|---|
| `ck_console_rows.h`, `.c` | Las filas de la lista: junta lo que encontró la búsqueda (`CKConsoles.h`) con los registros guardados (`ck_registry.h`), decide el estado y la acción de cada consola y las ordena |
| `CKConsoleService.h`, `.m` | Lo que las pantallas le piden a las consolas: la lista, despertar, registrar y el registro para la sesión |
| `CKMockConsoleService.h`, `.m` | Consolas simuladas (A6.1), para probar cada pantalla sin consola |
| `CKConsoleListViewController`, `CKConsoleCell` | La lista de consolas, la primera pantalla |
| `CKRegistViewController` | El registro: Account ID, PIN y qué corregir si la consola dice que no |
| `CKStreamViewController` | La pantalla de juego; por ahora, un fondo negro con el gesto para salir |
| `CKAppDelegate`, `main.m`, `CKTheme.h` | Arranque y colores |
| `shared_*.c` | Compilan dentro de la app el código de `ios/Bridge/` que también usa `ckctl` (el `Makefile` dice por qué) |
| `Makefile`, `control`, `Resources/` | El paquete de Theos, el `Info.plist` y los íconos |

## Pantallas

- **Consolas.** Una fila por consola encontrada y una por consola registrada que no contestó. Primero las registradas, luego por nombre, así las filas no brincan mientras la lista se refresca. Cada fila dice si es PS5 o PS4, su estado en color (encendida, en reposo, no contesta), su IP y el juego abierto, y tiene un botón con lo que hace un toque:
  - **Jugar**, registrada y encendida: abre la pantalla de juego.
  - **Despertar**, registrada y en reposo, o que no contesta pero tiene una IP guardada: manda la señal y muestra "Despertando…" hasta que la consola contesta encendida, o avisa a los 45 s si no se encendió.
  - **Registrar**, encendida y sin registrar: abre el registro.
  - Sin botón: una consola sin registrar en reposo (hay que encenderla para registrarla) o una registrada sin IP conocida. Un toque explica qué hacer.
- **Registrar.** El Account ID de PSN (en base64, 12 caracteres que terminan en `=`, o en decimal) y el PIN de 8 cifras que muestra la PS5 en *Ajustes › Sistema › Remote Play › Vincular dispositivo*. Antes de mandar nada revisa que los dos se puedan leer. Si la consola dice que no, muestra qué corregir (`ck_regist_result_string()`), y con un PIN equivocado borra el campo para escribir otro. Al terminar regresa a la lista.
- **Juego.** Pantalla completa, sin barra de estado y sin que el iPad se apague solo. Un toque con tres dedos abre un menú para salir. El video, el audio, los controles y las métricas llegan con A6.5.

Toda la app va en horizontal, hacia cualquiera de los dos lados.

## Consolas simuladas (A6.1)

Por ahora la lista sale de `CKMockConsoleService`, cuatro consolas inventadas, una de cada tipo de fila:

| Consola | Estado | Botón |
|---|---|---|
| PS5 de la sala | registrada, encendida, con un juego abierto | Jugar |
| PS5 del cuarto | registrada, en reposo | Despertar: se enciende 5 s después |
| PS5 de la oficina | registrada, no contesta | Despertar: nunca se enciende, y a los 45 s la lista lo avisa |
| PS5-517 | encendida, sin registrar | Registrar: tarda 2 s y funciona con cualquier Account ID válido y cualquier PIN de 8 cifras, salvo `00000000`, que la simulación rechaza como un PIN equivocado |

Nada de esto se guarda: al cerrar la app vuelve a como empezó. Debajo de la lista lo dice, junto con la versión y el commit de la app.

## Compilar

El CI la empaqueta en cada PR con los demás proyectos de Theos (`ios/toolchain/ci-ipad.sh`) y la deja en el artefacto `ipad-debs` como `com.thehuax.chiakiport_<versión>_iphoneos-arm.deb`. A mano, después de `ci-ipad.sh`, o de `build-deps.sh` y `core-check.sh`:

```sh
make -C ios/UI package FINALPACKAGE=1
```

Como `ios/cli`, enlaza el core de `build-ios-core/` y mbedTLS y Opus de `build-ios-deps/prefix/`; `CK_CORE_BUILD` y `CK_IOS_PREFIX` cambian esas rutas.

## Probar en el iPad

La instalación y lo que se corre por SSH lo hace un agente desde la laptop (`ios/toolchain/deploy.ps1`). En el iPad:

1. Abre **Chiakiport** desde la pantalla de inicio (ícono azul con un triángulo de "play").
2. Gira el iPad: la app se queda en horizontal y se voltea al girarlo 180°, pero no se pone en vertical.
3. Debe salir la lista de la tabla de arriba, con "Consolas simuladas" y la versión abajo.
4. **Despertar** en *PS5 del cuarto*: sale "Despertando…" y unos 5 s después queda *encendida* con **Jugar**.
5. **Despertar** en *PS5 de la oficina*: sale "Despertando…" y a los 45 s un aviso de que no se encendió.
6. **Registrar** en *PS5-517*: escribe `1` como Account ID y `00000000` como PIN: debe decir que la consola rechazó el PIN y borrar el PIN. Escribe otras 8 cifras: debe decir "Registro listo." y volver a la lista, donde *PS5-517* ya tiene **Jugar**.
7. **Jugar** en *PS5 de la sala*: pantalla negra con el nombre y "Toca con tres dedos para salir". Espera un minuto: la pantalla no debe oscurecerse. Toca con tres dedos y elige **Salir**: vuelve a la lista.

Reporta qué paso no hizo lo que dice, con una foto si algo se ve mal.

## Pruebas en Linux

Las pruebas de la app son las `ui_*.c` de `ios/tests/core/`: `ios/tests/core/run.sh` las compila con el mismo core y las corre en sus tres configuraciones, también en armv7 bajo `qemu-arm`.

- `ck_ui_console_rows` (`ui_console_rows.c`): cada estado y acción de una fila, el orden (el mismo sin importar en qué orden contestaron las consolas), el `host_id` en minúsculas, el nombre guardado sin NUL, el target del registro, un arreglo corto que no se desborda y la IP que se actualiza cuando la consola contesta desde otra.
