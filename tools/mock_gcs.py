#!/usr/bin/env python3
"""Local demonstration receiver; it never connects to an aircraft."""
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path

class Receiver(BaseHTTPRequestHandler):
    plans = {}
    def do_POST(self):
        try:
            size = int(self.headers.get('Content-Length', '0'))
            if self.path != '/plans' or not 0 < size <= 262144:
                raise ValueError('Invalid request')
            payload = json.loads(self.rfile.read(size))
            if payload.get('schema') != 'center-rescue-demo/v1' or payload.get('demo') is not True:
                raise ValueError('Only demo plans accepted')
            if type(payload.get('uavId')) is not int or not 1 <= payload['uavId'] <= 6:
                raise ValueError('Invalid UAV')
            if type(payload.get('planId')) is not int or payload['planId'] <= 0:
                raise ValueError('Invalid plan')
            key = payload['planId']
            if key in self.plans and self.plans[key] != payload:
                raise ValueError('Plan ID already used with different contents')
            self.plans[key] = payload
            directory = os.environ.get('CENTER_DEMO_MISSIONS_DIR')
            if directory:
                path = Path(directory) / f"uav{payload['uavId']}.json"
                temporary = path.with_suffix('.tmp')
                temporary.write_text(json.dumps(payload))
                temporary.replace(path)
            response = {'planId': key, 'uavId': payload['uavId'], 'status': 'demo_received'}
            data = json.dumps(response).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            print(f"DEMO plan {key} received for UAV {payload['uavId']}: {len(payload['waypoints'])} waypoints. No flight command.", flush=True)
        except (ValueError, KeyError, TypeError, json.JSONDecodeError):
            self.send_error(400, 'Invalid demo plan')

if __name__ == '__main__':
    print('Demo GCS listening on 127.0.0.1:8766; no aircraft connection.', flush=True)
    HTTPServer(('127.0.0.1', 8766), Receiver).serve_forever()
