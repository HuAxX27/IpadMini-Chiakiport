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
| `core-check.sh`, `core-check/` | Compila el core completo para el iPad y lo enlaza en un ejecutable firmado |

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
- `find_library()` y `find_path()` buscan solo en el SDK y en `CK_IOS_PREFIX` (librerías ya compiladas para armv7, de A1.4), nunca en `/usr` del Linux. Los programas (`python3`, `protoc`) sí se buscan en el Linux.
- `ck_ios_sign(<target>)` firma un ejecutable con ldid al enlazarlo. iOS no corre nada sin firma, ni siquiera con jailbreak.
- Usa `llvm-ar` y el `ranlib` de cctools: el `ar` de cctools llama a un `ranlib` con prefijo que el toolchain no trae.

`core-check.sh` compila el core como lo va a usar el iPad (solo red local, mbedTLS, sin holepunch) y enlaza el archivo completo en `ck-core-check`, un ejecutable armv7 firmado. Mientras llegan A1.4 y los parches de A2, baja mbedTLS 2.28.0 con el `FetchContent` del `CMakeLists.txt` raíz y deja Opus apagado.

## Qué revisa la prueba de humo

`smoke-test.sh` compila `smoke/` con Theos y con CMake en un directorio temporal, sin dejar nada en el repo, y falla si alguno de los dos binarios:

- no es solo armv7,
- no trae `LC_VERSION_MIN_IPHONEOS` 9.0 con SDK 9.3,
- no está firmado con ldid,
- o no enlaza libSystem.

Después revisa que una API de iOS 10 (`clock_gettime`) **no** enlace: así se sabe que el SDK sigue protegiendo el piso de iOS 9.0. La misma prueba corre en CI (`.github/workflows/ios-toolchain.yml`) con la imagen de Docker y con el script directo, cada vez que cambia esta carpeta.

## Actualizar una versión

Cambia la URL o el commit en `versions.env` junto con su sha256 (`sha256sum archivo.tar.xz`), y corre la prueba de humo con Docker y sin Docker. Si cambia el SDK, revisa que `fix-sdk93.sh` siga aplicando: el script falla si no encuentra lo que espera.

## Licencias

Theos es GPLv3, el toolchain es LLVM (Apache 2.0 con excepción de LLVM) y cctools/ld64 (APSL 2.0), y ldid es AGPLv3. El SDK está sujeto al acuerdo de Xcode y los SDK de Apple: se descarga al construir, nunca se sube al repo ni se publica como artefacto de CI.
