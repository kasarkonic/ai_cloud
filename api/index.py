from flask import Flask, request, jsonify

app = Flask(__name__)

@app.route('/api/data', methods=['POST'])
def handle_data():
    content = request.json
    # Šeit veic datu apstrādi ar Python
    result = {"status": "success", "processed": content['value'] * 2}
    return jsonify(result)

if __name__ == '__main__':
    app.run()
	