# Lightweight MQTT library for Arduino

This repository holds the sources of the MqttLite library.

This library has been designed as a really thin wrapper around the MQTT protocol.

This library handles QOS0 packets and ping requests natively.
QOS1/2 raw messages are forwarded to the incomming messages callback.

## Documentation

API documentation can be found in the `src/MqttLite.h` file.
