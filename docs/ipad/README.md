# Port al iPad mini 1: plan de trabajo

Objetivo: jugar PS5 por Remote Play en un iPad mini de primera generación (iOS 9.3.5, armv7) con un DualSense original, a 540p y 30 fps en H.264 como meta mínima.

## Decisiones técnicas

- **Core:** el de VitaRPS5 (`lib/`), ya probado en ARM de 32 bits con 512 MB de RAM. Holepunch apagado al inicio: solo red local.
- **Cifrado:** mbedTLS, que el core soporta (`CHIAKI_LIB_ENABLE_MBEDTLS`).
- **Toolchain:** Theos con `iPhoneOS9.3.sdk` en Linux. Xcode 14 y posteriores ya no compilan armv7.
- **Video:** H.264 con VideoToolbox. Sin HEVC ni HDR, que el A5 no decodifica.
- **Control:** iOS 9 no expone el DualSense a las apps. En P0 (2026-10-21) TheHuAx eligió dos caminos en paralelo: un ESP32 clásico con Bluepad32 que manda el estado por UDP (`bridge/`, A5.2 a A5.4), y la investigación de conectar el DualSense directo al iPad con jailbreak, por Bluetooth o por USB (A5.7). Si la conexión directa funciona, entra como otro `CKInputSource` y el puente queda opcional. Dejar el DualSense conectado a la PS5 queda como último recurso. El 2026-10-22 TheHuAx decidió no comprar la placa por ahora: la conexión directa pasa a ser el camino principal (A5.7 investiga, A5.8 la lleva a la app como `CKInputSource`). El puente queda en pausa: su firmware (A5.2) está hecho pero sin probar en hardware, y A5.3 espera a que haya placa.

## Agentes

| Agente | Responsabilidad | Carpeta |
|---|---|---|
| A0 Coordinador | Contratos, integración, upstream, guías, versión final | `docs/ipad/`, `ios/Bridge/CKSettings.h` |
| A1 Toolchain y CI | Theos, SDK 9.3, dependencias para armv7, CI y validaciones de calidad | `ios/toolchain/`, `.github/workflows/ios-*`, `.github/quality/` |
| A2 Core | Parches de iOS 9 en `lib/`, CLI `ckctl`, `CKSession` | `lib/`, `ios/Bridge/`, `ios/cli/` |
| A3 Video | De Annex B a VideoToolbox y a la pantalla | `ios/Video/` |
| A4 Audio | De Opus a la bocina, sincronía | `ios/Audio/` |
| A5 Control | DualSense directo al iPad, puente ESP32 (en pausa), controles táctiles | `ios/Input/`, `bridge/` |
| A6 Interfaz | Pantallas UIKit, registro, ajustes | `ios/UI/` |
| A7 Rendimiento y QA | Plan de pruebas, métricas, afinación | `docs/ipad/qa/`, `ios/tests/` |

## Oleadas y puntos de control

Cada oleada empieza solo cuando TheHuAx aprueba el punto de control anterior en el iPad.

| Oleada | Actividades | Punto de control al cerrar |
|---|---|---|
| 0 Preparación | A0.1–A0.3, A5.1 | P0: camino del DualSense elegido |
| 1 Cimientos | A0.5, A1.1–A1.6, A2.1–A2.3, A3.1, A5.2, A5.7, A7.1 | P1: el hola mundo abre en el iPad |
| 2 Hito espolón | A2.4–A2.5, A4.2, A5.5, A5.8, A6.1, A6.4 (A5.3 en pausa) | P2: la PS5 despierta desde el iPad (seguir o parar) |
| 3 Integración | A2.6–A2.8, A3.2–A3.5, A4.1, A4.3–A4.4, A5.4, A6.2–A6.3, A6.5, A7.2 | P3: primer cuadro; P4: sesión jugable de 10 minutos |
| 4 Afinación | A0.6, A5.6, A6.6, A7.3–A7.6 | P5: versión 1.0, 30 minutos estables |

Cada actividad tiene un issue en GitHub con su dependencia, su criterio de terminado y su tamaño. Cómo instalar y probar en el iPad cada `.deb`, qué medir y cómo reportar: [`guia-de-pruebas.md`](guia-de-pruebas.md).
