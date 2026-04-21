from flask import Flask, request, jsonify
from datetime import datetime, timezone, timedelta
import time
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

app = Flask(__name__)

# ─── Demo cenas (rezerves, ja Nordpool nav pieejams) ─────────────────────────
_DEMO_PRICES = [
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

# ─── Kešs (saglabā vērtības starp pieprasījumiem vienā Vercel instancei) ─────
_cache_prices = None
_cache_label = "Demo data"
_cache_time = 0.0
_CACHE_TTL = 3600  # 1 stunda

price_threshold = 60.0
relay_on = False

NORDPOOL_URL = "https://nordpool.didnt.work/nordpool-lv-excel.csv"


def current_slot():
    """Atgriež pašreizējo 15 min slotu (0–95) pēc Latvijas laika (UTC+2)."""
    tz = timezone(timedelta(hours=2))
    now = datetime.now(tz)
    return (now.hour * 60 + now.minute) // 15


def _fetch_nordpool():
    """
    Ielādē CSV no Nordpool un parsē rītdienas vai šodienas cenas.
    Loģika identiska updateNordpoolData() main.cpp.
    Atgriež (prices_list_96, date_label) vai (None, None) kļūdas gadījumā.
    """
    tz = timezone(timedelta(hours=2))
    today = datetime.now(tz).strftime("%Y-%m-%d")
    tomorrow = (datetime.now(tz) + timedelta(days=1)).strftime("%Y-%m-%d")

    resp = requests.get(NORDPOOL_URL, timeout=10, verify=False)
    resp.raise_for_status()

    # Ierobežojam apjomu tāpat kā C++ (50 * 100 * 2 baiti)
    payload = resp.text[:10000]

    today_raw = []
    tomorrow_raw = []

    for line in payload.split("\n"):
        if tomorrow in line:
            last_semi = line.rfind(";")
            if last_semi != -1:
                try:
                    tomorrow_raw.append(float(line[last_semi + 1:].strip().replace(",", ".")))
                except ValueError:
                    pass
        elif today in line:
            last_semi = line.rfind(";")
            if last_semi != -1:
                try:
                    today_raw.append(float(line[last_semi + 1:].strip().replace(",", ".")))
                except ValueError:
                    pass

    # Rītdiena – prioritāte (tāpat kā main.cpp)
    for raw, label in [(tomorrow_raw, tomorrow), (today_raw, today)]:
        if len(raw) > 90:
            # Apvērš secību un reizina ar 1000 (EUR/kWh → EUR/MWh)
            parsed = [raw[len(raw) - 1 - i] * 1000.0 for i in range(96)]
            return parsed, label

    return None, None


def get_prices():
    """Atgriež (prices, label) – no keša vai fresh no Nordpool."""
    global _cache_prices, _cache_label, _cache_time

    if _cache_prices is not None and (time.time() - _cache_time) < _CACHE_TTL:
        return _cache_prices, _cache_label

    try:
        prices, label = _fetch_nordpool()
        if prices:
            _cache_prices = prices
            _cache_label = label
            _cache_time = time.time()
            print(f"Nordpool cenas atjaunotas: {label}")
            return prices, label
    except Exception as e:
        print(f"Nordpool fetch kļūda: {e}")

    # Keša rezerve – ja bija ielādēts agrāk, izmanto to
    if _cache_prices is not None:
        return _cache_prices, _cache_label

    return _DEMO_PRICES, "Demo data"


@app.route('/api/prices', methods=['GET'])
def handle_prices():
    prices, label = get_prices()
    slot = current_slot()
    return jsonify({
        "prices": prices,
        "currentSlot": slot,
        "threshold": price_threshold,
        "relayOn": relay_on,
        "currentPrice": prices[slot],
        "priceData": label,
    })


@app.route('/api/optimal', methods=['GET'])
def handle_optimal():
    prices_data, _ = get_prices()
    try:
        energy = float(request.args.get('energy', 50))
    except ValueError:
        energy = 50.0
    energy = max(0.0, min(200.0, energy))

    # Sadalām 200 kWh uz 96 slotiem lineāri
    n_slots = max(1, round(energy / 200 * 96))

    # Izvēlāmies lētākos n_slots slotus
    indexed = sorted(range(96), key=lambda i: prices_data[i])
    selected = sorted(indexed[:n_slots])

    # Izmaksas: cena (EUR/MWh) * enerģija_slotā (MWh)
    energy_per_slot_mwh = (energy / n_slots) / 1000
    total_cost = sum(prices_data[i] * energy_per_slot_mwh for i in selected)

    return jsonify({
        "selectedSlots": selected,
        "totalCost": round(total_cost, 2),
        "energy": energy,
        "nSlots": n_slots,
    })


@app.route('/api/threshold', methods=['GET'])
def handle_threshold():
    global price_threshold
    value = request.args.get('value')
    if value is not None:
        try:
            price_threshold = float(value)
        except ValueError:
            pass
    return jsonify({"threshold": price_threshold})


@app.route('/api/data', methods=['POST'])
def handle_data():
    content = request.json
    result = {"status": "success", "processed": content['value'] * 2}
    return jsonify(result)


if __name__ == '__main__':
    app.run()
	