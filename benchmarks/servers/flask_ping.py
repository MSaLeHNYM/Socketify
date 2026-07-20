from flask import Flask, jsonify

app = Flask(__name__)

@app.get("/ping")
def ping():
    return jsonify(ok=True)

if __name__ == "__main__":
    import sys
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 19081
    # waitress is a production WSGI server (not the Flask debug server)
    from waitress import serve
    serve(app, host="127.0.0.1", port=port, threads=32, channel_timeout=120)
