from flask import Blueprint, jsonify, request, current_app

ports_api = Blueprint('ports_api', __name__)

@ports_api.route('/', methods=['GET'])
def get_ports():
    scanner = current_app.config['PORT_SCANNER']
    ports = scanner.scan_ports()
    return jsonify([p.__dict__ for p in ports])

@ports_api.route('/probe', methods=['POST'])
def probe_port():
    data = request.json
    port = data.get('port')
    if not port:
        return jsonify({'error': 'Port not provided'}), 400
        
    scanner = current_app.config['PORT_SCANNER']
    is_open = scanner.probe_port(port)
    return jsonify({'port': port, 'working': is_open})
