
#en el documento se explca el uso de cada libreria de forma brvee
from flask import Flask, request, jsonify

from datetime import datetime, timezone
import os
import threading
import time
import cv2

import numpy as np

import requests
import keyboard


from sklearn.ensemble import IsolationForest

from sklearn.preprocessing import StandardScaler

from influxdb_client import InfluxDBClient, Point, WritePrecision

from influxdb_client.client.write_api import SYNCHRONOUS

#esto permite instalar lo necesario para ejecutar, debido a que se esta usando un entonro virtual en la PC
#pip install flask influxdb-client opencv-python numpy requests keyboard scikit-learn


app = Flask("servidor_flask")


#informacion de Influx
INFLUX_URL = "https://us-east-1-1.aws.cloud2.influxdata.com"

INFLUX_TOKEN = "YQ3PP1vQBgVwNJjT0zkbos6WF1PIrwAjPshrpD6qK4fOXrPhOFVXsFTRpKMm7qlTsbh4mnYtSRVdaPyd5a0Lsg=="
INFLUX_ORG = "Student"

INFLUX_BUCKET = "Robot_TFM"

#ip de movil para la camara
CAMERA_STREAM_URL = "http://172.20.10.11"

#variables globales para el control del robot, el comando remoto y el estado de la camara
robot_mode = "autonomous"
remote_command = "stop"
camera_enabled = False

#dice cuanto tiempo ha pasado desde el ultimo comando enviado al robot, para evitar que se quede sin recibir comandos
last_command_time = time.time()

camera_thread = None
camera_running = False


last_received_data = {}

#variables para el modelo de Isolation Forest, que se usa para detectar anomalias en los datos recibidos del robot
training_data = []
ml_model = None
ml_scaler = None

ml_trained = False


TRAINING_SAMPLES = 30

#se queria hacer una estimacion de la bateria, pero al fianl no tenia sentido 
# lo relacionado con la bateira se descarta
BATTERY_CAPACITY_MAH = 2450
BATTERY_EFFICIENCY = 0.85
USABLE_BATTERY_MAH = BATTERY_CAPACITY_MAH * BATTERY_EFFICIENCY


battery_used_total_mah = 0.0
last_battery_time = None


#variables para la prediccion de los valores de los sensores
prediction_history = {
    "temperature_c": [],
    "humidity_pct": [],
    "load_voltage_v": [],
    "current_ma": [],
    "power_mw": [],
    "battery_percentage": []
}

#ventana de prediccion, se usa para predecir el siguiente valor de los sensores, se puede ajustar para mejorar la prediccion pero no es necesario poruq no es lo principla
PREDICTION_WINDOW = 10


recent_temperatures = []
recent_humidities = []

recent_gas_values = []
recent_currents = []


#toma los datos previamnete definidos

client = InfluxDBClient(
    url=INFLUX_URL,
    token=INFLUX_TOKEN,
    org=INFLUX_ORG
)


#configuracion de escritura en Influx
write_api = client.write_api(write_options=SYNCHRONOUS)




#controles basicos del robot, se hizo con las teclas tradicionales de gaming
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


#esto es para la camara esp32cam
#funciona de manera independiente a la esp32

def camera_viewer():
    global camera_running

    global camera_enabled


    print("Conectando a la ESP32-CAM...")

#conexion a la camara, si no se puede conectar, se desactiva la camara y se retorna
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
    
#se crea una ventana para mostrar el video de la camara, y se ajusta el tamaño de la ventana
    bytes_data = b""
#config basica
    cv2.namedWindow("ESP32-CAM Stream", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("ESP32-CAM Stream", 800, 600)

#chunk size de 16 KB para leer el stream de la camara, se puede ajustar para mejorar la calidad del video
# en este caso se para tener poco retraso


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



#control del mismo teclado del ordenador

def keyboard_control():
    global robot_mode
    global remote_command


    global camera_enabled
    global last_command_time


    global camera_thread
    global camera_running

    print_controls()

#srive para imprimir el estado del robot, el comando remoto y si la camara esta activa
    previous_mode = robot_mode

    previous_command = remote_command

    previous_camera_state = camera_enabled


#condiciones para cpntrol robot, el robot consulta a estos comandos del servidor
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

#solo en modo remoto funcionan estas teclas
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

        
#varibales para detectar anomalias
#se usa la temperatura, humedad, corriente y potencia para detectar anomalia
def ml_anomaly_detection(temperature, humidity, current_ma, power_mw):
    global ml_model
    global ml_scaler
    global ml_trained

#aqui se comprueba que los datos no esten vacios
    if temperature is None or humidity is None or current_ma is None or power_mw is None:
        return {
            "ml_status": "not_available",
            "ml_anomaly": False,
            "ml_score": 0.0,
            "system_recommendation": "continue",
            "analysis_reason": "missing_data"
        }


#funciona para entrenar el modelo de Isolation Forest y detectar anomalias
    sample = [
        float(temperature),
        float(humidity),

        float(current_ma),
        float(power_mw)
    ]

#si no se ha entrenado el modelo, se agregan los datos a la lista de entrenamiento
#sirve para calibrar el modelo y detectar anomalias
    if not ml_trained:
        training_data.append(sample)

        if len(training_data) >= TRAINING_SAMPLES:
            ml_scaler = StandardScaler()
            training_data_scaled = ml_scaler.fit_transform(training_data)

            ml_model = IsolationForest(
                contamination=0.10,
                random_state=42
            )

            ml_model.fit(training_data_scaled)
            ml_trained = True

            print("Modelo Isolation Forest entrenado.")

#retorna  con el estado del modelo y la recomendacion del sistema
        return {
            "ml_status": "calibrating",
            "ml_anomaly": False,

            "ml_score": 0.0,
            "system_recommendation": "continue",
            "analysis_reason": "model_calibration"
        }

#si el modelo ya esta entrenado, se escalan los datos y se hace la prediccion
    sample_scaled = ml_scaler.transform([sample])

    prediction = ml_model.predict(sample_scaled)[0]
    score = ml_model.decision_function(sample_scaled)[0]

#si se detecta una anomalia, se retorna con la recomendacion de quedarse en el lugar
    if prediction == -1:
        return {
            "ml_status": "anomaly",
            "ml_anomaly": True,
            "ml_score": float(score),
            "system_recommendation": "stay",
            "analysis_reason": "anomaly_detected"
        }

#en caso contrario, se retorna con la recomendacion de moverse
    return {
        "ml_status": "normal",
        "ml_anomaly": False,
        "ml_score": float(score),
        "system_recommendation": "move",
        "analysis_reason": "normal_behavior"
    }


#########################################################################################################
#esta funcion no se utiliza, fue para integrar la estimacion de la bateira, pero no funciono como se esperbaa
def update_battery_estimation(current_ma):
    global battery_used_total_mah
    global last_battery_time

    now = time.time()

    if current_ma is None:
        battery_remaining_mah = max(USABLE_BATTERY_MAH - battery_used_total_mah, 0)
        battery_percentage = max((battery_remaining_mah / USABLE_BATTERY_MAH) * 100, 0)

        return {
            "battery_used_mah": battery_used_total_mah,
            "battery_remaining_mah": battery_remaining_mah,
            "battery_percentage": battery_percentage,
            "estimated_runtime_min": 0.0
        }

    current_ma = float(current_ma)

    if last_battery_time is not None:
        delta_time_h = (now - last_battery_time) / 3600
        battery_used_total_mah += current_ma * delta_time_h

    last_battery_time = now

    battery_remaining_mah = max(USABLE_BATTERY_MAH - battery_used_total_mah, 0)
    battery_percentage = max((battery_remaining_mah / USABLE_BATTERY_MAH) * 100, 0)

    if current_ma > 0:
        estimated_runtime_min = (battery_remaining_mah / current_ma) * 60
    else:
        estimated_runtime_min = 0.0

    return {
        "battery_used_mah": battery_used_total_mah,
        "battery_remaining_mah": battery_remaining_mah,
        "battery_percentage": battery_percentage,
        "estimated_runtime_min": estimated_runtime_min
    }
###################################################################################################################

#etsa funcion predice el siguiente valor de los datos recibidos, se usa para la temperatura, humedad, voltaje, corriente y potencia

def predict_next_value(name, value):
    if value is None:
        return None

    values = prediction_history[name]
    values.append(float(value))
    prediction_history[name] = values[-PREDICTION_WINDOW:]

    if len(prediction_history[name]) < 3:
        return float(value)

    y = np.array(prediction_history[name])
    x = np.arange(len(y))

    slope, intercept = np.polyfit(x, y, 1)
    predicted_value = slope * len(y) + intercept

    return float(predicted_value)




#aqui se definen los endpoints del servidor Flask, que permiten consultar el estado del robot, enviar comandos y recibir datos de sensores

@app.route("/", methods=["GET"])
def home():
    return jsonify({
        "status": "Servidor Flask funcionando",
        "robot_mode": robot_mode,
        "remote_command": remote_command,
        "camera_enabled": camera_enabled,
        "camera_stream_url": CAMERA_STREAM_URL
    }), 200


#en esta funcion se retorna el estado del robot, el comando remoto, si la camara esta activa y el tiempo desde el ultimo comando

@app.route("/command", methods=["GET"])
def command():
    command_age_ms = int((time.time() - last_command_time) * 1000)

    return jsonify({
        "robot_mode": robot_mode,
        "remote_command": remote_command,
        "camera_enabled": camera_enabled,
        "command_age_ms": command_age_ms
    }), 200


#aqui se retorna el ultimo dato recibido de los sensores, junto con el estado del robot, el comando remoto y si la camara esta activa
@app.route("/last_data", methods=["GET"])
def last_data():
    return jsonify({
        "robot_mode": robot_mode,
        "remote_command": remote_command,
        "camera_enabled": camera_enabled,
        "last_received_data": last_received_data
    }), 200



#ahora se define el endpoint para recibir los datos de los sensores, procesarlos, detectar anomalias y enviarlos a InfluxDB
@app.route("/sensor_values", methods=["GET", "POST"])
def sensor_values():
    global last_received_data

    if request.method == "GET":
        return "Endpoint /sensor_values activo. Usa POST para enviar datos."

    data = request.get_json(force=True, silent=True) or {}
    last_received_data = data

#esta parte del codigo se encarga de extraer los datos recibidos del robot,
#es repetitivo, pero es necesario para poder procesarlos y enviarlos a InfluxDB
#no todas las variables se consideraron
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

#aqui se llama a la funcion de deteccion de anomalias, que utiliza el modelo de Isolation Forest
    ml_result = ml_anomaly_detection(
        temperature,
        humidity,
        current_ma,
        power_mw
    )

#funcion que retorna el estado del modelo, si hay anomalia, el score, la recomendacion del sistema y el motivo del analisis
    ml_status = ml_result["ml_status"]
    ml_anomaly = ml_result["ml_anomaly"]
    ml_score = ml_result["ml_score"]
    system_recommendation = ml_result["system_recommendation"]
    analysis_reason = ml_result["analysis_reason"]

    battery_result = update_battery_estimation(current_ma)# no se usa

    battery_used_mah = battery_result["battery_used_mah"]

    battery_remaining_mah = battery_result["battery_remaining_mah"]
    battery_percentage = battery_result["battery_percentage"]
    estimated_runtime_min = battery_result["estimated_runtime_min"]


    predicted_temperature = predict_next_value("temperature_c", temperature)
    predicted_humidity = predict_next_value("humidity_pct", humidity)
    predicted_load_voltage = predict_next_value("load_voltage_v", load_voltage)
    predicted_current = predict_next_value("current_ma", current_ma)
    predicted_power = predict_next_value("power_mw", power_mw)
    predicted_battery_percentage = predict_next_value("battery_percentage", battery_percentage)
    
    print()
    #los datos se reciben de la esp32 y se imprimen en consola para verificar que se estan recibiendo correctamente
    #al estar realziando las prueabs, se veian los mismos datos en arduino 
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
    print(f"ML status: {ml_status}")
    print(f"ML anomaly: {ml_anomaly}")
    print(f"ML score: {ml_score}")
    print(f"Recomendacion sistema: {system_recommendation}")
    print(f"Motivo analisis: {analysis_reason}")
    #print(f"Bateria usada: {battery_used_mah} mAh")
    #print(f"Bateria restante: {battery_remaining_mah} mAh")
    #print(f"Bateria porcentaje: {battery_percentage} %")
    print(f"Autonomia estimada: {estimated_runtime_min} min")
    print(f"Temperatura predicha: {predicted_temperature} C")
    print(f"Humedad predicha: {predicted_humidity} %")

    print(f"Voltaje predicho: {predicted_load_voltage} V")
    print(f"Corriente predicha: {predicted_current} mA")

    print(f"Potencia predicha: {predicted_power} mW")
    #print(f"Bateria predicha: {predicted_battery_percentage} %")
    print(f"Distancia frontal: {front_distance} mm")

    print(f"Distancia derecha: {right_distance} mm")
    print(f"Distancia izquierda: {left_distance} mm")

    print(f"Corriente: {current_ma} mA")
    print(f"Potencia: {power_mw} mW")
    print()

#aqui se crea un punto de datos para InfluxDB, con las etiquetas y campos correspondientes
    try:
        point = (
            Point("robot_sensors")
            .tag("device_id", device_id)
            .tag("robot_mode", robot_mode)
            .tag("remote_command", remote_command)
            .tag("esp32_robot_state", esp32_robot_state)
            .tag("ml_status", ml_status)
            .tag("system_recommendation", system_recommendation)
            .tag("analysis_reason", analysis_reason)
            .time(datetime.now(timezone.utc), WritePrecision.NS)
        )


        #agrega los campos al punto de datos, solo si no son nulos
        #esta oarte tambien es repetitiva, pero es necesaria para enviar los datos a influx
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



#se encarga de enviar los datos a InfluxDB, si hay algun error, se captura y se retorna un mensaje de error
        point = point.field("gas_alert", bool(gas_alert))
        point = point.field("camera_enabled", bool(camera_enabled))
        point = point.field("ml_anomaly", bool(ml_anomaly))
        point = point.field("ml_score", float(ml_score))

        point = point.field("battery_used_mah", float(battery_used_mah))
        point = point.field("battery_remaining_mah", float(battery_remaining_mah))
        point = point.field("battery_percentage", float(battery_percentage))
        point = point.field("estimated_runtime_min", float(estimated_runtime_min))

#ahora se agregan los valores predichos al punto de datos, solo si no son nulos
        if predicted_temperature is not None:
            point = point.field("predicted_temperature_c", float(predicted_temperature))

        if predicted_humidity is not None:
            point = point.field("predicted_humidity_pct", float(predicted_humidity))

        if predicted_load_voltage is not None:
            point = point.field("predicted_load_voltage_v", float(predicted_load_voltage))

        if predicted_current is not None:
            point = point.field("predicted_current_ma", float(predicted_current))

        if predicted_power is not None:
            point = point.field("predicted_power_mw", float(predicted_power))

        if predicted_battery_percentage is not None:
            point = point.field("predicted_battery_percentage", float(predicted_battery_percentage))

#se envia el punto de datos a InfluxDB, si hay algun error, se captura y se retorna un mensaje de error
        write_api.write(
            bucket=INFLUX_BUCKET,
            org=INFLUX_ORG,
            record=point
        )


        #en esta parte se retorna un mensaje de exito, junto con los datos recibidos y procesados
        #no todos se seleccionan en influx, pero se retornan para verificar que se estan recibiendo correctamente

        return jsonify({
            "status": "ok",
            "device_id": device_id,
            "robot_mode": robot_mode,
            "remote_command": remote_command,
            "camera_enabled": camera_enabled,
            "ml_status": ml_status,
            "ml_anomaly": ml_anomaly,
            "ml_score": ml_score,
            "system_recommendation": system_recommendation,
            "analysis_reason": analysis_reason,
            "battery_used_mah": battery_used_mah,
            "battery_remaining_mah": battery_remaining_mah,
            "battery_percentage": battery_percentage,
            "estimated_runtime_min": estimated_runtime_min,
            "predicted_temperature_c": predicted_temperature,
            "predicted_humidity_pct": predicted_humidity,
            "predicted_load_voltage_v": predicted_load_voltage,
            "predicted_current_ma": predicted_current,
            "predicted_power_mw": predicted_power,
            "predicted_battery_percentage": predicted_battery_percentage,
            "message": "Datos recibidos y enviados a InfluxDB"
        }), 200

#sirve para capturar cualquier error que ocurra al enviar los datos a InfluxDB y retornar un mensaje de error al cliente
    except Exception as e:
        print("Error enviando a InfluxDB:", repr(e))

        return jsonify({
            "status": "error",
            "message": "No se pudo enviar a InfluxDB",
            "error": str(e)
        }), 500

#se inicia el servidor Flask y el hilo de control del teclado, y se captura la interrupcion del teclado para cerrar el servidor y el hilo de manera segura

if __name__ == "__main__":
    keyboard_thread = threading.Thread(target=keyboard_control, daemon=True)
    keyboard_thread.start()

#se inicia el servidor Flask en el puerto 6000, con debug desactivado y sin recarga auto 
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