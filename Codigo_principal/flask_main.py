from flask import Flask, request, jsonify
from datetime import datetime, timezone

from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

app = Flask("servidor_flask")

INFLUX_URL = "https://us-east-1-1.aws.cloud2.influxdata.com"
INFLUX_TOKEN = "YQ3PP1vQBgVwNJjT0zkbos6WF1PIrwAjPshrpD6qK4fOXrPhOFVXsFTRpKMm7qlTsbh4mnYtSRVdaPyd5a0Lsg=="
INFLUX_ORG = "Student"
INFLUX_BUCKET = "Robot_TFM"


client = InfluxDBClient(
    url=INFLUX_URL,
    token=INFLUX_TOKEN,
    org=INFLUX_ORG
)

write_api = client.write_api(write_options=SYNCHRONOUS)


@app.route("/", methods=["GET"])
def home():
    return "Servidor Flask funcionando"


@app.route("/sensor_values", methods=["GET", "POST"])
def sensor_values():
    if request.method == "GET":
        return "Endpoint /sensor_values activo. Usa POST para enviar datos."

    data = request.get_json(force=True, silent=True) or {}

    device_id = data.get("device_id", "unknown")
    temperature = data.get("temperature_c")
    humidity = data.get("humidity_pct")
    front_distance = data.get("front_distance_mm")
    tof_valid = data.get("tof_valid", False)

    bus_voltage = data.get("bus_voltage_v")
    shunt_voltage = data.get("shunt_voltage_mv")
    load_voltage = data.get("load_voltage_v")
    current_ma = data.get("current_ma")
    power_mw = data.get("power_mw")

    print("----- Datos recibidos -----")
    print(f"Tiempo: {datetime.now()}")
    print(f"Device ID: {device_id}")
    print(f"Temperatura: {temperature} °C")
    print(f"Humedad: {humidity} %")
    print(f"Distancia frontal: {front_distance} mm")
    print(f"ToF válido: {tof_valid}")
    print(f"Bus Voltage: {bus_voltage} V")
    print(f"Shunt Voltage: {shunt_voltage} mV")
    print(f"Load Voltage: {load_voltage} V")
    print(f"Current: {current_ma} mA")
    print(f"Power: {power_mw} mW")
    print()

    try:
        point = (
            Point("robot_sensors")
            .tag("device_id", device_id)
            .field("tof_valid", bool(tof_valid))
            .time(datetime.now(timezone.utc), WritePrecision.NS)
        )

        if temperature is not None:
            point = point.field("temperature_c", float(temperature))

        if humidity is not None:
            point = point.field("humidity_pct", float(humidity))

        if front_distance is not None:
            point = point.field("front_distance_mm", int(front_distance))

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

        write_api.write(
            bucket=INFLUX_BUCKET,
            org=INFLUX_ORG,
            record=point
        )

        return jsonify({
            "status": "ok",
            "device_id": device_id,
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
    app.run(host="0.0.0.0", port=6000, debug=False, use_reloader=False)