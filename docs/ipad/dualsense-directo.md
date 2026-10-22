# DualSense directo al iPad (A5.7)

Investigación del issue #39: conectar el DualSense al iPad mini 1 con jailbreak, sin el puente ESP32. Desde el 2026-10-22 es el camino principal para el control: el puente ESP32 queda en pausa.

Estado al 2026-10-22: **el DualSense se lee en el iPad por Bluetooth nativo**, con todos sus botones, los dos sticks y los gatillos (T8). Con el filtro de `BTServer` (`ckbtfilter`) se empareja desde Ajustes, queda "Conectado" y se reconecta solo (T5 y T6). iOS no lo publica como `IOHIDDevice` (T7). Por eso el filtro copia sus reportes a un archivo compartido, y de ahí los lee `ckhidprobe -s`; lo mismo hará la app (A5.8, #73). Falta el reporte completo (touchpad y sensores de movimiento), que pide inyectar un `GET_REPORT`. Resultados abajo, en "Resultados en el iPad".

## Resumen

- Hay tres caminos. El recomendado es **Bluetooth nativo**: un filtro pequeño dentro de `BTServer` (el demonio de Bluetooth de Apple) para que acepte emparejar el DualSense, y la app lee sus reportes con `IOHIDManager`. MFiWrapper (2014, GPLv3) ya lo hizo con el DualShock 4 en iOS 7.
- El plan B es **USB con el adaptador de cámara Lightning**. Es lo más barato de probar, pero el DualSense pide 500 mA y el adaptador da mucho menos, así que probablemente necesita un hub USB con su propia corriente.
- **BTstack** queda al final: su port de iOS está archivado, apaga el Bluetooth de iOS y su licencia no deja meterlo en una app AGPLv3.
- Ya está listo, y probado en Linux, el parser que convierte los reportes del DualSense (USB y Bluetooth) en `ChiakiControllerState`: `ios/Input/ck_ds5.h`. Sirve para cualquiera de los tres caminos.
- T1 confirmó que iOS 9.3.5 usa `BTServer`, y T2 que el DualSense no aparece en Ajustes sin ayuda, como se esperaba. El USB no se ha probado porque no hay adaptador de cámara.
- Con `ckbtfilter`, el DualSense se empareja por Bluetooth nativo y abre sus dos canales HID, pero `BTServer` no lo publica para otros programas (T5 a T7).
- Los reportes sí llegan a `BTServer`: el filtro los copia a `/tmp/ckds5.report` y `ckhidprobe -s` los lee con cada botón, stick y gatillo bien (T8). Va en el reporte simple, sin touchpad ni movimiento.
- Siguiente paso: A5.8 (#73), un `CKInputSource` que lee ese archivo. Aparte, ver si se puede pasar al reporte completo inyectando un `GET_REPORT` del `0x05` desde el filtro.

## El iPad de prueba

| Dato | Valor |
|---|---|
| Modelo | iPad mini 1 Wi‑Fi, MD531LL/A (A1432, `iPad2,5`, P105AP) |
| Sistema | iOS 9.3.5 (13G36) |
| Jailbreak | Carbon (semi-untethered), con Cydia. Phœnix ya no firma |
| Wi‑Fi y Bluetooth | Un solo chip Broadcom BCM4334: Bluetooth 4.0 y Wi‑Fi de 2.4 y 5 GHz |

El chip comparte la radio de 2.4 GHz entre Wi‑Fi y Bluetooth. Con un control Bluetooth conectado, el streaming debe ir por Wi‑Fi de **5 GHz** para que no compitan.

## Resultados en el iPad

| Prueba | Resultado (2026-10-21 y 2026-10-22) |
|---|---|
| T1. Demonio de Bluetooth | Kernel `xnu-3248.61.1~1/RELEASE_ARM_S5L8942X`, `iPad2,5`, P105AP. Corren `/usr/sbin/BTServer` y `/usr/sbin/BlueTool -R`. LaunchDaemons: `com.apple.BTServer.plist`, `.avrcp`, `.le`, `.map`, `.pbap` y `com.apple.BlueTool.plist`. El camino 1 sigue abierto |
| T2. Emparejar sin tweak | El DualSense no aparece en Ajustes > Bluetooth, con el control en modo de emparejar (luz azul parpadeando rápido) y la PS5 apagada. Es lo esperado: iOS 9 esconde los periféricos con clase de gamepad |
| T3 y T4. USB | Sin probar: no hay adaptador de cámara Lightning |
| T5. Instalar el filtro | `ckbtfilter` carga en `BTServer` (corre como `mobile`, uid 501) y encuentra el socket HCI: `connect fd 7 to kernel control 16 unit 0 (com.apple.uart.bluetooth)`. `BTServer` usa `read` y `write` en ese socket, porque el filtro ve todo el tráfico |
| T6. Emparejar | Funciona al primer intento. La búsqueda trae al pad con clase `0x002508`, el filtro la cambia a teclado y `BTServer` se conecta. Hace Secure Simple Pairing sin PIN (iPad con E/S 1 y pad con E/S 3, "Just Works"), recibe la llave de enlace (tipo 4), cifra el enlace y abre los canales HID de control (PSM `0x11`) y datos (PSM `0x13`). Ajustes muestra "DualSense Wireless Controller", "Conectado". Al apagar y prender el pad, este vuelve solo: el filtro también cambia la clase en su solicitud de conexión, `BTServer` la acepta con la llave guardada y el pad abre sus dos canales |
| T7. `ckhidprobe` | `IOHIDManagerOpen` responde bien, pero no aparece ningún dispositivo HID, ni con el pad conectado. `BTServer` no publica el pad como `IOHIDDevice` |
| T8. Paquete 0.1.1 | Llegan reportes simples `0x01` de 10 bytes por el canal de datos: el filtro contó de 386 a 520 cada 10 segundos con botones en uso. `BTServer` no le manda nada al pad por los canales HID, ni `GET_REPORT` ni `SET_PROTOCOL`; solo abre y cierra SDP. En el IORegistry no hay ningún `IOHIDDevice` ni `IOHIDUserDevice`: solo `IOHIDResource` con un `IOHIDResourceDeviceUserClient` y 75 `IOHIDEventServiceUserClient`. `ckhidprobe -s` lee del archivo compartido cada botón probado con su nombre correcto (cruz, círculo, cuadrado, triángulo, cruceta, L1, R1, Options, Create, PS y el clic del touchpad; L3 y R3 no se probaron), los sticks con arriba e izquierda negativos y los gatillos de 0 a 255. En reposo, los sticks marcan unos cientos de unidades (de 32768) fuera del centro. `ckhidprobe -s` leyó unos 10 reportes por segundo; la versión siguiente cuenta también los que escribió el filtro, para saber si el pad manda pocos cuando nada cambia o si el lector pierde reportes |

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

Para nosotros son tres piezas, todas de A5:

- `ck_hci_filter` (`ios/Input/ck_hci_filter.h`): el filtro en C portable, probado en Linux. Sigue el flujo H4 entre `BTServer` y el chip, incluso cuando un paquete llega partido en dos lecturas, y cambia la clase "gamepad" o "joystick" por "teclado" en los resultados de búsqueda (los tres tipos de evento) y en las solicitudes de conexión, que es lo que manda el control cuando vuelve a conectarse solo. Es una implementación nueva; de MFiWrapper (GPLv3 o posterior, que se puede combinar con AGPLv3) se toma el método y se le da el crédito.
- `ckbtfilter` (`ios/Input/ckdirect/btfilter`): el tweak de Substrate que se carga solo en `BTServer`. Encuentra el socket HCI por su nombre de control del kernel, `com.apple.uart.bluetooth` (o un nodo `/dev` del UART del chip, si `BTServer` lo abriera así), le pasa al filtro todo lo que lee y escribe, y anota en `/tmp/ckbtfilter.log` la búsqueda, el emparejamiento y las conexiones. No anota llaves de enlace ni PIN.
- `CKDS5Source` (A5.8, issue #73): un `CKInputSource` (C4) que usa `IOHIDManager`, pide el `0x05`, pasa cada reporte por `ck_ds5_parse_input_report()` bajo un candado y entrega la copia en `poll`. Nada de reservas por reporte. Mientras tanto, `ckhidprobe` (`ios/Input/ckdirect/hidprobe`) prueba lo mismo desde la línea de comandos.

Como `BTServer` no publica el pad (T7), hay un plan B dentro del camino 1, en la versión 0.1.1. El filtro ya ve cada paquete que el pad le manda a `BTServer`. Con eso sigue los canales HID del pad y copia cada reporte de entrada a un archivo compartido, `/tmp/ckds5.report`, protegido con un candado de secuencia (`ios/Input/ck_ds5_shared.h`). Otro proceso lo mapea y lee el último reporte cuando quiera, sin hacer esperar a `BTServer`. Eso encaja con el `poll` cada 8 ms de C4: `CKDS5Source` leería ese archivo en vez de usar `IOHIDManager`. Solo se registra y comparte el tráfico de los dispositivos cuya clase cambió el filtro, nunca el de un teclado de verdad. Lo que falta en ese camino es pedir el `0x05` para pasar al reporte completo: habría que inyectar un `GET_REPORT` en el canal de control, y eso se decide después de T8.

A favor: el Bluetooth de iOS sigue encendido, se empareja desde Ajustes, no hay hardware extra ni problema de licencia, y es poco código.

Lo que falta saber, y cómo se prueba:

| Duda | Cómo se sabe |
|---|---|
| ¿iOS 9.3.5 aún usa `BTServer`? | Sí (T1) |
| ¿Cómo rechaza hoy iOS al DualSense? | No lo muestra en la lista (T2) |
| ¿`BTServer` habla con el chip por el socket `com.apple.uart.bluetooth`, con `read` y `write`? | Sí (T5) |
| ¿Empareja el DualSense con el filtro? | Sí, al primer intento, y se reconecta solo (T6) |
| ¿`BTServer` publica el pad como `IOHIDDevice`? | No, ni de otra forma en el IORegistry (T7 y T8) |
| ¿Llegan los reportes del pad a `BTServer`? ¿`BTServer` le manda algo, como un `GET_REPORT`? | Sí llegan, en reporte simple. `BTServer` no le manda nada (T8) |
| ¿Se pueden leer los reportes desde el archivo compartido? | Sí, con todos los botones, sticks y gatillos (T8) |
| ¿Se puede pasar al reporte completo inyectando un `GET_REPORT` del `0x05` en el canal de control? Hay que cuidar la cuenta de paquetes de `BTServer` con el chip | Pendiente |
| ¿La app puede abrir el `IOHIDDevice`? Instalada en `/Applications` no tiene sandbox; si no basta, un demonio pequeño como root | A5.8 |
| ¿iOS esconde el teclado en pantalla al creer que hay un teclado? La clase falsa solo se usa al emparejar; el descriptor HID dice GamePad | T6, tocando un campo de texto |

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
- `ios/Input/tools/ds5_hidraw.c`: lee un DualSense real por `hidraw` en Linux y muestra lo que entiende el parser. Imprime con `ds5_print.c`, el mismo código que usa `ckhidprobe` en el iPad.
- `ios/Input/ck_hci_filter.h` y `ck_hci_filter.c`: el filtro HCI, sin E/S ni reservas de memoria; sus pruebas en `ios/tests/hci_filter_test.c`.
- `ios/Input/ck_iohid.h`: las declaraciones de `IOHIDManager` y `IOHIDDevice`, que el SDK de iOS 9.3 exporta pero no trae en sus headers.
- `ios/Input/ck_ds5_shared.h` y `ck_ds5_shared.c`: el reporte compartido entre procesos, con su candado de secuencia; sus pruebas en `ios/tests/ds5_shared_test.c`, con un hilo que escribe y otro que lee al mismo tiempo.
- `ios/Input/ckdirect/`: el paquete `.deb` con `ckbtfilter` y `ckhidprobe`, para Theos (armv7, iOS 9.0).
- `ios/tests/ckbtfilter_test.c`: compila el tweak en Linux, con un sustituto de Substrate en `ios/tests/fakes/`, le pasa el tráfico de un emparejamiento y una reconexión como los de T6, y revisa el registro y los reportes compartidos.
- `ios/tests/input_tests.sh`: corre las pruebas de A5 en Linux, también para armv7 con qemu, y arma el paquete si `THEOS` está definido.

## Pruebas

### En CachyOS, opcional (10 minutos)

Valida el parser con tu DualSense antes de que el iPad pueda correr nada. Desde la raíz del repo:

```sh
cc -std=c11 -O2 -Ilib/include -Iios/Input -o ds5_hidraw ios/Input/tools/ds5_hidraw.c ios/Input/tools/ds5_print.c ios/Input/ck_ds5.c lib/src/controller.c
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

### En el iPad, con el paquete `ckdirect`

El paquete se arma con `make package FINALPACKAGE=1` en `ios/Input/ckdirect` (con Theos de `ios/toolchain`) y queda en `ios/Input/ckdirect/packages/`. Instala dos cosas: `ckbtfilter` en `/Library/MobileSubstrate/DynamicLibraries/` y `ckhidprobe` en `/usr/bin/`.

Antes de empezar: si el iPad se reinició, vuelve a correr Carbon. Sin eso, Substrate no carga el filtro.

**T5. Instalar y revisar que el filtro carga.** Desde la PC, en la carpeta donde está el `.deb` (cambia `IP` por la del iPad):

```sh
scp -o HostKeyAlgorithms=+ssh-rsa com.thehuax.ckdirect_0.1.1_iphoneos-arm.deb root@IP:/tmp/
ssh -o HostKeyAlgorithms=+ssh-rsa root@IP
```

Si `scp` falla con "subsystem request failed", agrega `-O` después de `scp`. Ya dentro del iPad:

```sh
dpkg -i /tmp/com.thehuax.ckdirect_0.1.1_iphoneos-arm.deb
killall BTServer
cat /tmp/ckbtfilter.log
```

`launchd` vuelve a abrir `BTServer` solo, ya con el filtro; si el `cat` sale vacío, espera unos segundos y repítelo. El iPad de prueba no trae `tail` ni `sleep`, por eso los pasos no los usan. Qué reportar: la salida de `dpkg` y del `cat`. Lo bueno es ver `ckbtfilter loaded into pid ...` y una línea `connect fd ... (com.apple.uart.bluetooth)`. Si el archivo no existe, el filtro anota en el syslog: instala "syslogd to /var/log/syslog" desde Cydia y corre `grep ckbtfilter /var/log/syslog`. En el iPad de prueba ese paquete no dejó `/var/log/syslog`, pero el archivo de registro sí se crea.

**T6. Emparejar con el filtro.** En Ajustes > Bluetooth, pon el DualSense en modo de emparejar (Create y PS hasta que la luz azul parpadee rápido). Reporta si aparece, con qué nombre, y qué pasa al tocarlo. Si falla, inténtalo dos o tres veces. Luego, por SSH, pega `cat /tmp/ckbtfilter.log` completo. Si se conecta, abre Notas, toca para escribir y di si sale el teclado en pantalla.

**T7. Leer el control.** Con el DualSense conectado, por SSH:

```sh
ckhidprobe
```

Presiona cada botón, mueve los sticks a los cuatro lados, aprieta los gatillos y toca el touchpad; termina con Ctrl-C. Qué reportar: la salida completa, sobre todo las líneas `device:`, `open:` y `feature report 0x05:`, y si los botones salen con su nombre. Corre también `ckhidprobe -x` unos segundos y pega dos o tres líneas de bytes. `ckhidprobe` sirve igual por USB, si algún día hay adaptador.

**T8. Dónde quedan los reportes (paquete 0.1.1).** Instala el paquete nuevo como en T5. El `killall BTServer` desconecta el pad: vuelve a conectarlo con el botón PS. Después, por SSH:

```sh
cat /tmp/ckbtfilter.log
ckhidprobe -r
ckhidprobe -s
```

- En el registro, lo nuevo son las líneas `hid ...`: si llegan reportes del pad (`hid input report 0x01 ...` y un resumen cada 10 segundos) y si `BTServer` le manda algo (`hid to ...`).
- `ckhidprobe -r` lista los servicios HID y Bluetooth del IORegistry de iOS, para ver si el pad está publicado de alguna otra forma, y luego sigue como `ckhidprobe`. Termina con Ctrl-C.
- `ckhidprobe -s` lee el archivo que comparte el filtro. Presiona cada botón, mueve los sticks y aprieta los gatillos, y termina con Ctrl-C.

Qué reportar: las tres salidas.

**Para quitarlo.** `dpkg -r com.thehuax.ckdirect` y luego `killall BTServer`. Reiniciar el iPad sin volver a correr Carbon también lo apaga, porque Substrate no carga nada. Para olvidar el control: Ajustes > Bluetooth, la (i) junto al DualSense, "Omitir este dispositivo".

## Cómo se decide

- Bluetooth nativo con reporte completo: es el camino. El puente ESP32 queda opcional.
- Si iOS nunca publica el pad, la app lee el archivo que comparte `ckbtfilter`. Funciona igual con el reporte simple, y el completo necesita inyectar un `GET_REPORT`. **Es lo que pasó (T8): Bluetooth nativo con reporte simple, leído desde el archivo compartido.** El touchpad se cubre por ahora con los controles táctiles (A5.5).
- Bluetooth nativo solo con reporte simple: también sirve; el touchpad se cubre con los controles táctiles (A5.5).
- Bluetooth falla y USB funciona: USB con hub es la conexión directa.
- Fallan los dos: se evalúa BTstack o se retoma el puente ESP32.

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
