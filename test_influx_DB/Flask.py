from flask import Flask, request, jsonify
from datetime import datetime

app = Flask("servidor_flask")

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

    return jsonify({
        "status": "ok",
        "device_id": device_id,
        "message": "Datos recibidos correctamente"
    }), 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=6000, debug=False, use_reloader=False)