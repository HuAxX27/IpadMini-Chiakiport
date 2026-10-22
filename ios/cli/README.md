# ckctl (A2.4, A2.5)

Herramienta de línea de comandos para buscar, registrar y despertar la PS5 desde el iPad, por SSH. Es la prueba del hito espolón (P2): que la PS5 se encienda desde el iPad. Dueño: A2.

`ckctl` es la cara de línea de comandos de `ios/Bridge/CKConsoles.h`, la misma API que usarán la lista de consolas (A6.2) y el registro (A6.3) de la app, y guarda los registros con `ios/Bridge/ck_registry.h` en el plist de C5, el mismo archivo que leerá la app. Todo es C portable y está probado en Linux contra una PS5 falsa (ver [Pruebas](#pruebas)).

| Archivo | Qué es |
|---|---|
| `ckctl.h`, `ckctl.c` | Los comandos, en `ckctl_main()`, que las pruebas llaman igual que `main.c` |
| `main.c` | Inicia el core y llama a `ckctl_main()` |
| `shared_ck_consoles.c`, `shared_ck_registry.c` | Compilan `ios/Bridge/ck_consoles.c` y `ios/Bridge/ck_registry.c` dentro de `ckctl` (el `Makefile` dice por qué) |
| `Makefile`, `control` | El paquete de Theos `com.thehuax.ckctl`, que instala `/usr/bin/ckctl` |

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

## Pruebas

```sh
./ios/tests/core/run.sh
```

`ck_consoles` (`ios/tests/core/consoles.c`) corre la API y los comandos de `ckctl` contra la PS5 falsa de `ios/tests/fakes/fake_ps5.c`, en 127.0.0.1, con OpenSSL, con mbedTLS y en armv7 bajo `qemu-arm`. La falsa contesta la búsqueda y la señal de despertar como una PS5, y el registro con el mismo cifrado del core: descifra la petición con el PIN, revisa el Account ID y responde con llaves inventadas. Cubre el registro que funciona, cada error de la tabla de arriba, cancelar un registro, despertar con la llave correcta y con otra, y la lista de consolas que cambia cuando una se enciende o deja de contestar. Con el registro guardado: que `regist` lo guarde o, si no puede, imprima las llaves sin tocar el archivo; que `wakeup` despierte sin llave, elija entre varias consolas y encuentre a una que cambió de IP; `discover`, `list` y `forget`.

`ck_registry` (`ios/tests/core/registry.c`) prueba el plist en disco: ida y vuelta, carpetas que no existen, permisos `0600`, un plist como los que escribe Foundation (con llaves de la app, comentarios y datos en varias líneas) y 25 archivos dañados que debe rechazar. `ck_registry_plistlib` (`check_registry_plist.py`) cruza el formato con `plistlib` de Python: lee lo que escribe `ck_registry`, le da a `ck_registry` lo que escribe `plistlib` y revisa que rechace un plist binario.
