# Guía de pruebas en el iPad

Para TheHuAx: cómo instalar lo que sale de cada PR, qué medir y cómo reportarlo. Es la actividad A0.5 (#8). Los detalles técnicos de la instalación están en [`ios/toolchain/README.md`](../../ios/toolchain/README.md#instalar-en-el-ipad), y el plan de pruebas completo, con las metas de cada punto de control, en [`qa/plan.md`](qa/plan.md).

## Antes de cada prueba

1. **Jailbreak activo.** Si el iPad se reinició, abre `carbon.sep.lol` en Safari, corre Carbon otra vez y revisa que Cydia abra. Sin jailbreak no hay SSH ni filtro del DualSense.
2. **Misma red.** El iPad y la PS5 en la misma red de casa, y Remote Play activado en la PS5 (**Ajustes > Sistema > Remote Play**).
3. **El iPad despierto.** Mientras pruebas por SSH, déjalo desbloqueado y con la pantalla encendida (**Ajustes > Pantalla y brillo > Bloqueo automático > Nunca**). Dormido, apaga el Wi-Fi y SSH no contesta.

## 1. Bajar el `.deb`

- **De un PR:** en la pestaña **Checks** del PR, abre **iOS CI**, baja hasta **Artifacts** y descarga `ipad-debs`.
- **Lo último de `main`:** en **Actions > iOS CI**, abre la corrida más reciente de `main` y descarga `ipad-debs` de la misma forma.

Llega como `.zip`: descomprímelo en una carpeta. GitHub pide haber iniciado sesión para descargarlo.

## 2. Instalar

En PowerShell, dentro de la carpeta descomprimida, con la ruta de tu copia del repo:

```powershell
powershell -ExecutionPolicy Bypass -File C:\ruta\al\repo\ios\toolchain\deploy.ps1 -Ip ipad
```

Instala todos los `.deb` de la carpeta y actualiza la pantalla de inicio. `-Ip ipad` usa tu alias de SSH, que entra sin contraseña; en otra PC, usa la IP del iPad (**Ajustes > Wi-Fi > (i)** junto a tu red) y agrega `-SetupKey` la primera vez. Para instalar uno solo: `-Path paquete.deb`.

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

- **Cada PR que necesita tu prueba trae los pasos exactos** en su descripción, en la sección "En el iPad (TheHuAx)", con qué reportar. Sigue esos pasos; esta guía solo es lo común a todos.
- **Puntos de control:** P1 a P5 tienen su lista de pruebas, sus metas y cuánto tiempo te piden en [`qa/plan.md`](qa/plan.md#nivel-3-en-el-ipad). Ya pasaron P1 y las pruebas de la PS5 de P2 (P2-1 a P2-4); el tono de audio (P2-5) y la app con datos simulados (P2-7) llegan con los hilos de audio e interfaz, y P2-6 no aplica mientras el puente esté en pausa.
- **Prueba de humo**, desde P3, con cada `.deb` que cambie algo en el iPad (5 minutos): instalar, abrir, conectar, 2 minutos de juego, salir, volver a conectar y salir.

Qué medir:

| Qué | Cómo |
|---|---|
| Lo que imprime cada comando | Copia la salida completa: `cksession` ya dice si transmitió, cuadros perdidos y memoria |
| Métricas de la app (fps, cuadros perdidos, memoria) | Las que muestre la app en pantalla, o el resumen por SSH cuando llegue A7.2 |
| Latencia del botón a la pantalla | Con la cámara lenta a 240 fps del teléfono, como dice [`qa/plan.md`](qa/plan.md#latencia-con-cámara-lenta) |
| Batería y calor | Porcentaje al empezar y al terminar; frío, tibio, caliente o incómodo |
| Impresiones | Del 1 al 5: imagen, fluidez, control y audio |

## 4. Reportar

Pégalo en el hilo que te pidió la prueba, o en el chat del proyecto:

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
| SSH tarda y falla (`timed out`) | El iPad está dormido o bloqueado, se reinició sin Carbon, o cambió de IP | Desbloquéalo, corre Carbon si se reinició, y revisa la IP en Ajustes > Wi-Fi |
| `ckctl discover` no ve la PS5 | La PS5 está apagada del todo (no en reposo), en otra red, o sin Remote Play | Revisa los pasos de [Antes de cada prueba](#antes-de-cada-prueba) |
| `la consola sigue ocupada` | La PS5 rechaza una sesión nueva unos 8 segundos después de cerrar otra | Nada: `cksession` vuelve a intentar solo |
| Una app se cierra al abrirla | La firma, o un error al arrancar | Ábrela por SSH, por ejemplo `ssh ipad /Applications/CKHola.app/CKHola`, y reporta lo que imprima (`Killed: 9` apunta a la firma) |
| `deploy.ps1` dice `FALLÓ` | No llegó al iPad, o `dpkg` rechazó el paquete | Reporta los mensajes que salieron arriba del error |
| El DualSense no aparece en Ajustes > Bluetooth | CK Directo no está instalado, o el pad no está en modo de emparejar | Revisa `dpkg -l` y el modo de emparejar; si sigue, reporta `/tmp/ckbtfilter.log` (no trae llaves ni PIN) |
