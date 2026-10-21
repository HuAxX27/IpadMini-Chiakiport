# App de iOS

App UIKit para iOS 9.3.5 en armv7, compilada con Theos. Aún no compila: el proyecto de Theos y el toolchain llegan con A1.

| Carpeta | Qué va aquí | Dueño |
|---|---|---|
| `Bridge/` | `CKSession` (C1), ajustes y métricas (C5), descubrimiento y registro | A2, A0 |
| `Video/` | Sumidero de video con VideoToolbox (C2) | A3 |
| `Audio/` | Reproductor de Opus (C3) | A4 |
| `Input/` | Fuentes de entrada: receptor UDP del puente y controles táctiles (C4) | A5 |
| `UI/` | Pantallas UIKit | A6 |
| `cli/` | `ckctl`, la herramienta de línea de comandos para el hito espolón | A2 |
| `toolchain/` | Archivo de toolchain de CMake y dependencias para armv7 | A1 |
| `tests/` | Pruebas que corren en Linux | Cada dueño, coordinado por A7 |

Contratos y reglas: `docs/ipad/contracts.md` y `CLAUDE.md`.
