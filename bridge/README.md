# Puente del DualSense

Lleva el estado del DualSense al iPad por UDP, con el paquete del contrato C4. Dueño: A5.

| Carpeta | Qué es |
|---|---|
| `protocol/` | El paquete C4 (`ck_pad_packet.h`, contrato) y cómo escribirlo, leerlo y filtrarlo por `seq` (`ck_pad_codec.*`). Lo comparten el firmware y el receptor del iPad. |
| `core/` | Mapeo del estado de Bluepad32 a Chiaki y el ritmo de envío, en C portable |
| `esp32/` | Firmware del ESP32 con Bluepad32 y la guía para flashearlo, configurarlo y probarlo |
| `tests/` | Pruebas de `protocol/` y `core/` que corren en Linux: `./bridge/tests/run_tests.sh` |
| `tools/` | Receptor de prueba para la PC y un emisor falso, en Python 3 sin dependencias |
