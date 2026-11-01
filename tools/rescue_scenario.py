"""PostgreSQL-backed, isolated rescue exercise. No aircraft control."""
import json
import math
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = {'latitude': 10.790, 'longitude': 106.695}


def sql_literal(value):
    return "'" + str(value).replace("'", "''") + "'"


def database(sql):
    schema = os.environ['CENTER_DB_SCHEMA']
    if not re.fullmatch(r'rescue_demo_[a-z0-9_]+', schema):
        raise ValueError('Lược đồ mô phỏng không hợp lệ')
    result = subprocess.run(
        ['docker', 'compose', '-f', str(ROOT / 'docker-compose.rescue.yml'),
         'exec', '-T', 'rescue-db', 'psql', '-X', '-v', 'ON_ERROR_STOP=1',
         '-U', 'center_demo', '-d', 'center_rescue', '-Atq'],
        input=f'SET search_path TO {schema};\n' + sql, text=True,
        capture_output=True, check=True, timeout=10)
    return result.stdout.strip()


def initialize():
    schema = os.environ['CENTER_DB_SCHEMA']
    # Unique schema for every launch; never clear the normal database or old exercises.
    database(f'CREATE SCHEMA {schema};')
    database((ROOT / 'database/schema.sql').read_text())
    database((ROOT / 'database/seed.sql').read_text())
    sites = json.loads(database('SELECT json_agg(s ORDER BY s.id) FROM rescue_site s;'))
    Path(os.environ['CENTER_DEMO_SCENARIO']).write_text(json.dumps(sites, ensure_ascii=False))
    database('DELETE FROM rescue_record; DELETE FROM rescue_site;')
    database("INSERT INTO rescue_record(event,detail) VALUES('demo_seed',"
             "'Sẵn sàng: kết nối CSDL, tạo kế hoạch rồi gửi đến trạm mô phỏng để bắt đầu khảo sát.');")
    print('Đã tạo phiên mô phỏng riêng: ' + schema, flush=True)


def distance(a, b):
    north = (a['latitude'] - b['latitude']) * 111320
    east = (a['longitude'] - b['longitude']) * 111320 * math.cos(math.radians(a['latitude']))
    return math.hypot(north, east)


class Survey:
    def __init__(self, sites):
        self.sites = sites
        self.discovered = set()
        self.events = set()
        self.delivered = set()
        self.confirmed = set()

    def discover(self, slot, latitude, longitude):
        if slot >= 3:
            return
        position = dict(latitude=latitude, longitude=longitude)
        for site in self.sites:
            if site['recon_uav'] != slot + 1 or distance(site, position) > 100:
                continue
            if site['id'] in self.delivered and site['id'] not in self.confirmed:
                detail = f"R{slot+1} xác nhận P{site['id']} đã nhận hàng, chuyển xanh."
                database(f"BEGIN; UPDATE rescue_site SET state='resolved',revision=revision+1,updated_at=now() WHERE id={site['id']} AND state='delivered'; "
                         f"INSERT INTO rescue_record(site_id,uav_id,event,detail) VALUES({site['id']},{slot+1},'recon_confirmed',{sql_literal(detail)}); COMMIT;")
                self.confirmed.add(site['id'])
            if site['id'] in self.discovered:
                continue
            columns = ['id', 'name', 'latitude', 'longitude', 'priority', 'need', 'people', 'recon_uav']
            values = ','.join(sql_literal(site[c]) for c in columns)
            detail = f"R{slot+1} phát hiện {site['name']}: {site['people']} người; {site['need']}."
            database(f"BEGIN; INSERT INTO rescue_site({','.join(columns)}) VALUES({values}) ON CONFLICT DO NOTHING; "
                     f"INSERT INTO rescue_record(site_id,uav_id,event,detail) VALUES({site['id']},{slot+1},'site_discovered',{sql_literal(detail)}); COMMIT;")
            self.discovered.add(site['id'])
            print(detail, flush=True)

    def record(self, plan, slot, event, detail, site=0):
        key = (plan, event, site)
        if key in self.events:
            return False
        database(f"INSERT INTO rescue_record(plan_id,uav_id,site_id,event,detail) VALUES({int(plan)},{slot+1},"
                 f"{int(site) if site else 'NULL'},{sql_literal(event)},{sql_literal(detail)});")
        self.events.add(key)
        return True

    def drop(self, plan, slot, site):
        key = (plan, 'supply_delivered', site)
        if key in self.events or site in self.delivered:
            return False
        detail = f'T{slot-2} đã thả 5 kg hàng tại P{site}; chờ UAV trinh sát xác nhận.'
        database(f"BEGIN; UPDATE rescue_site SET state='delivered',revision=revision+1,updated_at=now() WHERE id={site} AND state='open'; "
                 f"INSERT INTO rescue_record(plan_id,site_id,uav_id,event,detail) VALUES({plan},{site},{slot+1},'supply_delivered',{sql_literal(detail)}); COMMIT;")
        self.events.add(key)
        self.delivered.add(site)
        return True


if __name__ == '__main__':
    initialize()
