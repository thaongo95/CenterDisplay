#!/usr/bin/env python3
"""Exercise discovery and three independently dispatched routes against PostgreSQL."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import rescue_scenario as scenario
import simulate_uavs as simulator

os.environ['CENTER_DB_SCHEMA'] = f'rescue_demo_test_{os.getpid()}'
with tempfile.TemporaryDirectory() as folder:
    os.environ['CENTER_DEMO_SCENARIO'] = folder + '/scenario.json'
    os.environ['CENTER_DEMO_MISSIONS_DIR'] = folder
    mock = None
    try:
        scenario.initialize()
        simulator.SCENARIO = json.loads(Path(os.environ['CENTER_DEMO_SCENARIO']).read_text())
        simulator.SURVEY = scenario.Survey(simulator.SCENARIO)
        assert scenario.database('SELECT count(*) FROM rescue_site') == '0'
        assert scenario.database('SELECT count(*) FROM rescue_area') == '3'
        assert scenario.database('SELECT count(*) FROM rescue_plan') == '0'
        for elapsed in (0,100,500):
            for slot in range(6):
                simulator.position_frame(slot,elapsed,1)
                assert simulator.route_position(slot,elapsed)[2:] == (0,0)
                assert simulator.VEHICLES[slot]['battery'] == 100
        assert not simulator.SURVEY.discovered
        # Drafts in PostgreSQL alone never start the aircraft.
        mock = subprocess.Popen([sys.executable, '-u', str(scenario.ROOT / 'tools/mock_gcs.py')], stdout=subprocess.PIPE)
        assert b'8766' in mock.stdout.readline()
        for slot in range(3):
            points=[scenario.BASE]+[s for s in simulator.SCENARIO if s['recon_uav']==slot+1]+[scenario.BASE]
            plan=int(scenario.database("INSERT INTO rescue_plan(uav_id,purpose,waypoints,site_ids,site_revisions) VALUES "
                f"({slot+1},'recon',{scenario.sql_literal(json.dumps(points))}::jsonb,'[]','{{}}') RETURNING id;"))
            assert simulator.route_position(slot,500)[2:] == (0,0)
            payload=dict(schema='center-rescue-demo/v1',demo=True,planId=plan,uavId=slot+1,waypoints=points)
            request=urllib.request.Request('http://127.0.0.1:8766/plans',data=json.dumps(payload).encode(),headers={'Content-Type':'application/json'})
            with urllib.request.urlopen(request,timeout=3) as response:
                assert json.load(response)['status']=='demo_received'
            for waiting in range(slot+1,6):
                assert simulator.route_position(waiting,500)[2:] == (0,0)
            simulator.VEHICLES[slot]['last_time']=0
        # A UAV cannot discover another UAV's assigned location.
        first = simulator.SCENARIO[0]
        simulator.SURVEY.discover(1, first['latitude'], first['longitude'])
        assert not simulator.SURVEY.discovered
        for elapsed in range(0, 700, 2):
            for slot in range(3):
                simulator.position_frame(slot, elapsed, elapsed)
        assert len(simulator.SURVEY.discovered) == 10
        assert scenario.database('SELECT count(*) FROM rescue_site') == '10'
        assert scenario.database("SELECT count(*) FROM rescue_record WHERE event='site_discovered'") == '10'
        assert 50 < simulator.VEHICLES[0]['battery'] < 100
        assert simulator.VEHICLES[3]['battery'] == 100
        for speed in (0, 1, 2, 5, 10, 20):
            Path(folder + '/control.json').write_text(json.dumps({'speed':speed}))
            assert simulator.simulation_speed(folder) == speed
        Path(folder + '/control.json').write_text('{broken')
        assert simulator.simulation_speed(folder) == 1
        for slot in range(3, 6):
            assert simulator.route_position(slot, 700)[2:] == (0, 0)
        for slot in range(3, 6):
            site = simulator.SCENARIO[slot - 3]
            points = [dict(scenario.BASE, siteId=0), dict(site, siteId=site['id']), dict(scenario.BASE, siteId=0)]
            plan = int(scenario.database("INSERT INTO rescue_plan(uav_id,purpose,waypoints,site_ids,site_revisions) VALUES "
                f"({slot+1},'transport',{scenario.sql_literal(json.dumps(points))}::jsonb,'[]','{{}}') RETURNING id;"))
            payload = dict(schema='center-rescue-demo/v1', demo=True, planId=plan, uavId=slot+1, waypoints=points)
            request = urllib.request.Request('http://127.0.0.1:8766/plans', data=json.dumps(payload).encode(), headers={'Content-Type':'application/json'})
            with urllib.request.urlopen(request, timeout=3) as response:
                assert json.load(response)['status'] == 'demo_received'
            simulator.route_position(slot, 700)
            assert simulator.route_position(slot, 710)[2:] != (0, 0)
            # Sending one vehicle never starts any other waiting vehicle.
            for waiting in range(slot + 1, 6):
                assert simulator.route_position(waiting, 710)[2:] == (0, 0)
            end = simulator.route_position(slot, 2000)
            assert end[:2] == (scenario.BASE['latitude'], scenario.BASE['longitude'])
            assert end[2:] == (0, 0)
            simulator.route_position(slot, 2001)  # completion/drop records are idempotent
            assert simulator.VEHICLES[slot]['payload'] == 15
            assert scenario.database(f"SELECT state FROM rescue_site WHERE id={site['id']}") == 'delivered'
            wrong = site['recon_uav'] % 3
            simulator.SURVEY.discover(wrong, site['latitude'], site['longitude'])
            assert scenario.database(f"SELECT state FROM rescue_site WHERE id={site['id']}") == 'delivered'
            simulator.SURVEY.discover(site['recon_uav']-1, site['latitude'], site['longitude'])
            assert scenario.database(f"SELECT state FROM rescue_site WHERE id={site['id']}") == 'resolved'
            simulator.SURVEY.discover(site['recon_uav']-1, site['latitude'], site['longitude'])
        assert scenario.database("SELECT count(*) FROM rescue_record WHERE event='mission_completed'") == '3'
        assert scenario.database("SELECT count(*) FROM rescue_record WHERE event='supply_delivered'") == '3'
        assert scenario.database("SELECT count(*) FROM rescue_record WHERE event='recon_confirmed'") == '3'
        simulator.publish_status(folder, 2001, 20)
        assert json.loads(Path(folder+'/status.json').read_text())['uavs'][3]['payload'] == 15
        # Recon recharges at base instead of becoming permanently unable to confirm.
        simulator.LAST_POSITIONS[0] = (scenario.BASE['latitude'], scenario.BASE['longitude'])
        simulator.VEHICLES[0].update(battery=19, last_time=2020, charging=False, returning=False)
        simulator.position_frame(0, 2020.2, 1)
        assert simulator.VEHICLES[0]['charging']
        for tick in range(1, 250):
            simulator.position_frame(0, 2020.2+tick*.2, tick)
            if not simulator.VEHICLES[0]['charging']:
                break
        assert simulator.VEHICLES[0]['battery'] == 100
        # Run the actual clock/telemetry loop and change its control file during flight.
        Path(folder+'/control.json').write_text(json.dumps({'speed':20}))
        Path(folder+'/status.json').unlink()
        clock_process = subprocess.Popen([sys.executable, str(scenario.ROOT/'tools/simulate_uavs.py'), '--rescue'], stdout=subprocess.DEVNULL)
        try:
            for _ in range(50):
                if Path(folder+'/status.json').exists():
                    break
                time.sleep(.1)
            time.sleep(.8)
            fast = json.loads(Path(folder+'/status.json').read_text())
            assert fast['elapsed'] >= 10
            def set_speed(speed):
                temporary=Path(folder+'/control.tmp')
                temporary.write_text(json.dumps({'speed':speed}))
                temporary.replace(Path(folder+'/control.json'))
            set_speed(0)
            time.sleep(.5)
            paused = json.loads(Path(folder+'/status.json').read_text())
            time.sleep(.6)
            still = json.loads(Path(folder+'/status.json').read_text())
            assert still['elapsed'] == paused['elapsed']
            assert still['uavs'][0]['battery'] == paused['uavs'][0]['battery']
            set_speed(5)
            time.sleep(.8)
            resumed = json.loads(Path(folder+'/status.json').read_text())
            assert 2 <= resumed['elapsed'] - paused['elapsed'] <= 6
        finally:
            clock_process.terminate()
            clock_process.wait(timeout=5)
        # Newly received recon grids join from the current position and repeat from base.
        current = dict(latitude=scenario.BASE['latitude']+.0005, longitude=scenario.BASE['longitude'])
        corner = dict(latitude=scenario.BASE['latitude']+.001, longitude=scenario.BASE['longitude']+.001, siteId=0, relativeAltitudeM=90)
        simulator.LAST_POSITIONS[0] = (current['latitude'],current['longitude'])
        Path(folder+'/uav1.json').write_text(json.dumps(dict(planId=999,waypoints=[scenario.BASE,corner,scenario.BASE])))
        start=simulator.route_position(0,3000)
        assert start[:2] == simulator.LAST_POSITIONS[0]
        cycle=(scenario.distance(current,corner)+scenario.distance(corner,scenario.BASE))/25
        before=simulator.route_position(0,3000+cycle-.05)
        after=simulator.route_position(0,3000+cycle+.05)
        assert scenario.distance(dict(latitude=before[0],longitude=before[1]),dict(latitude=after[0],longitude=after[1])) < 4
        print('PASS: empty start, three areas, ten discoveries, independent receipt/flight/return for three transports.')
    finally:
        if mock:
            mock.terminate()
            mock.wait(timeout=5)
        scenario.database(f"DROP SCHEMA {os.environ['CENTER_DB_SCHEMA']} CASCADE;")
