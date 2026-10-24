# Guía de pruebas en el iPad

Para TheHuAx: cómo se instala lo que sale de cada PR, qué medir y cómo reportarlo. Es la actividad A0.5 (#8). Los detalles técnicos de la instalación están en [`ios/toolchain/README.md`](../../ios/toolchain/README.md#instalar-en-el-ipad), y el plan de pruebas completo, con las metas de cada punto de control, en [`qa/plan.md`](qa/plan.md).

## Quién hace qué

Todo lo que se hace en la laptop lo hace un agente, sin preguntarte, con una sesión de Remote Control: bajar el `.deb`, instalarlo en el iPad por SSH, correr los comandos y guardar lo que imprimen. A ti te toca lo físico: tener el iPad despierto, correr Carbon si se reinició, emparejar y usar el DualSense, mirar la pantalla y manejar la PS5. Después dices lo que viste. Los pasos de [Bajar el `.deb`](#1-bajar-el-deb) e [Instalar](#2-instalar) quedan aquí por si alguna vez quieres hacerlos tú.

## Antes de cada prueba

1. **Jailbreak activo.** Si el iPad se reinició, abre `carbon.sep.lol` en Safari, corre Carbon otra vez y revisa que Cydia abra. Sin jailbreak no hay SSH ni filtro del DualSense.
2. **Misma red.** El iPad y la PS5 en la misma red de casa, y Remote Play activado en la PS5 (**Ajustes > Sistema > Remote Play**).
3. **El iPad despierto.** Mientras pruebas por SSH, déjalo desbloqueado y con la pantalla encendida (**Ajustes > Pantalla y brillo > Bloqueo automático > Nunca**). Dormido, apaga el Wi-Fi y SSH no contesta.

## 1. Bajar el `.deb`

El CI publica los `.deb` de cada PR y de `main` en [Releases](https://github.com/HuAxX27/IpadMini-Chiakiport/releases), como pre-releases que se bajan sin iniciar sesión: `ipad-pr-<N>` trae lo último del PR N e `ipad-main` lo último de `main`. Un PR que viene de un fork no tiene; su `.deb` queda solo en el artefacto `ipad-debs` del PR (**Checks > iOS CI > Artifacts**), que pide iniciar sesión.

## 2. Instalar

`fetch-debs.ps1` baja los `.deb` y los instala en el iPad en un solo paso, sin preguntar nada. En PowerShell, desde tu copia del repo:

```powershell
powershell -ExecutionPolicy Bypass -File ios\toolchain\fetch-debs.ps1 -Pr 93                       # todo lo del PR 93
powershell -ExecutionPolicy Bypass -File ios\toolchain\fetch-debs.ps1 -Pr 93 -Package chiakiport   # solo un paquete
powershell -ExecutionPolicy Bypass -File ios\toolchain\fetch-debs.ps1                              # lo último de main
```

Actualiza la pantalla de inicio al terminar. Solo instala el build del último commit: si el CI todavía lo está armando (unos 3 minutos después de cada push) o falló, se detiene y lo dice. Entra por tu alias de SSH `ipad`, que no pide contraseña; en otra PC, agrega `-Ip` con la IP del iPad (**Ajustes > Wi-Fi > (i)** junto a tu red) y corre una vez `ios\toolchain\deploy.ps1 -Ip <IP> -SetupKey` para que tampoco la pida. Las demás opciones están en [`ios/toolchain/README.md`](../../ios/toolchain/README.md#bajar-e-instalar-lo-del-ci).

Si ya tienes los `.deb` en una carpeta, `deploy.ps1` los instala: `powershell -ExecutionPolicy Bypass -File C:\ruta\al\repo\ios\toolchain\deploy.ps1 -Ip ipad` dentro de esa carpeta, o con `-Path paquete.deb` para uno solo.

| Paquete | Qué es | Cómo se usa |
|---|---|---|
| `com.thehuax.ckhola` (CK Hola) | La app de prueba de P1 | Ícono en la pantalla de inicio |
| `com.thehuax.ckctl` | `ckctl` y `cksession`: buscar, registrar y despertar la PS5, y abrir sesiones | Por SSH, ver [`ios/cli/README.md`](../../ios/cli/README.md) |
| `com.thehuax.ckdirect` (CK Directo) | El filtro de Bluetooth que deja emparejar el DualSense con el iPad | Pon el pad en modo de emparejar (mantén Create y PS hasta que la luz parpadee rápido) y emparéjalo en **Ajustes > Bluetooth** |

La app de Remote Play se suma a esta tabla cuando A6 la tenga. Para ver qué versión tienes y para quitar un paquete:

```powershell
ssh ipad "dpkg -l | grep thehuax"
ssh ipad "cd /tmp && dpkg -r com.thehuax.ckhola && su mobile -c uicache"
```

## 3. Probar

- **Cada PR que necesita tu prueba trae los pasos exactos** en su descripción, separados entre los que corre el agente en la laptop y los que haces tú, con qué reportar. Esta guía solo es lo común a todos.
- **Puntos de control:** P1 a P5 tienen su lista de pruebas, sus metas y cuánto tiempo te piden en [`qa/plan.md`](qa/plan.md#nivel-3-en-el-ipad). Ya pasaron P1 y las pruebas de la PS5 de P2 (P2-1 a P2-4); el tono de audio (P2-5) y la app con datos simulados (P2-7) llegan con los hilos de audio e interfaz, y P2-6 no aplica mientras el puente esté en pausa.
- **Prueba de humo**, desde P3, con cada `.deb` que cambie algo en el iPad (5 minutos): instalar, abrir, conectar, 2 minutos de juego, salir, volver a conectar y salir.

Qué medir:

| Qué | Cómo |
|---|---|
| Lo que imprime cada comando | Lo guarda el agente desde la laptop: `cksession` ya dice si transmitió, cuadros perdidos y memoria |
| Métricas de la app (fps, cuadros perdidos, memoria) | Las que muestre la app en pantalla, o el resumen por SSH cuando llegue A7.2 |
| Latencia del botón a la pantalla | Con la cámara lenta a 240 fps del teléfono, como dice [`qa/plan.md`](qa/plan.md#latencia-con-cámara-lenta) |
| Batería y calor | Porcentaje al empezar y al terminar; frío, tibio, caliente o incómodo |
| Impresiones | Del 1 al 5: imagen, fluidez, control y audio |

## 4. Reportar

La salida de los comandos la junta el agente. Tú cuenta lo que viste, en el hilo que te pidió la prueba o en el chat del proyecto. Si quieres el formato completo:

```
Prueba: (ID, por ejemplo P3-1)   ·   .deb: (versión o número de PR)
Resultado: pasa / falla / bloqueada
Duración: _ min   ·   Batería: _ % → _ %   ·   Calor: _
Salida o métricas: (pega lo que imprimió)
Impresiones del 1 al 5: imagen _, fluidez _, control _, audio _
Qué viste: cortes, artefactos, desfase o cierres, con el minuto aproximado
```

Si algo se ve mal, un video corto con el teléfono vale más que una descripción.

**Nunca pegues** tu Account ID, el PIN, las llaves del registro (`rp_regist_key`, `rp_key`) ni el contenido de `/var/mobile/Library/Chiakiport/consoles.plist`. `ckctl list` muestra tus consolas sin las llaves. Si `ckctl regist` no puede guardar el registro, lo imprime para que no se pierda: guárdalo fuera del repo y no lo pegues.

## Si algo falla

| Qué pasa | Por qué suele ser | Qué hacer |
|---|---|---|
| SSH tarda y falla (`timed out`) | El iPad está dormido o bloqueado, se reinició sin Carbon, o cambió de IP | Desbloquéalo, corre Carbon si se reinició, y revisa la IP en Ajustes > Wi-Fi. Justo después de reiniciar, SSH falla (`timed out during banner exchange`) hasta que el Wi-Fi se reconecta: espera un minuto |
| `ckctl discover` no ve la PS5 | La PS5 está apagada del todo (no en reposo), en otra red, o sin Remote Play | Revisa los pasos de [Antes de cada prueba](#antes-de-cada-prueba) |
| `la consola sigue ocupada` | La PS5 rechaza una sesión nueva unos 8 segundos después de cerrar otra | Nada: `cksession` vuelve a intentar solo |
| Una app se cierra al abrirla | La firma, o un error al arrancar | Ábrela por SSH, por ejemplo `ssh ipad /Applications/CKHola.app/CKHola`, y reporta lo que imprima (`Killed: 9` apunta a la firma) |
| `deploy.ps1` dice `FALLÓ` | No llegó al iPad, o `dpkg` rechazó el paquete | Reporta los mensajes que salieron arriba del error |
| El DualSense no aparece en Ajustes > Bluetooth | CK Directo no está instalado, o el pad no está en modo de emparejar | Revisa `dpkg -l` y el modo de emparejar; si sigue, reporta `/tmp/ckbtfilter.log` (no trae llaves ni PIN) |
