from flask import Flask, request, jsonify
from datetime import datetime, timezone, timedelta

app = Flask(__name__)

# ─── Elektrības cenas (96 × 15 min = 24h, EUR/MWh) ──────────────────────────
prices = [
    42.1, 41.5, 40.8, 39.2, 38.7, 37.9, 37.1, 36.8,  # 00:00–01:45
    36.2, 35.9, 35.4, 35.1, 34.9, 34.7, 34.5, 34.3,  # 02:00–03:45
    34.1, 33.9, 33.8, 33.7, 33.9, 34.2, 35.1, 37.4,  # 04:00–05:45
    42.3, 51.2, 63.8, 72.1, 78.4, 81.2, 82.5, 83.1,  # 06:00–07:45
    84.0, 85.2, 86.1, 84.9, 83.2, 81.5, 79.8, 77.3,  # 08:00–09:45
    74.1, 70.8, 68.2, 65.9, 63.4, 61.1, 59.2, 57.8,  # 10:00–11:45
    56.1, 55.3, 54.8, 54.2, 53.9, 54.1, 55.3, 57.2,  # 12:00–13:45
    60.1, 63.4, 67.2, 71.8, 76.4, 80.1, 84.3, 88.7,  # 14:00–15:45
    91.2, 93.5, 94.1, 93.8, 92.1, 89.4, 85.2, 79.8,  # 16:00–17:45
    73.2, 67.1, 61.8, 57.3, 53.2, 50.1, 47.8, 45.9,  # 18:00–19:45
    44.2, 43.1, 42.5, 41.8, 41.2, 40.8, 40.5, 40.2,  # 20:00–21:45
    39.8, 39.4, 39.1, 38.8, 38.5, 38.2, 38.0, 37.8,  # 22:00–23:45
]

price_threshold = 60.0
relay_on = False
price_data = "Demo data"


def current_slot():
    """Atgriež pašreizējo 15 min slotu (0–95) pēc Latvijas laika (UTC+2)."""
    tz = timezone(timedelta(hours=2))
    now = datetime.now(tz)
    return (now.hour * 60 + now.minute) // 15


@app.route('/api/prices', methods=['GET'])
def handle_prices():
    slot = current_slot()
    return jsonify({
        "prices": prices,
        "currentSlot": slot,
        "threshold": price_threshold,
        "relayOn": relay_on,
        "currentPrice": prices[slot],
        "priceData": price_data,
    })


@app.route('/api/data', methods=['POST'])
def handle_data():
    content = request.json
    result = {"status": "success", "processed": content['value'] * 2}
    return jsonify(result)


if __name__ == '__main__':
    app.run()
	