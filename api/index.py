from flask import Flask, request, jsonify, Response, render_template
from datetime import datetime, timezone, timedelta
from functools import wraps
import time
import requests
import urllib3
import csv
import io
import sys
import os
import asyncio

#sys.path.append(os.path.join(os.path.dirname(__file__)))
#from mailgun import AsyncClient
#from mailgun3 import Mailgun
#import mailgun
#AsyncClient = Mailgun.AsyncClient


urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

app = Flask(__name__, template_folder='../templates')

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
AFRR_URL     = "https://api-baltic.transparency-dashboard.eu/api/v1/export"

# ─── aFRR kešs ────────────────────────────────────────────────────────────────
_cache_afrr      = None
_cache_afrr_time = 0.0
_CACHE_AFRR_TTL  = 3600  # 1 stunda

# ─── Mērījumu saglabāšana ─────────────────────────────────────────────────────
_measurements = []          # saraksts ar dict ierakstiem
_MAX_MEASUREMENTS = 10000   # limits atmiņas patēriņam

_apiKey = os.environ.get('MAILGUN_API_KEY')
_domain = os.environ.get('domain')
html = """<body style="margin: 0; padding: 0;">
 <table border="1" cellpadding="0" cellspacing="0" width="100%">
  <tr>
   <td>
    Hello! Mail from ai_cloud. 
   </td>
  </tr>
 </table>
</body>"""

def post_message():
    return requests.post(
        f"https://api.mailgun.net/v3/{MAILGUN_DOMAIN}/messages",
        auth=("api", _apiKey),
        data={
            "from": f"Mailgun <mailgun@{_domain}>",
            "to": ["maris.dirveiks@gmail.com"],
            "subject": "Hello World",
            "html": html,
        },
        timeout=10
    )

"""
client: AsyncClient = AsyncClient(auth=("_apiKey", key))


async def post_message() -> None:
    # Messages
    # POST /<domain>/messages
    data = {
        "from": "postmaster@" + _domain ,
        "to": "maris.dirveiks@gmail.com",
        "cc": "",
        "subject": "Hello World",
        "html": html,
        "o:tag": "Python test",
    }
    
    # It is strongly recommended that you open files in binary mode.
    # Because the Content-Length header may be provided for you,
    # and if it does this value will be set to the number of bytes in the file.
    # Errors may occur if you open the file in text mode.
    
    files = [
        (
            "attachment",
            ("test1.txt", Path("mailgun/doc_tests/files/test1.txt").read_bytes()),
        ),
        (
            "attachment",
            ("test2.txt", Path("mailgun/doc_tests/files/test2.txt").read_bytes()),
        ),
    ]

    async with AsyncClient(auth=("api", _apiKey)) as _client:
        req = await _client.messages.create(data=data, files=files, domain=_domain)
    print(req.json())
    """
   
async def post():

   # await asyncio.gather(
        post_message()
   # ,)

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


@app.route('/api/measurements', methods=['POST'])
def handle_measurements_post():
    """Saņem mērījumu no ierīces un saglabā atmiņā."""
    global _measurements
    content = request.get_json(silent=True)
    if not content:
        return jsonify({"status": "error", "message": "Invalid JSON"}), 400

    required = {"timestamp", "vali", "valf", "vals"}
    if not required.issubset(content.keys()):
        missing = required - content.keys()
        return jsonify({"status": "error", "message": f"Missing fields: {missing}"}), 400

    record = {
        "received_at": datetime.now(timezone.utc).isoformat(),
        "timestamp":   str(content["timestamp"]),
        "vali":        content["vali"],
        "valf":        content["valf"],
        "vals":        str(content["vals"]),
    }

    _measurements.append(record)
    if len(_measurements) > _MAX_MEASUREMENTS:
        _measurements = _measurements[-_MAX_MEASUREMENTS:]

    return jsonify({"status": "ok", "count": len(_measurements)}), 201


@app.route('/api/measurements', methods=['GET'])
def handle_measurements_get():
    """Atgriež saglabātos mērījumus JSON formātā (pēdējie N ieraksti)."""
    try:
        limit = int(request.args.get('limit', 1000))
    except ValueError:
        limit = 1000
    limit = max(1, min(limit, _MAX_MEASUREMENTS))

    data = _measurements[-limit:]
    return jsonify({"count": len(data), "measurements": data})


@app.route('/api/measurements/export', methods=['GET'])
def handle_measurements_export():
    """Lejupielādē mērījumus kā CSV failu."""
    output = io.StringIO()
    writer = csv.DictWriter(
        output,
        fieldnames=["received_at", "timestamp", "vali", "valf", "vals"],
        lineterminator="\n",
    )
    writer.writeheader()
    writer.writerows(_measurements)

    filename = f"measurements_{datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')}.csv"
    return Response(
        output.getvalue(),
        mimetype="text/csv",
        headers={"Content-Disposition": f"attachment; filename={filename}"},
    )

@app.route('/karlis', methods=['GET'])
def get_karlis():
    # render_template automātiski meklē failu "templates" mapē
    return render_template('karlis.html')

@app.route('/karlis1', methods=['GET'])
def get_karlis1():
    # render_template automātiski meklē failu "templates" mapē
    return render_template('karlis1.html')


@app.route('/info', methods=['GET'])
def get_info():
    return render_template('info.html')


def check_auth(username, password):
    # Šeit norādi savu lietotājvārdu un paroli
    return username == '123' and password == '123'


def requires_auth(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        auth = request.authorization
        if not auth or not check_auth(auth.username, auth.password):
            return Response(
                'Lūdzu, autorizējieties.', 401,
                {'WWW-Authenticate': 'Basic realm="Login Required"'}
            )
        return f(*args, **kwargs)
    return decorated

@app.route('/balance', methods=['GET'])
@requires_auth
def get_balance():
    post()
    return render_template('balance.html')


@app.route('/api/afrr', methods=['GET'])
def handle_afrr():
    """Ielādē aFRR LMP datus no Baltic Transparency Dashboard par pēdējām N dienām."""
    global _cache_afrr, _cache_afrr_time

    days = 15
    try:
        days = int(request.args.get('days', 15))
        days = max(1, min(30, days))
    except ValueError:
        pass

    # Kešs atbilst tikai noklusējumam 15 dienām
    if days == 15 and _cache_afrr is not None and (time.time() - _cache_afrr_time) < _CACHE_AFRR_TTL:
        return jsonify(_cache_afrr)

    try:
        tz = timezone(timedelta(hours=2))
        end_dt   = datetime.now(tz)
        start_dt = end_dt - timedelta(days=days)

        params = {
            "id":               "local_marginal_price_afrr",
            "start_date":       start_dt.strftime("%Y-%m-%dT%H:%M"),
            "end_date":         end_dt.strftime("%Y-%m-%dT%H:%M"),
            "output_time_zone": "EET",
            "output_format":    "json",
            "json_header_groups": "0",
        }

        resp = requests.get(AFRR_URL, params=params, timeout=30)
        resp.raise_for_status()
        raw = resp.json()

        # Normalizē uz {headers: [...], rows: [[...]]}
        result = _parse_afrr(raw)

        if days == 15:
            _cache_afrr      = result
            _cache_afrr_time = time.time()

        return jsonify(result)

    except Exception as e:
        print(f"aFRR fetch kļūda: {e}")
        return jsonify({"error": str(e), "headers": [], "rows": []}), 500


def _parse_afrr(raw):
    """
    Pārvērš Baltic API atbildi vienotā formātā:
    { "headers": ["Time","EE Up","EE Down","LV Up","LV Down","LT Up","LT Down"],
      "rows": [["2026-04-15 00:00", 120.5, 80.3, ...], ...] }
    """
    # Formāts 1: {"headers": [...], "data": [[...]]}
    if isinstance(raw, dict) and "headers" in raw and "data" in raw:
        return {"headers": raw["headers"], "rows": raw["data"]}

    # Formāts 2: {"data": [{"time": ..., col: val, ...}]}
    if isinstance(raw, dict) and "data" in raw and isinstance(raw["data"], list):
        rows_raw = raw["data"]
        if rows_raw and isinstance(rows_raw[0], dict):
            headers = list(rows_raw[0].keys())
            rows = [[r.get(h) for h in headers] for r in rows_raw]
            return {"headers": headers, "rows": rows}
        # data ir masīvs ar masīviem, galvenes citur
        headers = raw.get("columns", raw.get("header", []))
        return {"headers": headers, "rows": raw["data"]}

    # Formāts 3: tieši masīvs ar dict
    if isinstance(raw, list) and raw and isinstance(raw[0], dict):
        headers = list(raw[0].keys())
        rows = [[r.get(h) for h in headers] for r in raw]
        return {"headers": headers, "rows": rows}

    # Formāts 4: tieši masīvs ar masīviem (pirmais – galvenes)
    if isinstance(raw, list) and raw and isinstance(raw[0], list):
        return {"headers": raw[0], "rows": raw[1:]}

    # Nezināms formāts – atgriežam kā ir
    return {"headers": [], "rows": [], "raw": raw}


@app.route('/api/client-info', methods=['GET'])
def client_info():
    return jsonify({
        "ip":      request.headers.get('X-Forwarded-For', '').split(',')[0].strip(),
        "country": request.headers.get('X-Vercel-IP-Country'),
        "region":  request.headers.get('X-Vercel-IP-Country-Region'),
        "city":    request.headers.get('X-Vercel-IP-City'),
        "lat":     request.headers.get('X-Vercel-IP-Latitude'),
        "lon":     request.headers.get('X-Vercel-IP-Longitude'),
        "ua":      request.headers.get('User-Agent'),
        "lang":    request.headers.get('Accept-Language'),
    })


if __name__ == '__main__':
    app.run()
	