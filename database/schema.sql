BEGIN;
CREATE TABLE IF NOT EXISTS rescue_uav (
 id integer PRIMARY KEY CHECK(id BETWEEN 1 AND 6),
 name text NOT NULL, role text NOT NULL CHECK(role IN ('recon','transport')),
 CHECK((id<=3 AND role='recon') OR (id>=4 AND role='transport'))
);
CREATE TABLE IF NOT EXISTS rescue_site (
 id integer PRIMARY KEY, name text NOT NULL,
 latitude double precision NOT NULL CHECK(latitude BETWEEN -85 AND 85),
 longitude double precision NOT NULL CHECK(longitude BETWEEN -180 AND 180),
 priority integer NOT NULL CHECK(priority BETWEEN 1 AND 3),
 need text NOT NULL, people integer NOT NULL CHECK(people>=0),
 recon_uav integer NOT NULL REFERENCES rescue_uav(id) CHECK(recon_uav BETWEEN 1 AND 3),
 state text NOT NULL DEFAULT 'open' CHECK(state IN ('open','delivered','resolved')),
 revision integer NOT NULL DEFAULT 1, updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS rescue_area (
 id integer PRIMARY KEY, name text NOT NULL, severity text NOT NULL,
 path jsonb NOT NULL CHECK(jsonb_typeof(path)='array' AND jsonb_array_length(path)>=3)
);
CREATE TABLE IF NOT EXISTS rescue_plan (
 id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
 dispatch_id uuid NOT NULL DEFAULT gen_random_uuid(),
 uav_id integer NOT NULL REFERENCES rescue_uav(id),
 purpose text NOT NULL CHECK(purpose IN ('recon','transport')),
 state text NOT NULL DEFAULT 'draft' CHECK(state IN ('draft','sending','demo_received','received_for_review','failed','cancelled')),
 waypoints jsonb NOT NULL CHECK(jsonb_typeof(waypoints)='array'),
 site_ids jsonb NOT NULL, site_revisions jsonb NOT NULL,
 note text NOT NULL DEFAULT '', created_at timestamptz NOT NULL DEFAULT now(),
 CHECK((purpose='recon' AND uav_id<=3) OR (purpose='transport' AND uav_id>=4))
);
CREATE TABLE IF NOT EXISTS rescue_record (
 id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
 site_id integer REFERENCES rescue_site(id), plan_id bigint REFERENCES rescue_plan(id),
 uav_id integer REFERENCES rescue_uav(id), event text NOT NULL, detail text NOT NULL,
 created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS rescue_record_site_time ON rescue_record(site_id,created_at DESC);
CREATE INDEX IF NOT EXISTS rescue_site_priority ON rescue_site(state,priority DESC,id);
COMMIT;
