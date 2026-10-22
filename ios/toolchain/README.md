# Toolchain de iOS 9

Todo lo que hace falta para compilar en Linux para el iPad mini 1 (iOS 9.3.5, armv7), sin Mac:

- [Theos](https://theos.dev), el sistema de build.
- El toolchain de Linux que Theos instala por defecto ([L1ghtmann/llvm-project](https://github.com/L1ghtmann/llvm-project)): clang 11, ld64-609 y ldid. Ese ld64 todavía enlaza armv7, cosa que Xcode 14 y posteriores ya no hacen.
- `iPhoneOS9.3.sdk` de [theos/sdks](https://github.com/theos/sdks), parcheado por `fix-sdk93.sh`.

Cada pieza está fijada en `versions.env`, con su commit o su sha256, y `install-theos.sh` rechaza cualquier descarga cuyo hash no coincida.

| Archivo | Qué hace |
|---|---|
| `versions.env` | Versiones y hashes fijados |
| `install-theos.sh` | Instala Theos, el toolchain y el SDK en `$THEOS` |
| `fix-sdk93.sh` | Parcha el SDK 9.3 para el linker actual |
| `Dockerfile` | Imagen reproducible con todo lo anterior |
| `ios9-armv7.cmake` | Archivo de toolchain de CMake: armv7, iOS 9.0, SDK 9.3 |
| `smoke-test.sh`, `smoke/` | Prueba de humo: compila un binario con Theos y con CMake, y revisa que sirva en el iPad |
| `hello/` | Hola mundo UIKit (CK Hola), empaquetado como `.deb`: la prueba P1 en el iPad |
| `build-deps.sh` | Compila mbedTLS y Opus para armv7 como librerías estáticas |
| `core-check.sh`, `core-check/` | Compila el core completo para el iPad y lo enlaza en un ejecutable firmado |
| `ci-ipad.sh` | Lo que el CI compila para el iPad en cada PR: contratos, core y paquetes `.deb` |
| `apt-linux-tests.sh` | Instala en Ubuntu 24.04 lo que piden las pruebas en Linux, también para armhf |
| `deploy.ps1`, `deploy.sh` | Instalan `.deb` en el iPad por SSH, desde Windows o desde Linux |

## Con Docker (recomendado)

Desde la raíz del repo:

```sh
docker build -t ck-ios-toolchain ios/toolchain
docker run --rm -v "$PWD":/work -u "$(id -u):$(id -g)" ck-ios-toolchain ios/toolchain/smoke-test.sh
```

En Windows 11 sirve Docker Desktop con WSL 2; quita `-u ...` si no corres dentro de WSL. En CachyOS sirven `docker` o `podman` con los mismos comandos. Dentro de la imagen, `THEOS=/opt/theos` y el repo queda en `/work`. Cualquier proyecto de Theos se compila igual: `docker run --rm -v "$PWD":/work -w /work/<carpeta> ck-ios-toolchain make`.

## Sin Docker, en Linux

Hacen falta `git curl tar xz make perl fakeroot rsync`, más libtinfo para el toolchain:

```sh
sudo apt install git curl xz-utils make perl fakeroot rsync libtinfo6     # Ubuntu, Debian
sudo pacman -S --needed git curl xz make perl fakeroot rsync ncurses      # Arch, CachyOS
THEOS=$HOME/theos ios/toolchain/install-theos.sh
THEOS=$HOME/theos ios/toolchain/smoke-test.sh
```

Correr `install-theos.sh` otra vez solo rehace lo que cambió en `versions.env`. Solo hay toolchain precompilado para Linux x86_64 y aarch64.

## Qué se parcha en el SDK y por qué

Los stubs `.tbd` del SDK 9.3 de theos/sdks se regeneraron a partir de binarios del dispositivo, y el ld64 actual no enlaza con ellos tal cual:

1. **Falta `liblaunch`.** libSystem la reexporta, pero no viene en el SDK, y ld64-609 se detiene con `file not found: /usr/lib/system/liblaunch.dylib`. Se agrega un stub vacío. Nada en el proyecto usa APIs de launchd; si algo las usara, fallaría al enlazar, nunca en el iPad.
2. **Faltan `memset`, `memcpy`, `strcmp` y compañía.** En el dispositivo son alias de `_platform_memset` y similares, y el generador de stubs perdió los alias. Clang emite llamadas a `memset` y `memcpy` por su cuenta, así que casi cualquier programa fallaba al enlazar. Se agregan exactamente los 14 alias que implementa la libsystem_platform de iOS 9.3, en libsystem_c, donde los ponen los SDK posteriores de Apple. No se agrega nada de iOS 10: `clock_gettime` debe seguir fallando.
3. **Advertencias de simulador.** Cada stub lista también i386 y x86_64, y ld64-609 avisa `built for iOS Simulator` una vez por librería en cada enlace. Este SDK es solo para dispositivo, así que se quitan esas dos arquitecturas y las advertencias se van.

## CMake

`ios9-armv7.cmake` es el archivo de toolchain para todo lo que se compila con CMake, empezando por el core:

```sh
cmake -S . -B build-ios -G Ninja -DCMAKE_TOOLCHAIN_FILE=ios/toolchain/ios9-armv7.cmake
```

- Toma `THEOS` del entorno o de `-DTHEOS=...` (por defecto `/opt/theos`, como en la imagen).
- Compila con `--target=armv7-apple-ios9.0`, `-arch armv7`, el SDK 9.3 como sysroot y `-miphoneos-version-min=9.0`. NEON viene activado por defecto en armv7.
- Todo compila con `-Werror=unguarded-availability`: usar una API más nueva que iOS 9.0 sin revisar su disponibilidad rompe el build.
- `find_library()` y `find_path()` buscan solo en el SDK y en `CK_IOS_PREFIX` (las librerías de `build-deps.sh`), nunca en `/usr` del Linux. Los programas (`python3`, `protoc`) sí se buscan en el Linux. El `include/` de `CK_IOS_PREFIX` va en todas las compilaciones, porque el core incluye `<mbedtls/...>` sin preguntarle a CMake dónde está.
- `ck_ios_sign(<target>)` firma un ejecutable con ldid al enlazarlo. iOS no corre nada sin firma, ni siquiera con jailbreak.
- Usa `llvm-ar` y el `ranlib` de cctools: el `ar` de cctools llama a un `ranlib` con prefijo que el toolchain no trae.

`core-check.sh` compila el core como lo va a usar el iPad (solo red local, mbedTLS y Opus, sin holepunch) y enlaza el archivo completo en `ck-core-check`, un ejecutable armv7 firmado. Toma mbedTLS y Opus de `CK_IOS_PREFIX` (por defecto `build-ios-deps/prefix`) y, si ahí no están, corre `build-deps.sh` primero.

## Librerías para armv7

`build-deps.sh` compila con `ios9-armv7.cmake` las librerías que el core necesita y que no vienen con él, como librerías estáticas en un prefijo (por defecto `build-ios-deps/prefix`, fuera de git):

- **mbedTLS 2.28 LTS** (`libmbedtls.a`, `libmbedx509.a`, `libmbedcrypto.a`). El core no compila con mbedTLS 3, que volvió privados campos que usa.
- **Opus** (`libopus.a`), con el código NEON siempre activo. Todos los iPad que corren iOS 9 tienen NEON, y en iOS Opus no puede detectarlo en tiempo de ejecución, así que se compila como si siempre estuviera.

```sh
docker run --rm -v "$PWD":/work ck-ios-toolchain ios/toolchain/build-deps.sh
cmake ... -DCMAKE_TOOLCHAIN_FILE=ios/toolchain/ios9-armv7.cmake -DCK_IOS_PREFIX=$PWD/build-ios-deps/prefix
```

Las versiones y sus commits están en `versions.env`, y el script rechaza un tag que no apunte al commit fijado. Al final revisa que las cuatro librerías sean solo armv7 y que `libopus.a` traiga el código NEON. Las licencias quedan en `share/licenses/` del prefijo.

nanopb, jerasure y gf-complete no se compilan aquí: el CMake del core los compila desde sus submódulos en `third-party/`, con el mismo toolchain.

El CI (`ios-toolchain.yml`) corre `build-deps.sh` en la imagen y guarda el prefijo como artefacto `ios-deps-armv7`: `include/`, `lib/` y `share/licenses/`. Los archivos de pkg-config no van, porque traen la ruta del CI.

## Qué revisa la prueba de humo

`smoke-test.sh` compila `smoke/` con Theos y con CMake en un directorio temporal, sin dejar nada en el repo, y falla si alguno de los dos binarios:

- no es solo armv7,
- no trae `LC_VERSION_MIN_IPHONEOS` 9.0 con SDK 9.3,
- no está firmado con ldid,
- o no enlaza libSystem.

Después revisa que una API de iOS 10 (`clock_gettime`) **no** enlace: así se sabe que el SDK sigue protegiendo el piso de iOS 9.0. La misma prueba corre en CI (`.github/workflows/ios-toolchain.yml`) con la imagen de Docker y con el script directo, cada vez que cambia esta carpeta.

## Hola mundo (P1)

`hello/` es una app UIKit mínima, CK Hola, que muestra lo que reporta el iPad (versión de iOS, modelo, arquitectura, pantalla, núcleos y memoria), cuánto lleva abierta y cuántas veces se tocó la pantalla. Se instala en `/Applications` como un paquete `.deb` de Cydia, firmada con ldid.

```sh
docker run --rm -v "$PWD":/work -w /work/ios/toolchain/hello ck-ios-toolchain make package FINALPACKAGE=1
# deja ios/toolchain/hello/packages/com.thehuax.ckhola_0.1.0_iphoneos-arm.deb
```

El CI la compila en cada PR y deja el `.deb` en el artefacto `ipad-debs` (`ios-ci.yml`); `ios-toolchain.yml` también lo guarda como `ckhola-deb` cuando cambia esta carpeta.

Para instalarla en el iPad, sigue [Instalar en el iPad](#instalar-en-el-ipad).

Si el ícono aparece pero la app se cierra al abrirla, corre `ssh -o HostKeyAlgorithms=+ssh-rsa root@IP /Applications/CKHola.app/CKHola` y guarda lo que imprima; `Killed: 9` apunta a la firma. Para quitarla: `ssh -o HostKeyAlgorithms=+ssh-rsa root@IP "cd /tmp && dpkg -r com.thehuax.ckhola && su mobile -c uicache"`.

## Instalar en el iPad

`deploy.ps1` (Windows) y `deploy.sh` (Linux, también dentro de la imagen de Docker) instalan uno o varios `.deb` en el iPad por SSH: los copian, los instalan con `dpkg -i` y actualizan la pantalla de inicio con `uicache`. Antes:

1. Si el iPad se reinició, vuelve a correr Carbon en Safari y revisa que Cydia abra. Sin jailbreak activo no hay SSH.
2. Busca la IP del iPad en Ajustes → Wi-Fi → (i) junto a tu red.

Desde PowerShell en Windows, en la carpeta con los `.deb` (por ejemplo, el `ipad-debs` de un PR, ya descomprimido), con la ruta de tu copia del repo:

```powershell
powershell -ExecutionPolicy Bypass -File C:\ruta\al\repo\ios\toolchain\deploy.ps1 -Ip 192.168.0.123
```

Pide la contraseña de root dos veces, una para copiar y otra para instalar. Con `-SetupKey` crea una llave SSH en la PC si no hay (presiona Enter dos veces cuando pida la frase) y la agrega al iPad, y desde la siguiente vez ya no pide contraseña. `-Path` elige otros `.deb` o carpetas, y `-Port` y `-User` cambian el puerto y el usuario.

Desde Linux, en la raíz del repo:

```sh
ios/toolchain/deploy.sh 192.168.0.123                     # los .deb de build-ios-ci/debs (ci-ipad.sh)
ios/toolchain/deploy.sh 192.168.0.123 ~/Descargas/ipad-debs
```

Con la imagen: `docker run --rm -it --network host -v "$PWD":/work ck-ios-toolchain ios/toolchain/deploy.sh 192.168.0.123`.

Pide la contraseña una sola vez, porque todos los pasos van por la misma conexión, y no necesita `scp` ni `sftp` en el iPad. Para no escribirla, `ssh-copy-id -o HostKeyAlgorithms=+ssh-rsa root@192.168.0.123`. `CK_IPAD_PORT`, `CK_IPAD_USER` y `CK_SSH_OPTS` cambian el puerto, el usuario y las opciones de ssh.

A mano, lo mismo es:

```sh
scp -o HostKeyAlgorithms=+ssh-rsa paquete.deb root@IP:/tmp/
ssh -o HostKeyAlgorithms=+ssh-rsa root@IP "cd /tmp && dpkg -i /tmp/paquete.deb && su mobile -c uicache"
```

Detalles del iPad que los scripts ya resuelven:

- El OpenSSH de Cydia solo ofrece una llave de host `ssh-rsa`, que los clientes nuevos apagan: de ahí `-o HostKeyAlgorithms=+ssh-rsa`.
- `uicache` corre como `mobile` y desde `/tmp`. Desde `/var/root`, la carpeta de root, `mobile` no puede leer dónde está parado y la shell avisa `getcwd: cannot access parent directories`.

## Actualizar una versión

Cambia la URL o el commit en `versions.env` junto con su sha256 (`sha256sum archivo.tar.xz`), y corre la prueba de humo con Docker y sin Docker. Si cambia el SDK, revisa que `fix-sdk93.sh` siga aplicando: el script falla si no encuentra lo que espera.

## CI en cada PR

`.github/workflows/ios-ci.yml` corre en cada PR y en cada push a `main`, toque lo que toque:

- **`iPad build`** instala el toolchain con `install-theos.sh` y corre `ci-ipad.sh`, que:
  1. compila mbedTLS y Opus (`build-deps.sh`);
  2. revisa los contratos contra el SDK 9.3 para armv7: `ios/tests/contracts_check.c` en C, para que sus `_Static_assert` valgan con la estructura de 32 bits del iPad, y `ios/Bridge/CKSession.h` como Objective-C, con las mismas definiciones que el core;
  3. compila el core (`core-check.sh`);
  4. empaqueta como `.deb` cada proyecto de Theos que haya bajo `ios/`, es decir, cada carpeta con un `Makefile` y un archivo `control`. Hoy es solo CK Hola; la app y `ckctl` entran solos cuando tengan su `control`.

  Deja dos artefactos: **`ipad-debs`**, los `.deb` para instalar en el iPad, e **`ios-libs-armv7`**, las librerías estáticas para armv7 (core, nanopb, jerasure, gf-complete, mbedTLS y Opus) con los headers de mbedTLS y Opus y las licencias.
- **`Linux tests`** instala lo necesario con `apt-linux-tests.sh` y corre, cada una en su paso, las pruebas en C del port: los contratos, la entrada (A5: el parser del DualSense y el filtro de BTServer, con `ios/tests/input_tests.sh`, también en armv7 bajo `qemu-arm`), H.264 (A3, también en armv7 bajo `qemu-arm`) y el core (A2: OpenSSL, mbedTLS y mbedTLS en armv7 bajo `qemu-arm`, más las pruebas heredadas de VitaRPS5).

Lo mismo corre en un Linux sin CI: `THEOS=... ios/toolchain/ci-ipad.sh` deja todo en `build-ios-ci/`, y `sudo ios/toolchain/apt-linux-tests.sh` prepara un Ubuntu 24.04 para las pruebas.

Para bajar el `.deb` de un PR: en la pestaña **Checks** del PR, abre **iOS CI**, baja hasta **Artifacts** y descarga `ipad-debs`. Llega como `.zip` con el `.deb` adentro. GitHub pide haber iniciado sesión para descargarlo.

`ios-toolchain.yml` sigue revisando la imagen de Docker y el script de instalación cuando cambia esta carpeta.

## Licencias

Theos es GPLv3, el toolchain es LLVM (Apache 2.0 con excepción de LLVM) y cctools/ld64 (APSL 2.0), y ldid es AGPLv3. mbedTLS (Apache 2.0 o GPL 2.0 o posterior) y Opus (BSD de 3 cláusulas) se bajan de sus repos al compilar y sus licencias van con el artefacto `ios-deps-armv7`. El SDK está sujeto al acuerdo de Xcode y los SDK de Apple: se descarga al construir, nunca se sube al repo ni se publica como artefacto de CI.
