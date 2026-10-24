# CLAUDE.md

IpadMini-Chiakiport: un cliente de PS Remote Play para el iPad mini de primera generación, pensado para jugar PS5 con un DualSense original. Parte del código de [VitaRPS5](https://github.com/mauricio-gg/vitaki-vitarps5) (ver `UPSTREAM.md`), que a su vez viene de Chiaki.

Hardware objetivo, que manda en cada decisión: Apple A5 (2 núcleos Cortex-A9 a 1 GHz, armv7 de 32 bits), 512 MB de RAM, pantalla de 1024×768, iOS 9.3.5 con jailbreak, decodificación H.264 por hardware y nada de HEVC.

## Lee primero

1. `docs/ipad/README.md`: el plan de trabajo, los agentes y qué carpeta posee cada uno.
2. `docs/ipad/contracts.md`: los contratos C1 a C5 entre agentes. Programa contra ellos, no contra el código de otro agente.

## Mapa del repo

| Carpeta | Qué es | Dueño |
|---|---|---|
| `lib/` | Core de Chiaki, tratado como upstream | A2 |
| `ios/Bridge/` | Contratos C1 y C5, y la clase `CKSession` | A2 (C5: A0) |
| `ios/Video/`, `ios/Audio/`, `ios/Input/`, `ios/UI/` | Módulos de la app de iOS | A3, A4, A5, A6 |
| `ios/toolchain/`, `.github/workflows/ios-*`, `.github/quality/` | Theos, SDK 9.3, CI de iOS y validaciones de calidad | A1 |
| `bridge/` | Puente ESP32 para el DualSense y su protocolo UDP | A5 |
| `docs/ipad/` | Plan, contratos, guías para TheHuAx | A0 |
| `vita/`, `docs/design/`, `tools/` | Código y documentos de VitaRPS5, solo como referencia | Nadie: no se editan |

## Reglas duras

- **Piso de API: iOS 9.0, armv7.** Nada marcado como disponible desde iOS 10 o posterior (por ejemplo `clock_gettime`, que el core usa y hay que parchear). No supongas punteros de 64 bits ni `size_t` de 64 bits.
- **Lógica en C portable con pruebas en Linux; la capa de iOS, delgada.** Los agentes trabajan sin iPad: lo que se pueda probar en Linux se prueba en Linux.
- **`lib/` se cambia solo cuando hace falta**, con el cambio mínimo, protegido con `#if defined(__APPLE__) && TARGET_OS_IPHONE` cuando no aplique a otras plataformas, y explicado en el PR.
- **Un contrato se cambia solo en un PR propio** que aprueba A0. Si necesitas algo que el contrato no da, pídelo en vez de rodearlo.
- **Cada agente toca solo su carpeta.** Un cambio fuera de ella va en un PR aparte y se avisa al dueño.
- **Nada de reservas de memoria por cuadro** en video, audio o entrada.
- **Sin datos de PSN en el repo:** ni Account ID, ni PIN, ni llaves de registro.
- **Sin nombres reales del dueño:** en ningún archivo del repo. Si hace falta nombrarlo, usa `TheHuAx`.
- **El proyecto es AGPLv3.** No agregues dependencias cerradas.

## Compilar y probar

- Pruebas del core en Linux: `./tools/native_tests.sh` (heredado de VitaRPS5; necesita cmake y OpenSSL de desarrollo).
- Contratos en C: `cc -std=c11 -fsyntax-only -Ilib/include -Iios -Ibridge/protocol ios/tests/contracts_check.c`.
- App de iOS: la arma A1 (Theos con `iPhoneOS9.3.sdk`). Hasta entonces no hay build de iOS.
- `./tools/build.sh` es el build de PS Vita en Docker: no lo uses para el iPad.
- Antes de subir una rama: `.github/quality/check.sh` (secretos, datos de PSN, el nombre del dueño, análisis estático y lint). Corre igual en cada PR; cómo instalar lo que pide y qué hacer si falla está en `.github/quality/README.md`.

## Ramas, PRs y pruebas en el iPad

- Rama por actividad del plan: `a<agente>/<id>-<tema>`, por ejemplo `a3/a3.1-annexb-avcc`. Un PR por actividad, con el ID de la actividad en el título.
- El cuerpo del PR sigue `.github/pull_request_template.md`.
- **Pruebas en el iPad:** lo que se pueda hacer desde la laptop de TheHuAx lo hace un agente, sin preguntarle, con una sesión de Remote Control: bajar el `.deb`, instalarlo por SSH (`ssh ipad`), correr comandos en el iPad y guardar lo que imprimen. TheHuAx, el dueño del repo, solo hace lo físico (despertar o desbloquear el iPad, correr Carbon después de un reinicio, emparejar o apretar el DualSense, mirar la pantalla, manejar la PS5) y dice lo que ve. Todo PR que cambie lo que se ve o se siente en el dispositivo trae los pasos exactos, separando los de la laptop de los físicos, y qué reportar.
- **Fusión:** cuando el CI de un PR queda en verde y no le queda ningún comentario de revisión pendiente, el agente que lo abrió lo fusiona a `main` sin preguntarle a TheHuAx y borra la rama. Hay dos excepciones. Un PR que cambia lo que se ve o se siente en el iPad espera a que TheHuAx lo pruebe y diga que funciona. Un PR que cambia un contrato espera la aprobación de A0.
- El trabajo se sigue en issues de GitHub, uno por actividad del plan.
