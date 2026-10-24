# ckctl y cksession (A2.4 a A2.8)

Herramientas de línea de comandos para el iPad, por SSH. `ckctl` busca, registra y despierta la PS5: es la prueba del hito espolón (P2), que la PS5 se encienda desde el iPad. `cksession` abre y cierra sesiones de streaming con ella a través de `CKSession`, la clase que usará la app. Dueño: A2.

`ckctl` es la cara de línea de comandos de `ios/Bridge/CKConsoles.h`, la misma API que usarán la lista de consolas (A6.2) y el registro (A6.3) de la app, y guarda los registros con `ios/Bridge/ck_registry.h` en el plist de C5, el mismo archivo que leerá la app. Todo es C portable y está probado en Linux contra una PS5 falsa (ver [Pruebas](#pruebas)).

| Archivo | Qué es |
|---|---|
| `ckctl.h`, `ckctl.c` | Los comandos, en `ckctl_main()`, que las pruebas llaman igual que `main.c` |
| `main.c` | Inicia el core y llama a `ckctl_main()` |
| `cksession.m` | `cksession`, en Objective-C sobre `CKSession` |
| `shared_*.c`, `shared_CKSession.m` | Compilan dentro de las herramientas el código de `ios/Bridge/` que también usa la app (el `Makefile` dice por qué) |
| `Makefile`, `control` | El paquete de Theos `com.thehuax.ckctl`, que instala `/usr/bin/ckctl` y `/usr/bin/cksession` |

## Uso

```sh
ckctl discover                          # todas las consolas de la red, durante 2 s
ckctl discover -H 192.168.0.10          # solo esa, y regresa en cuanto contesta
ckctl regist -H 192.168.0.10 -a <ACCOUNT_ID> -p <PIN>
ckctl wakeup -w 30                      # la consola registrada, con lo que guardó regist
ckctl list
ckctl forget -c <NOMBRE, ID O IP>
```

- `discover` imprime una línea por consola: IP, PS5 o PS4, estado (`encendida` o `en reposo`), nombre, `id` (la MAC, que es como se reconoce una consola registrada) y firmware, y `registrada` si lo está. De las registradas guarda la IP en la que contestaron. Con `-t` cambia los segundos de búsqueda.
- `regist` registra el iPad con la consola y guarda el registro. El Account ID de PSN va en base64 (12 caracteres que terminan en `=`, como lo muestra chiaki-ng) o en decimal. El PIN son las 8 cifras que muestra la PS5 en **Ajustes > Sistema > Remote Play > Vincular dispositivo**; puede ir con espacios. Antes de registrar, `ckctl` le pregunta a la consola si es PS5 o PS4. Registrar otra vez la misma consola reemplaza su registro. Si no puede guardar (el archivo está dañado, no se puede escribir o ya hay 8 consolas), imprime los datos del registro para no perderlos, no toca el archivo y termina con 1.
- `wakeup` manda la señal para despertar a una consola registrada que está en reposo. Con una sola consola registrada no hace falta decir cuál; con varias, `-c` acepta su nombre, su `id` (en mayúsculas o minúsculas) o su IP. Busca a la consola en `-H`, o en la última IP en la que la vio, y si ya no está ahí (el router le dio otra), en toda la red; `-b DIRECCIÓN` cambia dónde busca, por ejemplo `-b 192.168.0.255`. Si la encuentra en otra IP, la guarda. Si la consola ya está encendida, no manda nada. Con `-w SEGUNDOS` espera a que se encienda, repitiendo la señal cada 5 s por si se pierde.
- `wakeup -H IP -k LLAVE` hace lo mismo sin el registro guardado, con la `rp_regist_key` a mano. Con `--ps4` despierta una PS4.
- `list` muestra las consolas registradas, sin sus llaves. `forget -c` borra una.
- `-s RUTA`, después del comando, usa otro archivo de registros. `-v`, antes del comando, muestra el registro del core en la salida de errores.

Termina con 0 si salió bien, 1 si falló (sin consolas, registro rechazado o sin guardar, la consola no se encendió) y 2 si el comando estaba mal escrito.

### El registro guardado

`regist` guarda los registros en `/var/mobile/Library/Chiakiport/consoles.plist`, el plist de C5 que leerá la app: un diccionario por consola, con su `id` en mayúsculas como llave y los campos de `ChiakiRegisteredHost` más la última IP (el formato completo está en `ios/Bridge/ck_registry.h`). Es un plist XML que `NSDictionary` lee tal cual.

- Sobrevive a reinicios: está en el disco del iPad, no en memoria.
- Tiene las llaves de la consola, así que se escribe con permisos `0600`. Si `ckctl` corre como `root` por SSH, el archivo y la carpeta quedan a nombre del dueño de `/var/mobile/Library` (`mobile`), para que la app pueda leerlos.
- Se escribe completo en un archivo temporal que luego reemplaza al anterior, así que un corte a la mitad nunca deja un archivo roto.
- Si el archivo no se puede leer como registro, `ckctl` no lo sobrescribe: puede tener registros que no entendió.

**Los datos del registro no van al repo**, y tampoco el plist: vive solo en el iPad.

### Errores del registro

El core solo dice si el registro funcionó o no. `ck_regist` lee la línea de error que el core escribe antes de fallar y la convierte en un `CKRegistResult`, que `ckctl` imprime con su explicación:

| Resultado | Qué pasó | Qué hacer |
|---|---|---|
| `CK_REGIST_NOT_FOUND` | Nada contestó la búsqueda en el puerto 9295 | Revisar la IP y que la PS5 esté encendida y en la pantalla de vincular dispositivo |
| `CK_REGIST_WRONG_PIN` | HTTP 403 con razón `0x80108b09` | Pedir un PIN nuevo: cada uno dura poco |
| `CK_REGIST_WRONG_ACCOUNT` | HTTP 403 con razón `0x80108b02` | Revisar que el Account ID sea el de la cuenta de la PS5 |
| `CK_REGIST_REJECTED` | Otra respuesta de error; `ckctl` imprime el código y la razón | Reportar el código |
| `CK_REGIST_UNREACHABLE` | La consola rechazó la conexión o no hay ruta | Revisar la red |
| `CK_REGIST_NO_REPLY`, `CK_REGIST_BAD_REPLY` | La consola no contestó, o contestó sin las llaves | Repetir; si sigue, reportar con `-v` |

## cksession

```sh
cksession                       # una sesión de 10 s con la consola registrada
cksession -n 20 -t 5            # abre y cierra 20 veces, 5 s cada una
cksession -c Sala -r 720 -f 60  # otra consola, 720p a 60 cuadros por segundo
cksession -t 60 -g /var/mobile/captura   # un minuto, grabado en captura.h264 y captura.opus
```

Usa el registro guardado como `ckctl wakeup`: sin `-c`, la única consola registrada, en la última IP que guardó `ckctl` (o en `-H`). La consola tiene que estar encendida: `ckctl wakeup -w 60` primero. Pide H.264 a 540p y 30 cuadros por segundo salvo que `-r`, `-f` o `-b` digan otra cosa.

Imprime cada cambio de estado (`conectando`, `transmitiendo`, `detenida`), la razón con la que terminó cada sesión, las métricas de cada segundo (cuadros, kbps, cuadros perdidos, memoria) y la memoria residente después de cerrar cada ronda. Si la memoria crece ronda tras ronda, hay una fuga. Termina con 0 si todas las rondas llegaron a transmitir. `CK_LOG_VERBOSE=1 cksession ...` muestra todo lo que dice el core.

Después de cerrar una sesión, la PS5 tarda unos segundos en soltarla: mientras tanto contesta que Remote Play ya está en uso (`0x80108b10`). En la primera prueba en el iPad tardó entre 6 y 9 s. Por eso, si una ronda recibe esa respuesta, `cksession` vuelve a pedir la sesión cada 2 s, hasta 10 veces, y dice cuántos segundos después de cerrar la anterior la aceptó.

Después de cada ronda dice qué video mandó la consola, leído del encabezado (SPS) que llega antes de los cuadros: el códec, el perfil, el nivel y el tamaño, por ejemplo `video: H.264 Main, nivel 3.2, 960x540`, que es lo que mandó la PS5 en la primera prueba en el iPad. Si el tamaño no es el que pidió lo dice, y si el A5 no lo puede decodificar (H.265, 10 bits, entrelazado, más de 1080p o más del nivel 4.1) también. La salida es 0 solo si todas las rondas transmitieron H.264 que el A5 decodifica.

Por ahora no muestra el video ni toca el audio: los cuenta. La pantalla y el sonido llegan con la app (A3, A4 y A6).

### Grabar una sesión (A2.8)

Con `-g RUTA`, `cksession` graba lo que llega de la consola en dos archivos, para probar el video y el audio sin consola (con `-n`, cada ronda en `RUTA-1`, `RUTA-2`...):

- `RUTA.h264`: el video en Annex B crudo, tal como lo entrega el core, con el encabezado del perfil primero y luego cada cuadro. Es lo que leen `ios/tests/ck_h264_tests.sh` y `ffplay`.
- `RUTA.opus`: el audio en Ogg Opus, que guarda dónde termina cada paquete y se escucha con `ffplay` o VLC. Un cuadro de audio que se perdió en la red queda como un paquete vacío que le dice al decodificador que lo disimule, así que la duración no cambia.

Al cerrar cada ronda dice cuántos cuadros y cuántos segundos de audio grabó. A 540p son unos 45 MB por minuto. Grabar escribe a disco en el hilo del video, con un búfer de 256 KB; sirve para pruebas, no para jugar.

## Compilar

El CI lo empaqueta en cada PR con los demás proyectos de Theos (`ios/toolchain/ci-ipad.sh`) y lo deja en el artefacto `ipad-debs` como `com.thehuax.ckctl_<versión>_iphoneos-arm.deb`. A mano, después de `ci-ipad.sh` o de `build-deps.sh` y `core-check.sh`:

```sh
make -C ios/cli package FINALPACKAGE=1
```

El `Makefile` enlaza el core de `build-ios-core/` y mbedTLS y Opus de `build-ios-deps/prefix/`, donde los dejan esos scripts; `CK_CORE_BUILD` y `CK_IOS_PREFIX` cambian esas rutas.

## Probar en el iPad (P2)

1. Instala el `.deb` del artefacto `ipad-debs` con `ios/toolchain/deploy.ps1` (ver `ios/toolchain/README.md`).
2. Con la PS5 encendida y el iPad en la misma red, por SSH: `ckctl discover`. Debe salir la PS5 con su IP.
3. En la PS5, abre **Ajustes > Sistema > Remote Play > Vincular dispositivo** y, mientras se ve el PIN: `ckctl regist -H <IP de la PS5> -a <tu Account ID> -p <PIN>`. Debe decir `Guardé el registro de ...`.
4. Pon la PS5 en reposo y espera a que la luz quede naranja. `ckctl discover` debe decir `en reposo` y `registrada`.
5. `ckctl wakeup -w 60`. La PS5 debe encenderse y `ckctl` debe decir en cuántos segundos.
6. Que el registro sobrevive a un reinicio (A2.5): reinicia el iPad, vuelve a correr Carbon, pon la PS5 en reposo otra vez y repite `ckctl list` (debe salir la PS5) y `ckctl wakeup -w 60`.

Reporta lo que imprimió cada paso (sin el Account ID ni el PIN) y, si algo falla, repite ese paso con `-v`.

## Probar la sesión en el iPad (A2.6)

1. Con la PS5 registrada (pasos de arriba) y encendida: `ckctl wakeup -w 60`.
2. `cksession -t 10`. Debe pasar por `conectando` y `transmitiendo`, imprimir unos 30 cuadros por segundo durante 10 s, terminar con `detenida` y decir `video: H.264 ..., 960x540` (A2.7).
3. `cksession -n 20 -t 5`. Debe decir `Transmitió en 20 de 20 rondas, con H.264 que el A5 decodifica en 20.`, y la memoria después de cada ronda debe quedarse más o menos igual.

Reporta la salida de los dos comandos. Si una ronda no llega a `transmitiendo`, repite con `CK_LOG_VERBOSE=1 cksession -t 10` y guarda la salida.

## Grabar un minuto en el iPad (A2.8)

1. Con la PS5 encendida, en un juego o en el menú: `cksession -t 60 -g /var/mobile/captura`. Al final debe decir `grabé ... cuadros` y `60.0 s de audio` (más o menos).
2. Desde la computadora, cópialos: `scp ipad:/var/mobile/captura.h264 ipad:/var/mobile/captura.opus .`
3. Revisa que se vean y se oigan: `ffplay captura.h264` y `ffplay captura.opus`.
4. Deja los dos archivos en la carpeta del proyecto (no en el repo: pesan) y bórralos del iPad con `rm /var/mobile/captura.*`.

Reporta las dos líneas de `grabé` y si el video y el audio se ven y se oyen bien.

## Pruebas

```sh
./ios/tests/core/run.sh
```

`ck_consoles` (`ios/tests/core/consoles.c`) corre la API y los comandos de `ckctl` contra la PS5 falsa de `ios/tests/fakes/fake_ps5.c`, en 127.0.0.1, con OpenSSL, con mbedTLS y en armv7 bajo `qemu-arm`. La falsa contesta la búsqueda y la señal de despertar como una PS5, y el registro con el mismo cifrado del core: descifra la petición con el PIN, revisa el Account ID y responde con llaves inventadas. Cubre el registro que funciona, cada error de la tabla de arriba, cancelar un registro, despertar con la llave correcta y con otra, y la lista de consolas que cambia cuando una se enciende o deja de contestar. Con el registro guardado: que `regist` lo guarde o, si no puede, imprima las llaves sin tocar el archivo; que `wakeup` despierte sin llave, elija entre varias consolas y encuentre a una que cambió de IP; `discover`, `list` y `forget`.

`ck_session` (`ios/tests/core/session.c`) prueba `ios/Bridge/ck_session.c`, la mitad en C de `CKSession`, contra la misma PS5 falsa, que contesta la petición de sesión como una PS5 (la acepta, dice que Remote Play está en uso, no contesta o cuelga) pero no transmite. Cubre cada forma de terminar, detener mientras conecta, volver a arrancar la misma sesión, el hilo de entrada que lee los controles a 125 Hz, y abrir y cerrar 20 veces sin que crezcan los sockets, los hilos ni la memoria. Con ASan y `--no-fork`, LeakSanitizer revisa además que no quede memoria sin liberar.

`ck_video_info` (`ios/tests/core/video_info.c`) prueba `ios/Bridge/ck_video_info.c`, que lee el encabezado de video, con encabezados de x264 (540p, 720p, 360p, 1080p, entrelazado, 10 bits, 4:2:2 y 4:4:4), uno de x265 y otros escritos a mano para lo que x264 no hace (listas de escalado, `pic_order_cnt_type` 1, bytes de prevención de emulación) o está roto. `ck_session` revisa además que el launch spec que el core le manda a la PS5 pida `avc` con el tamaño y los cuadros por segundo de los ajustes.

`ck_recorder` (`ios/tests/core/recorder.c`) prueba `ios/Bridge/ck_recorder.c` con paquetes Opus de libopus como los de la PS5: lee de vuelta cada página Ogg con un CRC propio, decodifica cada paquete con libopus y revisa la duración de cada página, incluidos los cuadros perdidos, paquetes de más de 255 bytes, paquetes de dos cuadros, encabezados que cambian, archivos que no se pueden abrir y un disco lleno. `ck_recorder_ffmpeg` (`check_recording.py`) graba 3 s y revisa con ffmpeg que el `.opus` se decodifique sin errores a exactamente esas muestras y que el `.h264` tenga los mismos cuadros que la muestra de x264.

`ck_registry` (`ios/tests/core/registry.c`) prueba el plist en disco: ida y vuelta, carpetas que no existen, permisos `0600`, un plist como los que escribe Foundation (con llaves de la app, comentarios y datos en varias líneas) y 25 archivos dañados que debe rechazar. `ck_registry_plistlib` (`check_registry_plist.py`) cruza el formato con `plistlib` de Python: lee lo que escribe `ck_registry`, le da a `ck_registry` lo que escribe `plistlib` y revisa que rechace un plist binario.
