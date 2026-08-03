import cv2
import numpy as np
import requests

url = "http://172.20.10.11"

print("Conectando a la ESP32-CAM...")

try:
    response = requests.get(url, stream=True, timeout=10)
    print("Estado HTTP:", response.status_code)
except Exception as e:
    print("No se pudo conectar:")
    print(e)
    exit()

if response.status_code != 200:
    print("La cámara respondió, pero no con video.")
    exit()

bytes_data = b""

cv2.namedWindow("ESP32-CAM Stream", cv2.WINDOW_NORMAL)
cv2.resizeWindow("ESP32-CAM Stream", 800, 600)

for chunk in response.iter_content(chunk_size=16384):
    bytes_data += chunk

    start = bytes_data.find(b"\xff\xd8")  # inicio JPG
    end = bytes_data.find(b"\xff\xd9")    # fin JPG

    if start != -1 and end != -1:
        jpg = bytes_data[start:end + 2]
        bytes_data = b""

        frame = cv2.imdecode(
            np.frombuffer(jpg, dtype=np.uint8),
            cv2.IMREAD_COLOR
        )
        if frame is None:
            continue

        cv2.putText(
            frame,
            "",
            (20, 40),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.8,
            (0, 255, 0),
            2
        )

        cv2.imshow("ESP32-CAM Stream", frame)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cv2.destroyAllWindows()