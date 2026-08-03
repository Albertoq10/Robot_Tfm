from flask import Flask, request, jsonify
from datetime import datetime, timezone
import os
import threading
import time
import cv2
import numpy as np
import requests
import keyboard

from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

app = Flask("servidor_flask")

INFLUX_URL = "https://us-east-1-1.aws.cloud2.influxdata.com"
INFLUX_TOKEN = "YQ3PP1vQBgVwNJjT0zkbos6WF1PIrwAjPshrpD6qK4fOXrPhOFVXsFTRpKMm7qlTsbh4mnYtSRVdaPyd5a0Lsg=="
INFLUX_ORG = "Student"
INFLUX_BUCKET = "Robot_TFM"

CAMERA_STREAM_URL = "http://172.20.10.11"

robot_mode = "autonomous"
remote_command = "stop"
camera_enabled = False
last_command_time = time.time()

camera_thread = None
camera_running = False

last_received_data = {}

client = InfluxDBClient(
    url=INFLUX_URL,
    token=INFLUX_TOKEN,
    org=INFLUX_ORG
)

write_api = client.write_api(write_options=SYNCHRONOUS)


def print_controls():
    print()
    print("========== CONTROL DEL ROBOT ==========")
    print("1 = modo autonomo")
    print("2 = modo estacionario")
    print("3 = modo control remoto")
    print("mantener W = avanzar")
    print("mantener S = retroceder")
    print("mantener A = izquierda")
    print("mantener D = derecha")
    print("soltar tecla = stop")
    print("C = abrir/cerrar camara")
    print("Q = cerrar Flask")
    print("=======================================")
    print()


def camera_viewer():
    global camera_running
    global camera_enabled

    print("Conectando a la ESP32-CAM...")

    try:
        response = requests.get(CAMERA_STREAM_URL, stream=True, timeout=10)
        print("Estado HTTP camara:", response.status_code)
    except Exception as e:
        print("No se pudo conectar a la camara:")
        print(e)
        camera_running = False
        camera_enabled = False
        return

    if response.status_code != 200:
        print("La camara respondio, pero no con video.")
        camera_running = False
        camera_enabled = False
        return

    bytes_data = b""

    cv2.namedWindow("ESP32-CAM Stream", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("ESP32-CAM Stream", 800, 600)

    for chunk in response.iter_content(chunk_size=16384):
        if not camera_running:
            break

        bytes_data += chunk

        start = bytes_data.find(b"\xff\xd8")
        end = bytes_data.find(b"\xff\xd9")

        if start != -1 and end != -1:
            jpg = bytes_data[start:end + 2]
            bytes_data = b""

            frame = cv2.imdecode(
                np.frombuffer(jpg, dtype=np.uint8),
                cv2.IMREAD_COLOR
            )

            if frame is None:
                continue

            cv2.imshow("ESP32-CAM Stream", frame)

        if cv2.waitKey(1) & 0xFF == ord("q"):
            camera_running = False
            camera_enabled = False
            break

    try:
        response.close()
    except Exception:
        pass

    try:
        cv2.destroyWindow("ESP32-CAM Stream")
    except Exception:
        pass

    camera_running = False
    camera_enabled = False
    print("Stream de camara cerrado.")


def keyboard_control():
    global robot_mode
    global remote_command
    global camera_enabled
    global last_command_time
    global camera_thread
    global camera_running

    print_controls()

    previous_mode = robot_mode
    previous_command = remote_command
    previous_camera_state = camera_enabled

    while True:
        if keyboard.is_pressed("q"):
            camera_running = False
            camera_enabled = False
            remote_command = "stop"
            last_command_time = time.time()
            print("Cerrando Flask y control del robot.")
            os._exit(0)

        if keyboard.is_pressed("1"):
            robot_mode = "autonomous"
            remote_command = "stop"
            last_command_time = time.time()
            time.sleep(0.25)

        elif keyboard.is_pressed("2"):
            robot_mode = "stationary"
            remote_command = "stop"
            last_command_time = time.time()
            time.sleep(0.25)

        elif keyboard.is_pressed("3"):
            robot_mode = "remote"
            remote_command = "stop"
            last_command_time = time.time()
            time.sleep(0.25)

        elif keyboard.is_pressed("c"):
            last_command_time = time.time()

            if not camera_running:
                camera_enabled = True
                camera_running = True
                print("Camara: ON")
                camera_thread = threading.Thread(target=camera_viewer, daemon=True)
                camera_thread.start()
            else:
                camera_enabled = False
                camera_running = False
                print("Camara: OFF")

            time.sleep(0.5)

        elif robot_mode == "remote":
            if keyboard.is_pressed("w"):
                remote_command = "forward"
                last_command_time = time.time()

            elif keyboard.is_pressed("s"):
                remote_command = "backward"
                last_command_time = time.time()

            elif keyboard.is_pressed("a"):
                remote_command = "left"
                last_command_time = time.time()

            elif keyboard.is_pressed("d"):
                remote_command = "right"
                last_command_time = time.time()

            else:
                remote_command = "stop"
                last_command_time = time.time()

        if robot_mode != previous_mode or remote_command != previous_command or camera_enabled != previous_camera_state:
            print(
                f"Modo: {robot_mode} | "
                f"Comando: {remote_command} | "
                f"Camara: {'ON' if camera_enabled else 'OFF'}"
            )

            previous_mode = robot_mode
            previous_command = remote_command
            previous_camera_state = camera_enabled

        time.sleep(0.05)


@app.route("/", methods=["GET"])
def home():
    return jsonify({
        "status": "Servidor Flask funcionando",
        "robot_mode": robot_mode,
        "remote_command": remote_command,
        "camera_enabled": camera_enabled,
        "camera_stream_url": CAMERA_STREAM_URL
    }), 200


@app.route("/command", methods=["GET"])
def command():
    command_age_ms = int((time.time() - last_command_time) * 1000)

    return jsonify({
        "robot_mode": robot_mode,
        "remote_command": remote_command,
        "camera_enabled": camera_enabled,
        "command_age_ms": command_age_ms
    }), 200


@app.route("/last_data", methods=["GET"])
def last_data():
    return jsonify({
        "robot_mode": robot_mode,
        "remote_command": remote_command,
        "camera_enabled": camera_enabled,
        "last_received_data": last_received_data
    }), 200


@app.route("/sensor_values", methods=["GET", "POST"])
def sensor_values():
    global last_received_data

    if request.method == "GET":
        return "Endpoint /sensor_values activo. Usa POST para enviar datos."

    data = request.get_json(force=True, silent=True) or {}
    last_received_data = data

    device_id = data.get("device_id", "unknown")
    esp32_robot_state = data.get("robot_state", "unknown")

    temperature = data.get("temperature_c")
    humidity = data.get("humidity_pct")

    front_distance = data.get("front_distance_mm")
    front_tof_valid = data.get("front_tof_valid", False)

    right_distance = data.get("right_distance_mm")
    right_tof_valid = data.get("right_tof_valid", False)

    left_distance = data.get("left_distance_mm")
    left_tof_valid = data.get("left_tof_valid", False)

    bus_voltage = data.get("bus_voltage_v")
    shunt_voltage = data.get("shunt_voltage_mv")
    load_voltage = data.get("load_voltage_v")
    current_ma = data.get("current_ma")
    power_mw = data.get("power_mw")

    mq2_ready = data.get("mq2_ready", False)
    gas_raw = data.get("gas_raw")
    gas_voltage_esp = data.get("gas_voltage_esp_v")
    gas_voltage = data.get("gas_voltage_v")
    gas_alert = data.get("gas_alert", False)

    print()
    print("----- Datos recibidos -----")
    print(f"Tiempo: {datetime.now()}")
    print(f"Device ID: {device_id}")
    print(f"Modo Flask: {robot_mode}")
    print(f"Comando remoto: {remote_command}")
    print(f"Estado ESP32: {esp32_robot_state}")
    print(f"Temperatura: {temperature} C")
    print(f"Humedad: {humidity} %")
    print(f"Gas raw: {gas_raw}")
    print(f"Alerta gas: {gas_alert}")
    print(f"Distancia frontal: {front_distance} mm")
    print(f"Distancia derecha: {right_distance} mm")
    print(f"Distancia izquierda: {left_distance} mm")
    print(f"Corriente: {current_ma} mA")
    print(f"Potencia: {power_mw} mW")
    print()

    try:
        point = (
            Point("robot_sensors")
            .tag("device_id", device_id)
            .tag("robot_mode", robot_mode)
            .tag("remote_command", remote_command)
            .tag("esp32_robot_state", esp32_robot_state)
            .time(datetime.now(timezone.utc), WritePrecision.NS)
        )

        if temperature is not None:
            point = point.field("temperature_c", float(temperature))

        if humidity is not None:
            point = point.field("humidity_pct", float(humidity))

        if front_distance is not None:
            point = point.field("front_distance_mm", int(front_distance))
        point = point.field("front_tof_valid", bool(front_tof_valid))

        if right_distance is not None:
            point = point.field("right_distance_mm", int(right_distance))
        point = point.field("right_tof_valid", bool(right_tof_valid))

        if left_distance is not None:
            point = point.field("left_distance_mm", int(left_distance))
        point = point.field("left_tof_valid", bool(left_tof_valid))

        if bus_voltage is not None:
            point = point.field("bus_voltage_v", float(bus_voltage))

        if shunt_voltage is not None:
            point = point.field("shunt_voltage_mv", float(shunt_voltage))

        if load_voltage is not None:
            point = point.field("load_voltage_v", float(load_voltage))

        if current_ma is not None:
            point = point.field("current_ma", float(current_ma))

        if power_mw is not None:
            point = point.field("power_mw", float(power_mw))

        point = point.field("mq2_ready", bool(mq2_ready))

        if gas_raw is not None:
            point = point.field("gas_raw", int(gas_raw))

        if gas_voltage_esp is not None:
            point = point.field("gas_voltage_esp_v", float(gas_voltage_esp))

        if gas_voltage is not None:
            point = point.field("gas_voltage_v", float(gas_voltage))

        point = point.field("gas_alert", bool(gas_alert))
        point = point.field("camera_enabled", bool(camera_enabled))

        write_api.write(
            bucket=INFLUX_BUCKET,
            org=INFLUX_ORG,
            record=point
        )

        return jsonify({
            "status": "ok",
            "device_id": device_id,
            "robot_mode": robot_mode,
            "remote_command": remote_command,
            "camera_enabled": camera_enabled,
            "message": "Datos recibidos y enviados a InfluxDB"
        }), 200

    except Exception as e:
        print("Error enviando a InfluxDB:", repr(e))

        return jsonify({
            "status": "error",
            "message": "No se pudo enviar a InfluxDB",
            "error": str(e)
        }), 500


if __name__ == "__main__":
    keyboard_thread = threading.Thread(target=keyboard_control, daemon=True)
    keyboard_thread.start()

    try:
        app.run(
            host="0.0.0.0",
            port=6000,
            debug=False,
            use_reloader=False,
            threaded=True
        )
    except KeyboardInterrupt:
        print("Servidor Flask detenido.")
        camera_running = False
        os._exit(0)