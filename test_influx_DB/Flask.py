from flask import Flask, request, jsonify
from datetime import datetime, timezone

#para usar influzx cloud
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

    print("----- Datos recibidos -----")
    print(f"Tiempo: {datetime.now()}")
    print(f"Device ID: {device_id}")
    print(f"Temperatura: {temperature} °C")
    print(f"Humedad: {humidity} %")
    print()


    if temperature is None or humidity is None:
        return jsonify({
            "status": "error",
            "message": "Faltan temperature_c o humidity_pct",
            "received": data
        }), 400
    
    try:
        point = (
            Point("environmental_data")
            .tag("device_id", device_id)
            .field("temperature_c", float(temperature))
            .field("humidity_pct", float(humidity))
            .time(datetime.now(timezone.utc), WritePrecision.NS)
        )

        write_api.write(
            bucket=INFLUX_BUCKET,
            org=INFLUX_ORG,
            record=point
        )

        return jsonify({
            "status": "ok",
            "device_id": device_id,
            "message": "Datos recibidos correctamente"
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