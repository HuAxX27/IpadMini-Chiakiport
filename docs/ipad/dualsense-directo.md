# DualSense directo al iPad (A5.7)

Investigación del issue #39: conectar el DualSense al iPad mini 1 con jailbreak, sin el puente ESP32. Estado al 2026-10-21: terminada la investigación de escritorio y el parser de reportes; faltan las pruebas en el iPad.

## Resumen

- Hay tres caminos. El recomendado es **Bluetooth nativo**: un filtro pequeño dentro de `BTServer` (el demonio de Bluetooth de Apple) para que acepte emparejar el DualSense, y la app lee sus reportes con `IOHIDManager`. MFiWrapper (2014, GPLv3) ya lo hizo con el DualShock 4 en iOS 7.
- El plan B es **USB con el adaptador de cámara Lightning**. Es lo más barato de probar, pero el DualSense pide 500 mA y el adaptador da mucho menos, así que probablemente necesita un hub USB con su propia corriente.
- **BTstack** queda al final: su port de iOS está archivado, apaga el Bluetooth de iOS y su licencia no deja meterlo en una app AGPLv3.
- Ya está listo, y probado en Linux, el parser que convierte los reportes del DualSense (USB y Bluetooth) en `ChiakiControllerState`: `ios/Input/ck_ds5.h`. Sirve para cualquiera de los tres caminos.
- Siguiente paso: cuatro pruebas en el iPad que no necesitan código nuestro (abajo, en "Pruebas"). Con sus resultados se escribe la sonda `ckhidprobe` cuando A1.2 tenga el toolchain.

## El iPad de prueba

| Dato | Valor |
|---|---|
| Modelo | iPad mini 1 Wi‑Fi, MD531LL/A (A1432, `iPad2,5`, P105AP) |
| Sistema | iOS 9.3.5 (13G36) |
| Jailbreak | Carbon (semi-untethered), con Cydia. Phœnix ya no firma |
| Wi‑Fi y Bluetooth | Un solo chip Broadcom BCM4334: Bluetooth 4.0 y Wi‑Fi de 2.4 y 5 GHz |

El chip comparte la radio de 2.4 GHz entre Wi‑Fi y Bluetooth. Con un control Bluetooth conectado, el streaming debe ir por Wi‑Fi de **5 GHz** para que no compitan.

## Cómo habla el DualSense

| | USB | Bluetooth, al conectar | Bluetooth, modo completo |
|---|---|---|---|
| Reporte de entrada | `0x01`, 64 bytes | `0x01`, 10 bytes | `0x31`, 78 bytes |
| Botones, sticks y gatillos | Sí | Sí | Sí |
| Touchpad y sensores de movimiento | Sí | No | Sí |
| Verificación | Ninguna | Ninguna | CRC-32 al final, que también cubre el byte `0xa1` de HIDP |
| Para llegar ahí | Nada | Nada | Leer una vez el reporte de funciones `0x05` (calibración) |

- USB: el DualSense es un dispositivo compuesto, con audio (bocina, micrófono y vibración háptica) y HID. Declara 500 mA de consumo (`bMaxPower = 0xFA`).
- Bluetooth: es Bluetooth clásico (BR/EDR) con el perfil HID, no Bluetooth LE. Se empareja con Secure Simple Pairing, sin PIN. Modo de emparejar: mantener Create y PS hasta que la luz azul parpadee rápido.
- Los vendor/product ID son `054c:0ce6` (DualSense) y `054c:0df2` (DualSense Edge).

`ck_ds5_parse_input_report()` entiende los tres reportes, revisa el CRC y descarta lo que no reconoce sin tocar el estado. Toma las posiciones de SDL (zlib) y Bluepad32 (Apache 2.0), sin copiar código. El movimiento (giroscopio y acelerómetro) queda para después: necesita la calibración del reporte `0x05` y no hace falta para jugar.

## Camino 1: Bluetooth nativo con un filtro en BTServer (recomendado)

iOS 9 ya trae un host HID en `BTServer`: así funcionan los teclados Bluetooth y los controles MFi. A cualquier otro periférico lo rechaza con "dispositivo no compatible". MFiWrapper resolvió eso con un tweak de Substrate que se carga solo en `BTServer` y engancha `read` y `write` del socket HCI:

1. Cuando una búsqueda encuentra un periférico, le cambia la clase de dispositivo a "teclado". Con eso `BTServer` acepta emparejarlo desde Ajustes.
2. Ya conectado, `BTServer` publica el control como un `IOHIDDevice` con uso GamePad, igual que un teclado.
3. La app lo abre con `IOHIDManager` (filtrando por `054c:0ce6`), recibe los reportes crudos con `IOHIDDeviceRegisterInputReportCallback` y puede pedir reportes de funciones con `IOHIDDeviceGetReport`. MFiWrapper pedía el `0x02` del DualShock 4 para pasarlo a modo completo; con el DualSense sería el `0x05`.

Para nosotros serían dos piezas, las dos de A5:

- `ckbtfilter`: el tweak para `BTServer`, unas 150 líneas adaptadas de MFiWrapper. GPLv3 o posterior se puede combinar con AGPLv3, con su aviso de copyright.
- `CKDS5Source`: un `CKInputSource` (C4) que usa `IOHIDManager`, pide el `0x05`, pasa cada reporte por `ck_ds5_parse_input_report()` bajo un candado y entrega la copia en `poll`. Nada de reservas por reporte.

A favor: el Bluetooth de iOS sigue encendido, se empareja desde Ajustes, no hay hardware extra ni problema de licencia, y es poco código.

Lo que falta saber, y cómo se prueba:

| Duda | Cómo se sabe |
|---|---|
| ¿iOS 9.3.5 aún usa `BTServer` con el mismo socket HCI? BTstack 1.1 (2015) usaba el mismo socket en iOS 9, buena señal | Prueba T1 |
| ¿Cómo rechaza hoy iOS al DualSense? | Prueba T2 |
| ¿Empareja el DualSense con el filtro? MFiWrapper avisa que a veces hay que intentarlo varias veces | `ckbtfilter` |
| ¿`BTServer` deja pasar la lectura del `0x05`? Si no, se queda en el reporte simple: se puede jugar, sin touchpad ni movimiento | `ckhidprobe` |
| ¿La app puede abrir el `IOHIDDevice`? Instalada en `/Applications` no tiene sandbox; si no basta, un demonio pequeño como root | `ckhidprobe` desde la app |
| ¿iOS esconde el teclado en pantalla al creer que hay un teclado? La clase falsa solo se usa al buscar; el descriptor HID dice GamePad | Con el control conectado, tocar un campo de texto |

Con jailbreak semi-untethered, tras reiniciar el iPad el filtro solo se carga después de volver a correr Carbon.

## Camino 2: USB con el adaptador de cámara Lightning (plan B)

Con el adaptador de cámara Lightning a USB, iOS 9 acepta teclados USB, así que el kernel trae un driver HID de clase. Si publica el DualSense como `IOHIDDevice`, el mismo código de `IOHIDManager` del camino 1 lo lee, y por USB no hace falta pedir el `0x05`.

Riesgos:

- **Corriente.** El DualSense declara 500 mA y el adaptador da alrededor de 100 mA según reportes de usuarios. Lo esperable es el mensaje "Este accesorio requiere demasiada energía". La salida conocida es un hub USB con su propia fuente entre el adaptador y el control. El adaptador "Lightning a USB 3" trae un puerto Lightning para cargar, pero no confirmé que funcione con el iPad mini 1.
- **Audio.** iOS 9 usa dispositivos de audio USB estándar, así que puede mandar el sonido del iPad a la bocina del DualSense.
- **El puerto.** El iPad no se carga mientras juega, salvo con el adaptador USB 3.

A favor: quizá no necesita tweak, la latencia es la más baja y el control se carga.

## Camino 3: BTstack (último recurso)

El paquete "BTstack" de Cydia (1.1-1, noviembre de 2015) trae `BTdaemon`, que descarga `BTServer`, toma el chip y atiende a las apps por el socket `/tmp/BTstack`.

En contra:

- BlueKitchen archivó el port de iOS: "BTstack on iOS is not supported anymore".
- En iOS 9 hubo reportes mixtos. El problema conocido (en 9.1) era el sandbox de las apps hechas con Xcode, y se resolvía instalándolas en `/Applications`.
- Apaga el Bluetooth de iOS mientras está activo.
- Tendríamos que escribir el host HID nosotros: emparejamiento, SDP y canales L2CAP `0x11` y `0x13`.
- **Licencia:** la cláusula 4 de BTstack prohíbe el uso comercial. Es una restricción que la AGPLv3 no permite, así que no se puede enlazar `libBTstack` en la app ni distribuirla con ella. Solo quedaría hablar con el demonio que el usuario instala desde Cydia, lo cual es una zona gris.

## Antecedentes

| Proyecto | Cómo funciona | ¿Sirve de base? |
|---|---|---|
| MFiWrapper (meancoot, 2014, GPLv3) | Filtro en `BTServer` y `IOHIDManager`; DualShock 4 y controles de Wii en iOS 7 | Sí: es abierto y muestra el método completo |
| Controllers for All (cerrado, de pago) | Su propia pila Bluetooth: pide apagar el Bluetooth de iOS. DualShock 3 y 4, hasta iOS 9.3 | Solo prueba que se puede |
| nControl (Kevin Bradley, cerrado, de pago) | DualShock 4, Xbox y Joy-Con en iOS 7 a 12, emparejando desde su propia lista | Solo prueba que se puede |
| BTstack para iOS (BlueKitchen) | Pila Bluetooth en espacio de usuario que reemplaza a `BTServer` | Ver camino 3 |

Ninguno soporta el DualSense, y en todos el control queda a cargo de otra app o de los juegos con soporte MFi. Para nosotros eso no aplica: la misma app de Remote Play lee el control.

## Comparación

| | Bluetooth nativo | USB | BTstack |
|---|---|---|---|
| Bluetooth de iOS | Sigue encendido | Sigue encendido | Apagado |
| Hardware extra | Nada | Adaptador y quizá un hub con corriente | Nada |
| Tweak | Sí, en `BTServer` | Quizá ninguno | Paquete de Cydia sin mantenimiento |
| Licencia | Sin problema | Sin problema | Incompatible para enlazar |
| Touchpad | Si pasa el `0x05` | Sí | Sí |
| Esfuerzo | Medio | Bajo | Alto |

## Lo que ya está en el repo

- `ios/Input/ck_ds5.h` y `ck_ds5.c`: el parser, en C portable, sin E/S ni reservas de memoria. Compila para armv7 con iOS 9 como destino.
- `ios/tests/ds5_test.c`: sus pruebas en Linux. El reporte Bluetooth de prueba lleva un CRC calculado aparte con `zlib.crc32` de Python.
- `ios/Input/tools/ds5_hidraw.c`: lee un DualSense real por `hidraw` en Linux y muestra lo que entiende el parser.

## Pruebas

### En CachyOS, opcional (10 minutos)

Valida el parser con tu DualSense antes de que el iPad pueda correr nada. Desde la raíz del repo:

```sh
cc -std=c11 -O2 -Ilib/include -Iios/Input -o ds5_hidraw ios/Input/tools/ds5_hidraw.c ios/Input/ck_ds5.c lib/src/controller.c
sudo ./ds5_hidraw -x
```

Hazlo dos veces: con el DualSense por cable USB y luego emparejado por Bluetooth. En cada una presiona cada botón, mueve los sticks a los cuatro lados, aprieta los gatillos y toca el touchpad.

Qué reportar: si cada botón sale con su nombre correcto; si el stick hacia arriba y hacia la izquierda da números negativos; la línea `reports/s`; y si aparece alguna línea `bad-crc` o `unknown`. Pega también dos o tres líneas de bytes (las que empiezan con un número y `bytes:`).

### En el iPad, sin código nuestro

Todas por SSH como root (la contraseña por defecto es `alpine`; cámbiala con `passwd`), salvo donde dice Ajustes.

**T1. Qué demonio maneja el Bluetooth**

```sh
uname -a
ps ax | grep -i -E 'btserver|bluetoothd|bluetool' | grep -v grep
ls /System/Library/LaunchDaemons | grep -i -E 'bt|blue'
```

Si `ps` no existe, instala "adv-cmds" desde Cydia. Pega la salida completa.

**T2. Emparejar sin tweak.** En Ajustes > Bluetooth, con el Bluetooth encendido, pon el DualSense en modo de emparejar. Reporta si aparece en la lista, con qué nombre, y qué pasa al tocarlo (una captura del mensaje sirve).

**T3. USB directo.** Si tienes el adaptador de cámara Lightning a USB, conecta el DualSense con su cable USB. Reporta qué mensaje sale en el iPad y si la luz del control se prende.

**T4. USB con hub con corriente.** Solo si tienes un hub USB con su propia fuente: adaptador, hub, DualSense. Reporta lo mismo que en T3. Además, reproduce un video y di si el sonido sale del iPad o del control.

### Después, cuando A1.2 tenga el toolchain

1. `ckhidprobe`: herramienta de línea de comandos que lista los `IOHIDDevice` y lee el DualSense con `IOHIDManager` y `ck_ds5`. Si T3 o T4 muestran el control, se prueba primero por USB, sin tweak.
2. `ckbtfilter`: el filtro para `BTServer`. Con él se repite T2 y luego `ckhidprobe` por Bluetooth.

## Cómo se decide

- Bluetooth nativo con reporte completo: es el camino. El puente ESP32 queda opcional.
- Bluetooth nativo solo con reporte simple: también sirve; el touchpad se cubre con los controles táctiles (A5.5).
- Bluetooth falla y USB funciona: USB con hub es la conexión directa.
- Fallan los dos: se evalúa BTstack o se queda el puente ESP32.

## Fuentes

- MFiWrapper: https://github.com/meancoot/MFiWrapper (rama `native_bt`: `btserverfilter/src/BTServerFilter.cpp`, `src/backend/hidmanager/HIDManager.cpp`, `src/backend/hidpad/HIDPad_DualShock4.cpp`)
- BTstack, ports archivados: https://github.com/bluekitchen/btstack/blob/master/port/archive/README.md ; port de iOS en la etiqueta `v1.1`: https://github.com/bluekitchen/btstack/tree/v1.1/port/ios
- BTstack en iOS 9.1 (sandbox): https://groups.google.com/g/btstack-dev/c/_vGgS6RHsPA
- Paquete BTstack en BigBoss: https://www.ios-repo-updates.com/repository/bigboss/package/ch.ringwald.btstack/
- nControl: https://www.idownloadblog.com/2019/03/18/ncontrol/
- Controllers for All con DualShock 4: https://www.idownloadblog.com/2014/01/25/ps4-dual-shock-4-ios-7-controllers-for-all-mfi/
- DualShock 4 con BTstack en iOS 9: https://forums.libretro.com/t/how-to-connect-ps4-dualshock4-to-retroarch1-2-2-with-btstack-ios9-0-2-jb/3484
- Descriptores USB y reportes Bluetooth del DualSense: https://dsremap.readthedocs.io/en/latest/reverse.html
- SDL, `SDL_hidapi_ps5.c`: https://github.com/libsdl-org/SDL/blob/release-2.30.x/src/joystick/hidapi/SDL_hidapi_ps5.c
- Bluepad32, `uni_hid_parser_ds5.c`: https://github.com/ricardoquesada/bluepad32/blob/main/src/components/bluepad32/parser/uni_hid_parser_ds5.c
- Hardware del iPad mini 1: https://theapplewiki.com/wiki/iPad2,5
- "Demasiada energía" con el adaptador de cámara: https://pocketables.com/2013/02/how-to-get-around-the-too-much-power-issue-when-using-the-ipad-usb-camera-adapter.html
