# Chiakiport, la app (A6)

La app UIKit del iPad mini 1: busca la PS5 en la red, la registra, la despierta y abre la pantalla de juego. Es un proyecto de Theos que se instala en `/Applications/Chiakiport.app` con el paquete `com.thehuax.chiakiport`. Dueño: A6.

La lógica que se puede probar sin iPad está en C portable con pruebas en Linux (`ios/tests/core/ui_*.c`); el Objective-C solo dibuja y pasa los toques.

| Archivo | Qué es |
|---|---|
| `ck_console_rows.h`, `.c` | Las filas de la lista: junta lo que encontró la búsqueda (`CKConsoles.h`) con los registros guardados (`ck_registry.h`), decide el estado y la acción de cada consola y las ordena |
| `CKConsoleService.h`, `.m` | Lo que las pantallas le piden a las consolas: la lista, despertar, registrar y el registro para la sesión |
| `CKLanConsoleService.h`, `.m` | Las consolas de la red (A6.2): búsqueda, registros guardados y despertar |
| `CKMockConsoleService.h`, `.m` | Consolas simuladas (A6.1), para probar cada pantalla sin consola |
| `ck_ui_regist.h`, `.c` | Al terminar un registro (A6.3): la frase para el usuario y guardarlo encima del archivo de registros |
| `ck_ui_settings.h`, `.c` | Los ajustes que ofrece la app sobre el modelo de C5 y lo que acepta del archivo |
| `ck_ui_stream.h`, `.c` | La pantalla de juego (A6.5): por qué terminó una sesión, cuándo volver a pedirla y la línea de métricas |
| `CKSettingsStore.h`, `.m` | Los ajustes guardados en el iPad (A6.4) |
| `CKSettingsViewController` | La pantalla de ajustes |
| `CKConsoleListViewController`, `CKConsoleCell` | La lista de consolas, la primera pantalla |
| `CKRegistViewController` | El registro: Account ID, PIN y qué corregir si la consola dice que no |
| `CKStreamViewController` | La pantalla de juego: la sesión (`CKSession`), el video, el DualSense y los controles táctiles, el menú y las métricas |
| `CKAppDelegate`, `main.m`, `CKTheme.h` | Arranque y colores |
| `shared_*.c`, `shared_*.m` | Compilan dentro de la app el código de `ios/Bridge/` que también usan `ckctl` y `cksession`, el del DualSense y los controles táctiles de `ios/Input/` y el sumidero de video de `ios/Video/` (el `Makefile` dice por qué) |
| `Makefile`, `control`, `Resources/` | El paquete de Theos, el `Info.plist` y los íconos |

## Pantallas

- **Consolas.** Una fila por consola encontrada y una por consola registrada que no contestó. Primero las registradas, luego por nombre, así las filas no brincan mientras la lista se refresca. Cada fila dice si es PS5 o PS4, su estado en color (encendida, en reposo, no contesta), su IP y el juego abierto, y tiene un botón con lo que hace un toque:
  - **Jugar**, registrada y encendida: abre la pantalla de juego.
  - **Despertar**, registrada y en reposo, o que no contesta pero tiene una IP guardada: manda la señal y muestra "Despertando…" hasta que la consola contesta encendida, o avisa a los 45 s si no se encendió. Una consola apagada del todo no escucha la red, así que solo se despierta desde el reposo; el aviso lo dice.
  - **Registrar**, encendida y sin registrar: abre el registro.
  - Sin botón: una consola sin registrar en reposo (hay que encenderla para registrarla) o una registrada sin IP conocida. Un toque explica qué hacer.
- **Registrar.** El Account ID de PSN (en base64, 12 caracteres que terminan en `=`, o en decimal) y el PIN de 8 cifras que muestra la PS5 en *Ajustes › Sistema › Remote Play › Vincular dispositivo*. Antes de mandar nada revisa que los dos se puedan leer. Si la consola dice que no, muestra qué corregir (`ck_regist_result_string()`), y con un PIN equivocado borra el campo para escribir otro. Al terminar regresa a la lista.
- **Ajustes**, desde el botón de arriba a la derecha de la lista. Resolución (360p, 540p o 720p), cuadros por segundo (30 o 60) y bitrate (automático, o de 2 a 15 Mbps), los de `CKSettings` (C5); por defecto 540p a 30 fps con el bitrate del core, 6 Mbps. Cada cambio se guarda en el momento y vale desde la siguiente sesión; debajo de la lista se ven los actuales. *Volver a los valores por defecto* los regresa a esos. En *Pruebas*, **Consolas simuladas** cambia la lista por las consolas inventadas.
- **Juego.** Pantalla completa, sin barra de estado y sin que el iPad se apague solo. Abre la sesión con la consola y se juega con el DualSense conectado al iPad. Un toque con tres dedos abre el menú: seguir, mostrar las métricas o salir. Con consolas simuladas no abre ninguna sesión.

Toda la app va en horizontal, hacia cualquiera de los dos lados.

## La lista en la red (A6.2)

`CKLanConsoleService` busca con el buscador de `CKConsoles.h`: una búsqueda por segundo a toda la red (broadcast), como `ckctl discover`. La lista se actualiza cada vez que una consola aparece, cambia de estado o deja de contestar 3 búsquedas seguidas. Solo busca mientras la lista está en pantalla y la app al frente.

- **Registros.** Lee los de `/var/mobile/Library/Chiakiport/consoles.plist`, el plist de C5 que también escribe `ckctl regist`, cada vez que la lista vuelve a la pantalla, así ve lo que `ckctl` cambió mientras tanto. Si el archivo no se puede leer, lo dice debajo de la lista y nunca lo sobrescribe.
- **IP nueva.** Si una consola registrada contesta desde otra IP (el router le dio otra), la guarda en el registro, como `ckctl`, para despertarla ahí la próxima vez. Antes de guardar vuelve a leer el archivo, para no borrar lo que `ckctl` haya escrito.
- **Primera búsqueda.** Al abrir la lista espera hasta 2 s a que contesten, para no mostrar un momento como "no contesta" a las registradas que sí están.
- **Despertar** manda la señal de `ck_console_wakeup()` con el registro guardado a la IP donde contestó la consola, o a la última conocida.
- **Registrar** (A6.3) corre `ck_regist` en segundo plano con la IP y el tipo de consola de la búsqueda, como `ckctl regist`. Si la consola dice que no, muestra el porqué con el código HTTP y la razón cuando los da. Si dice que sí, vuelve a leer el registro y agrega la consola encima de lo que haya, igual que `ckctl` (`ck_ui_regist_save()`); si el archivo no se puede leer, ya hay 8 consolas o no se pudo escribir, lo dice y no guarda nada. Salir de la pantalla cancela el registro.

## La pantalla de juego (A6.5)

`CKStreamViewController` abre la sesión de `CKSession` (C1) con el registro guardado, la IP donde contestó la consola y los ajustes de la app.

- **Video.** `CKVideoSink` (C2, A3) decodifica el H.264 con VideoToolbox y lo dibuja en una vista a pantalla completa con `AVSampleBufferDisplayLayer`, ajustado a la pantalla sobre negro. El sumidero se limpia antes de cada intento y se libera solo después de que `-[CKSession stop]` regresa. Al terminar la sesión la imagen se quita para que se lea el motivo.
- **Control.** El DualSense conectado directo al iPad (`CKDS5Source`, A5.8) con el mapeo por defecto (`ck_pad_map.h`, A5.4), y los controles táctiles (`CKTouchControlsView`, A5.5) encima del video. Las dos fuentes van a la sesión y se combinan (C4). Los controles táctiles se muestran solo mientras no hay DualSense conectado; ocultos no presionan nada.
- **Menú.** Un toque con tres dedos fuera de los controles táctiles: tres dedos sobre los controles son parte del juego.
- **"Ya está en uso".** Justo después de cerrar una sesión, la PS5 rechaza otra durante 10 a 20 s: a veces primero no acepta la conexión de control y luego dice que ya está en uso. Si eso pasa antes de que llegue la sesión, la pantalla la vuelve a pedir cada 2 s, hasta 15 veces (unos 30 s), y lo dice. Una conexión de control que falla sin una sesión reciente (30 s) es un error de verdad y se muestra enseguida.
- **Cuando termina**, dice por qué en español (`ck_ui_quit_reason_string()`), con el detalle del core debajo, y ofrece *Volver a intentar* o *Volver a la lista*. Al mandar la app al fondo cierra la sesión: iOS la detendría a los pocos segundos.
- **Métricas**, desde el menú: una línea arriba con cuadros por segundo, tiempo de decodificación, bitrate, cuadros perdidos, audio y memoria, cada segundo.
- **Audio** se conecta aquí cuando llegue el reproductor de A4 (C3). Mientras, el sonido sale de la tele.

## Consolas simuladas (A6.1)

Con **Consolas simuladas** encendido en Ajustes, la lista sale de `CKMockConsoleService`, cuatro consolas inventadas, una de cada tipo de fila:

| Consola | Estado | Botón |
|---|---|---|
| PS5 de la sala | registrada, encendida, con un juego abierto | Jugar |
| PS5 del cuarto | registrada, en reposo | Despertar: se enciende 5 s después |
| PS5 de la oficina | registrada, no contesta | Despertar: nunca se enciende, y a los 45 s la lista lo avisa |
| PS5-517 | encendida, sin registrar | Registrar: tarda 2 s y funciona con cualquier Account ID válido y cualquier PIN de 8 cifras, salvo `00000000`, que la simulación rechaza como un PIN equivocado |

Nada de esto se guarda: al cerrar la app vuelve a como empezó. Debajo de la lista lo dice, junto con la versión y el commit de la app.

## Dónde se guardan los ajustes (A6.4)

En `/var/mobile/Library/Chiakiport/settings.plist`, junto a los registros de `ckctl`, como un plist XML con tres números: `resolution` (la altura: 360, 540 o 720), `fps` (30 o 60) y `bitrate_kbps` (0 es el del core). `ck_ui_settings.h` tiene el formato. Se escribe completo en un archivo temporal que luego reemplaza al anterior, así que cerrar la app a la mitad nunca lo deja roto, y sobrevive a cerrar la app y a reiniciar el iPad.

Al abrir, un valor que falta o que la app no ofrece vuelve a su valor por defecto sin tocar los demás, y la app abre igual. Un bitrate escrito a mano entre 1 y 30 Mbps se respeta aunque no tenga botón; la pantalla lo dice. No tiene datos de la cuenta ni de la consola.

## Compilar

El CI la empaqueta en cada PR con los demás proyectos de Theos (`ios/toolchain/ci-ipad.sh`) y la deja en el artefacto `ipad-debs` como `com.thehuax.chiakiport_<versión>_iphoneos-arm.deb`. A mano, después de `ci-ipad.sh`, o de `build-deps.sh` y `core-check.sh`:

```sh
make -C ios/UI package FINALPACKAGE=1
```

Como `ios/cli`, enlaza el core de `build-ios-core/` y mbedTLS y Opus de `build-ios-deps/prefix/`; `CK_CORE_BUILD` y `CK_IOS_PREFIX` cambian esas rutas.

## Probar en el iPad

La instalación y lo que se corre por SSH lo hace un agente desde la laptop (`ios/toolchain/deploy.ps1`).

### Con consolas simuladas (P2-7)

1. Abre **Chiakiport** desde la pantalla de inicio (ícono azul con un triángulo de "play").
2. Gira el iPad: la app se queda en horizontal y se voltea al girarlo 180°, pero no se pone en vertical.
3. En **Ajustes**, enciende **Consolas simuladas** y regresa: debe salir la lista de la tabla de arriba, con "Consolas simuladas" y la versión abajo.
4. **Despertar** en *PS5 del cuarto*: sale "Despertando…" y unos 5 s después queda *encendida* con **Jugar**.
5. **Despertar** en *PS5 de la oficina*: sale "Despertando…" y a los 45 s un aviso de que no se encendió.
6. **Registrar** en *PS5-517*: escribe `1` como Account ID y `00000000` como PIN: debe decir que la consola rechazó el PIN y borrar el PIN. Escribe otras 8 cifras: debe decir "Registro listo." y volver a la lista, donde *PS5-517* ya tiene **Jugar**.
7. **Jugar** en *PS5 de la sala*: pantalla negra con el nombre y "Toca con tres dedos para salir". Espera un minuto: la pantalla no debe oscurecerse. Toca con tres dedos y elige **Salir**: vuelve a la lista.

8. **Ajustes**: elige 720p, 60 y 8. Debajo de la lista debe decir `Ajustes: 720p · 60 fps · 8 Mbps`, sin "(automático)". No uses 10: a 720p es también el bitrate automático y no se distingue de *Auto*.
9. Cierra la app desde el multitarea (doble clic al botón de inicio y arrastra la tarjeta de la app hacia arriba hasta que desaparezca; solo salir al inicio no la cierra) y ábrela otra vez: los ajustes siguen en 720p, 60 y 8.
10. Reinicia el iPad, vuelve a correr Carbon y abre la app: siguen igual. Al final, *Volver a los valores por defecto* deja 540p, 30 y Auto.

Reporta qué paso no hizo lo que dice, con una foto si algo se ve mal. Desde la laptop, `ssh ipad cat /var/mobile/Library/Chiakiport/settings.plist` muestra lo que quedó guardado (`bitrate_kbps` 8000 tras el paso 8; 0 es *Auto*). Que la app arrancara después de esa escritura (la hora de inicio del proceso en `ps`) dice que el paso 9 sí la cerró.

### Con la PS5 (A6.2)

Con la PS5 ya registrada con `ckctl regist` y en la misma red. Antes, el agente corre `ssh ipad ckctl list` para ver qué consolas hay registradas.

1. En **Ajustes**, apaga **Consolas simuladas** si estaba encendido.
2. Pon la PS5 en reposo (luz naranja) y abre la lista: en 2 o 3 s debe salir la PS5 *en reposo*, con su IP y **Despertar**.
3. **Despertar**: "Despertando…" y entre 10 y 20 s después debe quedar *encendida*, con **Jugar** y el juego abierto si hay uno.
4. Desde el control, pon la PS5 en reposo otra vez: en unos segundos la fila vuelve a *en reposo*.
5. Apaga del todo la PS5 (mantén el botón de encendido hasta el segundo pitido) o desconéctala de la red: unos segundos después la fila dice *no contesta*. Enciéndela y vuelve a aparecer.

### Registrar la PS5 desde la app (A6.3)

Antes, el agente guarda una copia del registro y quita la PS5 para que la app la vea sin registrar:

```sh
ssh ipad cp -p /var/mobile/Library/Chiakiport/consoles.plist /var/mobile/Library/Chiakiport/consoles.plist.bak
ssh ipad ckctl forget -c <ID de la PS5 en ckctl list>
```

1. Con la PS5 encendida, abre la lista: la PS5 sale *encendida* con **Registrar**.
2. En la PS5: *Ajustes › Sistema › Remote Play › Vincular dispositivo*. Queda un código de 8 cifras en la tele.
3. En la app, **Registrar**: escribe el Account ID de PSN y, primero, un código equivocado (cambia una cifra). En unos 6 s debe decir que la consola rechazó el PIN y borrar el campo.
4. Pide otro código en la PS5 si el anterior ya no se ve, escríbelo bien y **Registrar**: "Registro listo." y regresa a la lista, donde la PS5 sale con **Jugar**.
5. Pon la PS5 en reposo y usa **Despertar** desde la app: se enciende con el registro nuevo.

Después, el agente corre `ssh ipad ckctl list`, que debe mostrar la PS5 registrada otra vez, con su IP. Si algo falló, regresa la copia con `ssh ipad mv /var/mobile/Library/Chiakiport/consoles.plist.bak /var/mobile/Library/Chiakiport/consoles.plist`.

### Jugar (A6.5, sin audio todavía)

Con la PS5 registrada, `com.thehuax.ckdirect` instalado y el DualSense emparejado con el iPad. El sonido sale de la tele: el iPad todavía no lo reproduce.

1. Con el DualSense apagado y la PS5 encendida, toca **Jugar**: "Conectando con…" y en 2 o 3 s sale la imagen de la PS5 en el iPad, ajustada a la pantalla, con los controles táctiles encima.
2. Navega el menú de la PS5 solo con la pantalla: cruceta, stick, ✕ y ○.
3. Enciende el DualSense con el botón PS: en un segundo se quitan los controles táctiles. Abre un juego y juega un par de minutos: la imagen se mueve fluida y la PS5 responde al pad.
4. Toca con tres dedos en el centro y elige **Mostrar métricas**: arriba sale una línea que cambia cada segundo, con unos 30 fps y pocos o ningún cuadro perdido. Toma una foto de esa línea.
5. Apaga el DualSense (mantén PS 10 s): en un segundo vuelven los controles táctiles.
6. **Salir** y, enseguida, **Jugar** otra vez: debe decir que la consola está cerrando la sesión anterior, probar de nuevo y conectar en 10 a 20 s, con imagen.
7. Pon la PS5 en reposo desde su menú: la imagen se quita y la pantalla dice que la consola se apagó o entró en reposo, con *Volver a intentar* y *Volver a la lista*.

Reporta qué paso no hizo lo que dice, con una foto si algo se ve mal: sobre todo si la imagen se congela, se ve cortada o con bloques, o tarda en seguir al pad.

## Pruebas en Linux

Las pruebas de la app son las `ui_*.c` de `ios/tests/core/`: `ios/tests/core/run.sh` las compila con el mismo core y las corre en sus tres configuraciones, también en armv7 bajo `qemu-arm`.

- `ck_ui_regist` (`ui_regist.c`): guardar un registro en un archivo que no existe (ni su carpeta), encima de los que dejó `ckctl` sin perderlos, reemplazando el de la misma consola, con el registro lleno (no toca el archivo), con un archivo que no se puede leer (lo deja igual) o escribir, y la frase con el código HTTP y la razón, recortada sin partir una letra.
- `ck_ui_stream` (`ui_stream.c`): antes de que llegue la sesión, se vuelve a pedir solo con "ya está en uso", o con la conexión de control fallida justo después de otra sesión o a mitad de los reintentos, hasta el límite y cubriendo la espera de la PS5; cada motivo de fin tiene su frase; la línea de métricas con coma decimal, redondeo, valores imposibles y un búfer corto.
- `ck_ui_settings` (`ui_settings.c`): cada combinación que ofrece la pantalla sobrevive al archivo, un archivo vacío o con valores que no se ofrecen da los de por defecto (cada valor por su lado), el bitrate a mano se respeta en su rango, el bitrate automático es el del core para cada resolución y el resumen de la lista.
- `ck_ui_console_rows` (`ui_console_rows.c`): cada estado y acción de una fila, el orden (el mismo sin importar en qué orden contestaron las consolas), el `host_id` en minúsculas, el nombre guardado sin NUL, el target del registro, un arreglo corto que no se desborda y la IP que se actualiza cuando la consola contesta desde otra.
