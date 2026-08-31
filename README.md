# Prototipo móvil IoT para monitorización ambiental en interiores

Este repositorio contiene el código fuente desarrollado para el Trabajo de Fin de Máster, orientado al diseño e implementación de un prototipo móvil IoT de bajo coste para la monitorización ambiental en interiores.

## Contenido

- Firmware del ESP32 principal.
- Código del servidor Flask.
- Recursos asociados a la ESP32-CAM.
- Archivos complementarios del proyecto.

## Descripción general

El sistema está compuesto por un robot móvil basado en ESP32, sensores ambientales y de proximidad, un servidor Flask y una base de datos InfluxDB. El ESP32 principal recopila datos de los sensores y los envía al servidor mediante WiFi y HTTP en formato JSON. La plataforma Flask recibe los datos, gestiona comandos y permite su almacenamiento para visualización y análisis.

## Tecnologías utilizadas

- ESP32
- ESP32-CAM
- Arduino IDE
- Python
- Flask
- InfluxDB
- OpenCV
- scikit-learn

## Nota

El código corresponde a la versión desarrollada para el TFM y puede requerir ajustes en credenciales, direcciones IP o parámetros de configuración según el entorno de prueba.
