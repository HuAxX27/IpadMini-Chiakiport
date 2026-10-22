# Puente ESP32 para el DualSense

Firmware para un ESP32 clásico (o un Raspberry Pi Pico W) con [Bluepad32](https://bluepad32.readthedocs.io): se empareja con el DualSense por Bluetooth y manda su estado al iPad por UDP, con el paquete de `bridge/protocol/ck_pad_packet.h`.

Los ESP32-S3 y C3 no sirven: solo tienen Bluetooth LE, y el DualSense usa Bluetooth clásico.

Aún vacío: lo llena A5 en la actividad A5.2.
