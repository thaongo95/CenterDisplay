#!/usr/bin/env python3
"""Send six moving MAVLink positions using only Python's standard library.

Run with --launch to open an isolated CenterDisplay simulation on this PC.
This simulates telemetry messages, not flight dynamics or RTSP video.
"""
import argparse
import json
import math
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
from rescue_scenario import Survey


ROUTES = {}
RECON_REPEAT = {}
SCENARIO = []
SURVEY = None
LAST_POSITIONS = {}
VEHICLES = [dict(battery=100.0, payload=20.0 if i >= 3 else 0.0,
                 status='Chờ gửi kế hoạch', last_time=0.0, route_origin=0.0, charging=False, returning=False, mission_active=False) for i in range(6)]


def drop_at(slot, plan, site):
    vehicle = VEHICLES[slot]
    if SURVEY and vehicle['payload'] >= 5 and SURVEY.drop(plan, slot, site):
        vehicle['payload'] = max(0, vehicle['payload'] - 5)
        vehicle['status'] = f'Đã thả hàng tại P{site}'


def simulation_speed(directory):
    try:
        speed = json.loads((Path(directory) / 'control.json').read_text())['speed']
        return speed if type(speed) in (int, float) and speed in (0, 1, 2, 5, 10, 20) else 1
    except (OSError, ValueError, KeyError, TypeError):
        return 1


def publish_status(directory, elapsed, speed):
    target = Path(directory) / 'status.json'
    temp = target.with_suffix('.tmp')
    temp.write_text(json.dumps(dict(elapsed=elapsed, speed=speed, uavs=VEHICLES), ensure_ascii=False))
    temp.replace(target)



def route_position(slot, elapsed):
    base = {'latitude': 10.790, 'longitude': 106.695}
    directory = os.environ.get('CENTER_DEMO_MISSIONS_DIR')
    if directory:
        path = Path(directory) / f'uav{slot+1}.json'
        if path.exists():
            payload = json.loads(path.read_text())
            if slot not in ROUTES or ROUTES[slot][0] != payload['planId']:
                points = list(payload['waypoints'])
                if slot < 3:
                    RECON_REPEAT[slot] = list(points)
                if slot in LAST_POSITIONS:
                    lat, lon = LAST_POSITIONS[slot]
                    points = [dict(latitude=lat, longitude=lon)] + points[1:]
                ROUTES[slot] = (payload['planId'], elapsed, points)
                if slot < 3:
                    VEHICLES[slot]['status'] = 'Đang khảo sát'
                    VEHICLES[slot]['mission_active'] = True
                if SURVEY and slot >= 3:
                    VEHICLES[slot]['status'] = 'Đang vận chuyển'
                    VEHICLES[slot]['mission_active'] = True
                    SURVEY.record(payload['planId'], slot, 'transport_departed', f'T{slot-2} đã nhận tuyến riêng và bắt đầu vận chuyển mô phỏng.')
    if slot in ROUTES:
        _, started, points = ROUTES[slot]
        distance = max(0, elapsed-started) * 25
    else:
        return base['latitude'] + slot*0.00005, base['longitude'], 0, 0
    segments = []
    for a, b in zip(points, points[1:]):
        north = (b['latitude']-a['latitude'])*111320
        east = (b['longitude']-a['longitude'])*111320*math.cos(math.radians(a['latitude']))
        segments.append((a,b,north,east,math.hypot(north,east)))
    total = sum(segment[4] for segment in segments)
    if total <= 0:
        return base['latitude'],base['longitude'],0,0
    if slot < 3:
        if slot in ROUTES and distance >= total and slot in RECON_REPEAT:
            plan, started, _ = ROUTES[slot]
            ROUTES[slot] = (plan, started+total/25, RECON_REPEAT[slot])
            return route_position(slot, elapsed)
        distance %= total
    elif distance >= total:
        if SURVEY:
            for point in points:
                if point.get('siteId'):
                    drop_at(slot, ROUTES[slot][0], point['siteId'])
            VEHICLES[slot]['status'] = 'Đã về căn cứ'
            VEHICLES[slot]['mission_active'] = False
            SURVEY.record(ROUTES[slot][0], slot, 'mission_completed', f'T{slot-2} hoàn thành tuyến và trở về căn cứ.')
        return points[-1]['latitude'],points[-1]['longitude'],0,0
    for a,b,north,east,length in segments:
        if distance <= length and length > 0:
            ratio = distance/length
            if slot >= 3:
                VEHICLES[slot]['status'] = f"Đang đến P{b['siteId']}" if b.get('siteId') else 'Đang về căn cứ'
            return a['latitude']+(b['latitude']-a['latitude'])*ratio, a['longitude']+(b['longitude']-a['longitude'])*ratio, north/length*25, east/length*25
        if SURVEY and slot >= 3 and b.get('siteId'):
            drop_at(slot, ROUTES[slot][0], b['siteId'])
        distance -= length
    return base['latitude'],base['longitude'],0,0


def position_frame(slot, elapsed, sequence):
    # Six circular routes on a 2x3 grid near Ho Chi Minh City.
    angle = elapsed * 0.12 + slot * 0.5
    radius = 0.002
    latitude = 10.80 + (slot // 3 - 1) * 0.009 + radius * math.cos(angle)
    longitude = 106.70 + (slot % 3 - 1) * 0.009 + radius * math.sin(angle)
    altitude = 80 + slot * 10 + 5 * math.sin(angle)
    north = -radius * 111320 * 0.12 * math.sin(angle)
    east = radius * 111320 * math.cos(math.radians(latitude)) * 0.12 * math.cos(angle)
    if SCENARIO:
        vehicle = VEHICLES[slot]
        dt = max(0, elapsed - vehicle['last_time'])
        vehicle['last_time'] = elapsed
        if slot < 3 and (vehicle['battery'] <= 20 or vehicle['returning'] or vehicle['charging']):
            lat, lon = LAST_POSITIONS.get(slot, (10.790, 106.695))
            dn = (10.790-lat)*111320
            de = (106.695-lon)*111320*math.cos(math.radians(lat))
            remaining = math.hypot(dn,de)
            if remaining <= max(1,dt*25):
                latitude, longitude, north, east = 10.790, 106.695, 0, 0
                vehicle['returning'] = False
                vehicle['charging'] = True
                vehicle['status'] = 'Đang sạc tại căn cứ'
                vehicle['battery'] = min(100,vehicle['battery']+dt*2)
                if vehicle['battery'] >= 100:
                    vehicle['charging'] = False
                    vehicle['route_origin'] = elapsed
                    if slot in ROUTES:
                        plan, _, points = ROUTES[slot]
                        ROUTES[slot] = (plan, elapsed, RECON_REPEAT.get(slot,points))
                    vehicle['status'] = 'Đang khảo sát'
            else:
                vehicle['returning'] = True
                vehicle['status'] = 'Pin thấp · đang về căn cứ'
                ratio = min(1, dt*25/remaining)
                latitude, longitude = lat+(10.790-lat)*ratio, lon+(106.695-lon)*ratio
                north, east = dn/remaining*25, de/remaining*25
                vehicle['battery'] = max(0, vehicle['battery']-dt/18)
        elif vehicle['battery'] <= 0:
            latitude, longitude = LAST_POSITIONS.get(slot, (10.790, 106.695))
            north = east = 0
            vehicle['status'] = 'Hết pin · dừng mô phỏng UAV'
        else:
            latitude, longitude, north, east = route_position(slot, elapsed)
            if north or east:
                if slot < 3:
                    vehicle['status'] = 'Đang khảo sát / xác nhận điểm'
                vehicle['battery'] = max(0, vehicle['battery'] - dt / 18)

        LAST_POSITIONS[slot] = (latitude, longitude)
        if SURVEY and slot in ROUTES:
            SURVEY.discover(slot, latitude, longitude)
        altitude = 80 if north or east else 0
        if altitude and slot in ROUTES:
            altitude = ROUTES[slot][2][-1].get('relativeAltitudeM',80)
    heading = math.degrees(math.atan2(east, north)) % 360
    # GLOBAL_POSITION_INT wire layout: milliseconds, lat/lon (1e-7 degrees),
    # AMSL/relative altitude (mm), N/E/down speed (cm/s), heading (centidegrees).
    payload = struct.pack('<IiiiihhhH', int(elapsed * 1000) & 0xffffffff,
                          round(latitude * 1e7), round(longitude * 1e7),
                          round((altitude + 10) * 1000), round(altitude * 1000),
                          round(north * 100), round(east * 100), 0,
                          round(heading * 100) % 36000)
    # MAVLink 1: payload length, sequence, system ID 1, component ID 1, message 33.
    header = bytes((28, sequence % 256, 1, 1, 33))
    crc = 0xffff
    for byte in header + payload + bytes((104,)):  # message-specific CRC_EXTRA
        value = byte ^ (crc & 0xff)
        value ^= (value << 4) & 0xff
        crc = (crc >> 8) ^ (value << 8) ^ (value << 3) ^ (value >> 4)
    return b'\xfe' + header + payload + struct.pack('<H', crc)


def attitude_frame(slot, elapsed, sequence):
    payload = struct.pack('<Iffffff', int(elapsed * 1000) & 0xffffffff,
                          0.15 * math.sin(elapsed + slot), 0.08 * math.cos(elapsed + slot),
                          (elapsed * 0.12 + slot * 0.5 + math.pi) % (2 * math.pi) - math.pi,
                          0, 0, 0)
    header = bytes((28, sequence % 256, 1, 1, 30))
    crc = 0xffff
    for byte in header + payload + bytes((39,)):
        value = byte ^ (crc & 0xff)
        value ^= (value << 4) & 0xff
        crc = (crc >> 8) ^ (value << 8) ^ (value << 3) ^ (value >> 4)
    return b'\xfe' + header + payload + struct.pack('<H', crc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rescue', action='store_true', help='use PostgreSQL-exported rescue scenario and mock GCS routes')
    parser.add_argument('--launch', action='store_true', help='open an isolated local app with demo link settings')
    parser.add_argument('--app', type=Path, default=Path(__file__).resolve().parents[1] / 'build/map-check/bin/centerDisplay')
    parser.add_argument('--host', default='127.0.0.1', help='destination IPv4, default localhost')
    parser.add_argument('--base-port', type=int, help='first of six UDP ports; default 24550 with --launch, otherwise 14550')
    parser.add_argument('--duration', type=float, default=0, help='stop sending after this many seconds; 0 runs until Ctrl+C')
    args = parser.parse_args()
    if args.rescue:
        global SCENARIO, SURVEY
        scenario_file = os.environ.get('CENTER_DEMO_SCENARIO')
        if not scenario_file:
            parser.error('Use tools/run-rescue-demo.sh to export the PostgreSQL scenario first.')
        SCENARIO = json.loads(Path(scenario_file).read_text())
        SURVEY = Survey(SCENARIO)
    port = args.base_port if args.base_port is not None else (24550 if args.launch else 14550)
    if not 1024 <= port <= 65530 or not math.isfinite(args.duration) or args.duration < 0:
        parser.error('Use base port 1024–65530 and a finite nonnegative duration.')
    try:
        socket.inet_pton(socket.AF_INET, args.host)
    except OSError:
        parser.error('--host must be an IPv4 address')
    if args.launch and (args.host != '127.0.0.1' or not args.app.is_file()):
        parser.error('--launch needs localhost and an existing executable; build the app or supply --app PATH')

    child = None
    with tempfile.TemporaryDirectory(prefix='centerdisplay-demo-') as config:
        try:
            if args.launch:
                folder = Path(config) / 'CenterDisplay'
                folder.mkdir()
                lines = ['[telemetry]']
                for slot in range(6):
                    lines += [f'{slot}\\port={port + slot}', f'{slot}\\source=127.0.0.1', f'{slot}\\system=1']
                (folder / 'CenterDisplay.ini').write_text('\n'.join(lines) + '\n')
                env = dict(os.environ, XDG_CONFIG_HOME=config)
                child = subprocess.Popen([str(args.app.resolve()), '--simulation'] + (['--rescue-demo'] if args.rescue else []), env=env)
                print('Đã mở MÔ PHỎNG với cấu hình riêng; giữ nguyên cài đặt thông thường.', flush=True)
            print(f'Sending 6 simulated UAVs at 5 Hz to {args.host}:{port}–{port + 5}.', flush=True)
            print('Kết nối CSDL → Kế hoạch → Tạo R1/R2/R3 → Lưu → Gửi đến trạm mô phỏng để bắt đầu bay.' if args.rescue else 'Chọn Bản đồ → Vừa để xem UAV. Ctrl+C dừng mô phỏng.', flush=True)
            started = time.monotonic()
            previous = started
            elapsed = 0.0
            sequence = 0
            frames = [None] * 6
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                while child is None or child.poll() is None:
                    now = time.monotonic()
                    speed = simulation_speed(os.environ['CENTER_DEMO_MISSIONS_DIR']) if args.rescue else 1
                    target_time = elapsed + (now - previous) * speed
                    previous = now
                    if args.duration and now - started >= args.duration:
                        break
                    # Bound simulation steps so fast playback cannot skip detection/drop radii.
                    while elapsed < target_time:
                        elapsed = min(target_time, elapsed + 0.2)
                        for slot in range(6):
                            frames[slot] = position_frame(slot, elapsed, sequence * 2)
                    for slot in range(6):
                        if frames[slot] is None:
                            frames[slot] = position_frame(slot, elapsed, sequence * 2)
                        sock.sendto(frames[slot], (args.host, port + slot))
                        sock.sendto(attitude_frame(slot, elapsed, sequence * 2 + 1), (args.host, port + slot))
                    if args.rescue:
                        publish_status(os.environ['CENTER_DEMO_MISSIONS_DIR'], elapsed, speed)
                    sequence += 1
                    time.sleep(0.2)
            if child is not None and child.poll() is None:
                print('Telemetry stopped. Markers become stale after 5 s. Close the app to finish.', flush=True)
                child.wait()
        except KeyboardInterrupt:
            print('\nSimulation stopped.', flush=True)
        finally:
            if child is not None and child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()


if __name__ == '__main__':
    main()
