# IpadMini-Chiakiport

**PS Remote Play en el iPad mini de primera generación, para jugar PS5 con un DualSense original.**

[![Licencia: AGPL v3](https://img.shields.io/badge/Licencia-AGPL%20v3-blue.svg)](LICENSES/AGPL-3.0-only-OpenSSL.txt)

IpadMini-Chiakiport es un cliente de PS Remote Play para el iPad mini 1 con iOS 9.3.5 y jailbreak. Recibe el video y el audio de una PS5 en la red de casa y le manda lo que se hace con el control. El protocolo lo pone el core de [Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki).

> **Estado:** en desarrollo, todavía no hay nada que instalar. La oleada 0 del plan está en curso: ya están la estructura del repo y los contratos entre módulos. Lo primero que se va a poder instalar en el iPad es una app de prueba que solo abre (punto de control P1).

## Objetivo

- PS5 en la red local. Jugar por internet queda fuera al principio.
- Video H.264 a 540p y 30 fps, decodificado por hardware.
- Audio Opus.
- Un DualSense original como control, con controles táctiles en pantalla como respaldo.
- Una app UIKit que se instala como paquete `.deb`.

## Hardware objetivo

| | |
|---|---|
| Equipo | iPad mini de primera generación |
| Chip | Apple A5: 2 núcleos Cortex-A9 a 1 GHz, armv7 de 32 bits |
| Memoria | 512 MB |
| Pantalla | 1024×768 (el video en 16:9 ocupa 1024×576) |
| Sistema | iOS 9.3.5 con jailbreak, por ejemplo Phœnix |
| Video | H.264 por hardware; HEVC no |

## Cómo va a funcionar

- **Core:** el protocolo de Remote Play de Chiaki, en C (`lib/`), con parches pequeños para iOS 9. Por ejemplo, `clock_gettime` no existe antes de iOS 10.
- **Video:** VideoToolbox decodifica el H.264 que manda la PS5.
- **Audio:** Opus decodificado y reproducido con AudioQueue o RemoteIO.
- **Control:** iOS 9 no deja leer un DualSense directamente. El camino principal es un puente: un ESP32 clásico o una Raspberry Pi Pico W con [Bluepad32](https://github.com/ricardoquesada/bluepad32) recibe el DualSense por Bluetooth y manda su estado al iPad por UDP. El camino se decide en la prueba P0 del plan.
- **App:** UIKit para iOS 9, compilada en Linux con Theos y el SDK de iOS 9.3, firmada con ldid y empaquetada como `.deb`.

## Qué vas a necesitar

Cuando haya una versión:

- Un iPad mini 1 con iOS 9.3.5 y jailbreak, para instalar el `.deb`.
- Una PS5 con Remote Play activado, en la misma red que el iPad.
- El Account ID de tu cuenta de PSN y el PIN de vinculación de la PS5. Se escriben en la app y nunca se guardan en el repo.
- Un DualSense y, si gana el camino del puente, un ESP32 clásico o una Pico W. Los ESP32-S3 y C3 no sirven: no tienen Bluetooth clásico.

## El plan

El trabajo lo hacen agentes de [Claude Code](https://claude.com/claude-code) en paralelo, cada uno dueño de una carpeta. Solo Axel prueba en el iPad.

- [docs/ipad/README.md](docs/ipad/README.md): agentes, oleadas, actividades y puntos de control.
- [docs/ipad/contracts.md](docs/ipad/contracts.md): los contratos C1 a C5 entre módulos.
- [CLAUDE.md](CLAUDE.md): las reglas para los agentes.

## Mapa del repo

| Carpeta | Qué es |
|---|---|
| `lib/` | Core de Chiaki |
| `ios/` | La app de iOS: puente con el core, video, audio, entrada e interfaz |
| `bridge/` | Firmware del puente ESP32 y protocolo UDP del control |
| `docs/ipad/` | Plan, contratos y guías |
| `vita/`, `docs/design/`, `docs/screenshots/`, `tools/` | Código, documentos y herramientas de VitaRPS5, el proyecto del que parte este repo. Se quedan como referencia y para traer arreglos del core; no son parte de la app del iPad |

## Compilar y probar

El build de iOS todavía no existe: lo arma la actividad A1.1 del plan. Por ahora, en Linux:

```sh
# Pruebas del core (necesita cmake y OpenSSL de desarrollo)
./tools/native_tests.sh

# Los contratos en C compilan
cc -std=c11 -fsyntax-only -Ilib/include -Iios -Ibridge/protocol ios/tests/contracts_check.c
```

`./tools/build.sh` es el build de PS Vita que viene de VitaRPS5 y no sirve para el iPad.

## Créditos

Este repo parte de [VitaRPS5](https://github.com/mauricio-gg/vitaki-vitarps5), de [solidEm](https://github.com/mauricio-gg), un cliente de Remote Play para PS Vita. VitaRPS5 viene del [vitaki-fork de ywnico](https://github.com/ywnico/vitaki-fork) y del port de Chiaki para Vita de [AAGaming](https://github.com/AAGaming00), y todos vienen de [Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki), de Florian Märkl. De VitaRPS5 sale el core, ya probado en ARM de 32 bits con 512 MB de RAM como el iPad.

Gracias también a las personas que VitaRPS5 agradece: [delroth](https://github.com/delroth) por el análisis del registro y el despertar de la consola, [grill2010](https://github.com/grill2010) por el de OAuth de PSN, [FioraAeterna](https://github.com/FioraAeterna) por FEC y corrección de errores, y [Epicpkmn11](https://github.com/Epicpkmn11) por los controles de movimiento.

De qué commit de VitaRPS5 parte el repo y cómo traer sus arreglos: [UPSTREAM.md](UPSTREAM.md).

Este proyecto no tiene relación con Sony. PlayStation, PS5 y DualSense son marcas de Sony Interactive Entertainment.

## Licencia

[AGPL-3.0](LICENSES/AGPL-3.0-only-OpenSSL.txt), como Chiaki y VitaRPS5. Quien distribuya la app tiene que publicar su código fuente.
