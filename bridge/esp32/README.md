# Puente ESP32 para el DualSense

Firmware para un ESP32 clásico con [Bluepad32](https://bluepad32.readthedocs.io): se empareja con el DualSense por Bluetooth y manda su estado al iPad por Wi-Fi, en UDP, con el paquete del contrato C4 (`bridge/protocol/ck_pad_packet.h`, puerto 9400).

```
DualSense --Bluetooth--> ESP32 --Wi-Fi, UDP 9400--> iPad (o la PC para probar)
```

## Qué placa sirve

- **Sí:** cualquier placa con ESP32 clásico, por ejemplo ESP32-DevKitC, "ESP32 DEVKIT V1" o NodeMCU-32S, con el módulo ESP32-WROOM-32 y 4 MB de flash. Hace falta un cable USB que pase datos, no solo carga.
- **No:** ESP32-S2, S3, C3, C6 ni H2. No tienen Bluetooth clásico y el DualSense lo usa.
- El Pico W también lo soporta Bluepad32, pero este firmware solo está hecho para el ESP32.
- El ESP32 solo usa Wi-Fi de 2.4 GHz. El iPad puede estar en la red de 5 GHz del mismo router: lo que importa es que estén en la misma red local.

## 1. Flashear el firmware, sin instalar nada

Necesitas el archivo `ck_bridge_full.bin`: sale de la compilación (ver "Compilar desde el código") o del artefacto que deja el flujo de GitHub Actions del puente.

1. En Windows, si la placa no aparece como puerto COM, instala el driver de su chip USB: CP210x (Silicon Labs) o CH340 (WCH). Lo dice la placa o la página donde la compraste.
2. Abre <https://espressif.github.io/esptool-js/> en Chrome o Edge.
3. Conecta la placa por USB, deja *Baudrate* en 921600 y pulsa **Connect**. Elige el puerto de la placa. Si no conecta, mantén presionado el botón **BOOT** de la placa mientras pulsas Connect.
4. En *Flash Address* escribe `0x0`, elige `ck_bridge_full.bin` y pulsa **Program**. Tarda menos de un minuto.
5. Pulsa **Disconnect** y luego el botón **EN** (o **RST**) de la placa para reiniciarla.

Con Python también sirve: `python -m esptool --chip esp32 write_flash 0x0 ck_bridge_full.bin`.

## 2. Configurarlo por la consola

La configuración queda guardada en la placa; solo se hace una vez.

1. Abre una terminal serial a **115200** baudios: la pestaña *Console* de la misma página de esptool-js, PuTTY en Windows (*Serial*, el puerto COM, 115200) o `python -m serial.tools.miniterm <puerto> 115200`.
2. Presiona Enter hasta ver el prompt `bp32>`. Escribe `help` para ver todos los comandos.
3. Escribe, con tus datos (las comillas permiten espacios):

   ```
   ssid "NombreDeTuRed"
   password "LaClaveDeTuRed"
   ipad 192.168.1.50
   restart
   ```

   `ipad` es la dirección del iPad, o la de tu PC para la prueba de abajo. Se puede cambiar en cualquier momento y se aplica al instante; `ssid` y `password` se aplican al reiniciar.
4. Después del reinicio, `status` muestra el estado del Wi-Fi, la IP del puente, el destino, el control conectado y los paquetes enviados.

Para que la dirección del iPad no cambie, conviene reservarle una IP fija en el router (DHCP estático).

## 3. Emparejar el DualSense

1. Con el puente encendido, mantén **Create** y **PS** en el DualSense hasta que la luz parpadee rápido.
2. Al conectarse, el control vibra un poco y enciende el LED del jugador 1. La barra de luz dice el estado del puente:
   - **Roja:** falta configurar la red o la dirección del iPad.
   - **Amarilla:** conectándose al Wi-Fi.
   - **Verde:** conectado y mandando al destino.
3. Las siguientes veces basta con presionar **PS**: el control vuelve solo al puente.

Si conectas el DualSense a la PS5 por cable, se empareja con la PS5 y olvida el puente. Para volver, repite el paso 1.

## 4. Prueba en la PC (criterio de terminado de A5.2)

Sirve para ver que el puente funciona antes de tener la app del iPad. Se necesita Python 3 en la PC.

1. En la PC, desde la raíz del repo: `python3 bridge/tools/ck_pad_recv.py` (en Windows, `py bridge\tools\ck_pad_recv.py`). Muestra la IP que hay que darle al puente; confírmala con `ipconfig` (Windows) o `ip addr` (Linux).
2. En la consola del puente: `ipad <IP de la PC>`.
3. Si Windows pregunta por el firewall, permite el acceso en redes privadas.
4. El receptor escribe una línea cada vez que cambian los botones y dos por segundo con sticks y gatillos.

Qué revisar y qué reportar en el issue #34:

- Cada botón aparece con su nombre al presionarlo y desaparece al soltarlo: Cruz, Circulo, Cuadrado, Triangulo, la cruceta, L1, R1, L3, R3, Options, Create, Touchpad (clic del panel) y PS.
- Stick izquierdo a la izquierda y arriba da valores negativos en `L`; igual el derecho en `R`. Los extremos llegan cerca de ±32767.
- L2 y R2 van de 0 a 255.
- Con el control quieto, unos **50 paq/s** y "hueco max" cerca de 20 ms. Moviendo los sticks sube, hasta unos 250 paq/s.
- "perdidos" debe quedarse en 0 o muy cerca.
- Copia la salida de `status` del puente y unas líneas del receptor.

Para probar el receptor sin la placa: `python3 bridge/tools/ck_pad_send.py 127.0.0.1` manda un patrón de prueba.

## Cómo manda los paquetes

- Un botón que cambia sale al instante.
- El movimiento de sticks y gatillos sale como mucho cada 4 ms. Un temblor del stick de menos de 2 pasos de los 256 que tiene no manda nada hasta el siguiente repaso.
- Sin cambios, repite el estado cada 20 ms, así un paquete perdido se corrige rápido y el iPad sabe que el control sigue ahí.
- Si el control se desconecta, manda tres veces el estado neutro para que nada se quede presionado y luego calla.
- `seq` sube con cada paquete. El receptor descarta los que no sean más nuevos, salvo después de 500 ms sin paquetes, para aceptar a un puente que se reinició (`ck_pad_rx_filter_accept` en `bridge/protocol/ck_pad_codec.h`).

Mapeo del DualSense a los bits de `ChiakiControllerButton`:

| DualSense | Chiaki |
|---|---|
| Cruz, Círculo, Cuadrado, Triángulo | `CROSS`, `MOON`, `BOX`, `PYRAMID` |
| Cruceta | `DPAD_UP`, `DPAD_DOWN`, `DPAD_LEFT`, `DPAD_RIGHT` |
| L1, R1, L3, R3 | `L1`, `R1`, `L3`, `R3` |
| Options, Create, PS | `OPTIONS`, `SHARE`, `PS` |
| Clic del panel táctil | `TOUCHPAD` y `flags` bit 0 |
| L2, R2 | `l2`, `r2` de 0 a 255 |
| Sticks | `lx`, `ly`, `rx`, `ry` en el rango int16, negativo a la izquierda y arriba |
| Silencio (mute) | No se manda: Chiaki no tiene bit para él |

Las zonas muertas y la posición de los dedos en el panel quedan para A5.4. La vibración de vuelta al control es A5.6.

## Compilar desde el código

Necesitas ESP-IDF 5.3 (la versión que pide Bluepad32 4.2). Desde esta carpeta:

```sh
./deps.sh                # baja Bluepad32 4.2.0 y BTstack a deps/, una sola vez
idf.py build
idf.py -p <puerto> flash monitor
```

Para un solo archivo flasheable en 0x0: `cd build && python -m esptool --chip esp32 merge_bin -o ck_bridge_full.bin @flash_args`.

Los valores por defecto de red y destino se pueden fijar en `idf.py menuconfig`, menú *CK bridge*, para una placa propia. No se suben al repo.

Las pruebas de la lógica portable (paquete, mapeo y ritmo de envío) corren en Linux: `./bridge/tests/run_tests.sh` desde la raíz del repo.

## Archivos

| Archivo | Qué hace |
|---|---|
| `main/main.c` | Arranque: NVS, Wi-Fi, BTstack y Bluepad32 |
| `main/bridge_platform.c` | Plataforma de Bluepad32: toma un control, lo mapea y avisa al envío; barra de luz |
| `main/bridge_net.c` | Wi-Fi en modo estación y la tarea que manda por UDP |
| `main/bridge_config.c` | Red y destino guardados en NVS |
| `main/bridge_console.c` | Comandos `ssid`, `password`, `ipad` y `status` |
| `../core/` | Mapeo y ritmo de envío, en C portable con pruebas en Linux |
| `../protocol/ck_pad_codec.*` | Escribir y leer el paquete C4, y el filtro de `seq` del receptor |
| `../tools/` | Receptor de prueba y emisor falso para la PC |

## Licencias

Este firmware es AGPLv3 como el resto del repo. Bluepad32 es Apache 2.0. BTstack es gratis para proyectos de código abierto y de pago para uso comercial: <https://github.com/bluekitchen/btstack/blob/master/LICENSE>.
