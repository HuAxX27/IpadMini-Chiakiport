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
| `smoke-test.sh`, `smoke/` | Prueba de humo: compila un binario y revisa que sirva en el iPad |
| `hello/` | Hola mundo UIKit (CK Hola), empaquetado como `.deb`: la prueba P1 en el iPad |

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

## Qué revisa la prueba de humo

`smoke-test.sh` compila `smoke/` con Theos en un directorio temporal, sin dejar nada en el repo, y falla si el binario:

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

El CI (`ios-toolchain.yml`) también la compila y guarda el `.deb` como artefacto `ckhola-deb`.

Para instalarla en el iPad, con jailbreak y OpenSSH (desde PowerShell en Windows o una terminal en Linux, en la carpeta del `.deb`):

1. Si el iPad se reinició, vuelve a correr Carbon en Safari y revisa que Cydia abra.
2. Busca la IP del iPad en Ajustes → Wi-Fi → (i) junto a tu red.
3. Copia e instala, cambiando `IP` por la del iPad:

   ```sh
   scp -o HostKeyAlgorithms=+ssh-rsa com.thehuax.ckhola_0.1.0_iphoneos-arm.deb root@IP:/tmp/
   ssh -o HostKeyAlgorithms=+ssh-rsa root@IP "dpkg -i /tmp/com.thehuax.ckhola_0.1.0_iphoneos-arm.deb && su mobile -c uicache"
   ```

4. Abre CK Hola desde la pantalla de inicio.

Si el ícono aparece pero la app se cierra al abrirla, corre `ssh -o HostKeyAlgorithms=+ssh-rsa root@IP /Applications/CKHola.app/CKHola` y guarda lo que imprima; `Killed: 9` apunta a la firma. Para quitarla: `dpkg -r com.thehuax.ckhola && su mobile -c uicache`.

## Actualizar una versión

Cambia la URL o el commit en `versions.env` junto con su sha256 (`sha256sum archivo.tar.xz`), y corre la prueba de humo con Docker y sin Docker. Si cambia el SDK, revisa que `fix-sdk93.sh` siga aplicando: el script falla si no encuentra lo que espera.

## Licencias

Theos es GPLv3, el toolchain es LLVM (Apache 2.0 con excepción de LLVM) y cctools/ld64 (APSL 2.0), y ldid es AGPLv3. El SDK está sujeto al acuerdo de Xcode y los SDK de Apple: se descarga al construir, nunca se sube al repo ni se publica como artefacto de CI.
