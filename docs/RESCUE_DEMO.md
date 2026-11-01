# PostgreSQL Map–List–Record rescue example

This example represents flood-response reconnaissance and delivery of supplies.
All ten locations, population counts, needs, priorities and flood areas are
fictional. They are placed near Ho Chi Minh City for a repeatable demonstration.

## Start the example

Prerequisites: Docker Compose, Python 3, and the existing Qt 6.8.3/GStreamer SDK,
including Qt SQL's **QPSQL** driver and the PostgreSQL client library (`libpq`).

```bash
cd /home/b6/Works/CenterDisplay
cmake -S . -B build/map-check \
  -DCMAKE_PREFIX_PATH=/home/b6/Qt/6.8.3/gcc_64 \
  -DCENTERDISPLAY_BUILD_TESTS=OFF \
  -DCENTERDISPLAY_BUILD_MAP_TESTS=ON \
  -DCENTERDISPLAY_BUILD_RESCUE_TESTS=ON
cmake --build build/map-check -j4
bash tools/run-rescue-demo.sh
```

The launcher starts PostgreSQL on **127.0.0.1:55432**, starts a mock ground
station on **127.0.0.1:8766**, exports the scenario from PostgreSQL, and opens
CenterDisplay with six simulated MAVLink feeds on **24550–24555**. Normal app
settings are kept separate. The database is persistent in Docker's
`centerdisplay_rescue-data` volume. Closing the demo stops its simulator and
mock GCS; the database stays running. Stop it without deleting records using:

```bash
docker compose -f docker-compose.rescue.yml stop
```

The Compose password is deliberately a local demo credential. PostgreSQL is not
published to the internet or controllers. Override `CENTER_DB_HOST`,
`CENTER_DB_PORT`, `CENTER_DB_NAME`, `CENTER_DB_USER`, and `CENTER_DB_PASSWORD`
for a separately provisioned database. The optional `CENTER_DB_SCHEMA` isolates tables for tests and each demonstration run. This is a single-center-PC demo;
Qt database calls are synchronous with short connection/query timeouts, not a
multi-user backend or a high-volume telemetry archive.

## What Map–List–Record means

These are three views of the same PostgreSQL data, not three separate copies:

- **Map:** P1–P10 markers show priorities. Blue polygons mark flood areas.
  Colored lines show all active routes; selecting a plan makes its line thicker. The six paper-airplane icons show live MAVLink data.
- **List:** sorts locations red first, then orange, then yellow. Select a row
  to focus the map and open its record. Selecting a map point opens the same
  record. Confirmed locations remain visible in green; delivered locations awaiting reconnaissance are blue.
- **Record:** shows the need and assigned reconnaissance UAV. Enter an
  observation, change priority or mark resolved, and save. The database updates
  the current state and inserts a timestamped history record in one transaction.
  A revision check rejects edits based on an older copy of the location.

Priority interpretation:

| Color | Degree | Example response |
|---|---|---|
| Yellow | 1 · Monitor | Survey access routes or watch water level |
| Orange | 2 · Urgent | Schedule food, blankets or water |
| Red | 3 · Critical | Prioritize first-aid or emergency supplies |

A priority color represents the recorded need; it is not an automated diagnosis
or a declaration that flying there is safe. Flood polygons are display overlays,
not aviation geofences or terrain-avoidance calculations.

## Six UAVs and route assignment

| Slot | Label | Role | Normal telemetry port |
|---|---|---|---|
| 1 | R1 | Reconnaissance | 14550 |
| 2 | R2 | Reconnaissance | 14551 |
| 3 | R3 | Reconnaissance | 14552 |
| 4 | T1 | Transport | 14553 |
| 5 | T2 | Transport | 14554 |
| 6 | T3 | Transport | 14555 |

The video selector, receiver instances, telemetry slots, settings editor, and
simulator all use six UAVs. A previous nine-screen setting migrates to a six-tile
3×2 grid; the first six saved video URLs are retained. Layout choices are 1, 4,
and 6. Video and telemetry remain independent.

Each launcher run creates a separate PostgreSQL schema with three predicted flood areas
and no pre-created plans. All six UAVs wait at base. The operator connects the database, creates a plan, and dispatches it before its UAV starts. The site list starts empty;
scripted locations are discovered within 100 m and inserted with a survey record.
The center refreshes these records every second. This is scripted detection, not
camera image analysis. Existing normal database records are preserved.

For transport:

1. Open **Plans**, select T1, and **Generate draft route**.
2. The planner divides discovered, unassigned open locations among available transports, ordered by
   descending priority, then point ID. It reserves those assignments in the
   saved draft so T2 and T3 select other locations.
3. Click the plan to show its line. Review the waypoint count and note.
4. **Send to demo GCS** sends JSON to the local mock receiver. It acknowledges
   the matching plan and UAV IDs. Status becomes `demo_received` and the Record
   tab receives an event. In the full demo launcher, the matching transport
   simulation then follows the received waypoint route and stops at base.
5. Update location records as supplies are confirmed. Resolved sites are excluded
   from new drafts. Drafting again for the same UAV cancels its previous unsent
   draft in the transaction. Received plans remain reserved until the sites are
   resolved; this prototype does not provide a full delivery lifecycle.

The simulation uses approximately 25 m/s along the route. It produces position
and attitude packets, not video or flight dynamics. Its recon scenario is a
snapshot at launch; restart the simulation to update that patrol snapshot.
The mock's receipt is not a flight-completion acknowledgement.

Drafts contain straight line segments at **80 m relative to home**. They do not
optimize distance, assess payload/battery, plan separation, avoid obstacles, check
terrain/airspace, or select landing/drop locations. The example base and home
altitude are placeholders. Review and replace these values before real use.
There is no automatic takeoff, cargo release, landing, arming, or flight start.

## PostgreSQL tables

| Table | Purpose |
|---|---|
| `rescue_uav` | Fixed six-UAV roster and role constraints |
| `rescue_site` | Current location, priority, need, people, recon assignment, revision |
| `rescue_area` | Flood-area names, severity and polygon vertices as JSONB |
| `rescue_plan` | Route snapshot, site IDs/revisions, assigned UAV, UUID and delivery state |
| `rescue_record` | Timestamped reports, drafted plans and delivery results |

Coordinates are WGS84 latitude/longitude in degrees. Paths are JSONB arrays of
named coordinates; this example uses PostgreSQL without PostGIS. Add PostGIS
when you need spatial indexing, polygon queries or proximity-based dispatch.
Planning uses a transaction and database lock to prevent competing assignments.
Before sending, the center compares each saved site revision against the current
record. If a need has changed, it requires a new draft.

```text
R1–R3 → Z2-Flight → MAVLink positions + RTSP → CenterDisplay
                                                 │
                                      operator reconnaissance report
                                                 ↓
                                           PostgreSQL
                                                 ↓
                           priority + unassigned sites → route draft
                                                 ↓
                     T1/T2/T3 controller plan inbox over Tailscale
                                                 ↓
                       local plan review → aircraft upload → flight start
                                                 ↓
                         UAV telemetry/video → Z2-Flight → center
```

## Connect to the actual Z2-Flight application

The existing observation-only telemetry sender in Z2-Flight continues unchanged.
It cannot receive missions. This work adds `CenterPlanInbox` as a separate TCP
receiver and adds its controls to your `/home/b6/Works/qgroundcontrol` source.
The source files and idempotent installer are also retained in
`integration/z2-flight/` for review and future application.

1. Build and deploy the updated Z2-Flight to each controller. A desktop build does
   not update an Android APK automatically.
2. In **Application Settings → Video → Center flight-plan inbox**, enter this
   controller's Tailscale IPv4, its fleet slot (1–6), and a unique shared token of
   at least 24 characters. Press **Start plan inbox (8767)**. This is an explicit
   per-session listener; restart it after restarting Z2-Flight.
3. In CenterDisplay **Plans → GCS plan inbox settings**, select the corresponding
   UAV and enter the same controller IPv4 and token. Allow TCP 8767 through your
   Tailscale ACLs/firewall. HTTP runs inside the encrypted Tailscale connection;
   this adapter does not implement TLS and rejects public/wildcard bind addresses.
   Loopback is supported for local integration tests. Tokens are stored in the
   center PC's user QSettings file, not encrypted at rest; protect that account.
4. Generate and review a route, then **Send draft to Z2-Flight**. The inbox checks
   token, fleet slot, UUID, coordinate/altitude ranges and request size. It saves
   the route atomically as a `.plan` file in its app-data `CenterPlanInbox` folder.
5. On the GCS, open Plan view and **Load center route for review**. Save existing
   edits first; the load button is disabled while the current plan is dirty or
   an upload is in progress. Confirm the actual connected UAV: the inbox's fleet
   slot identifies the controller assignment, not the aircraft MAVLink ID.
6. Review firmware/vehicle type (the example exports ArduPilot multirotor defaults),
   home, waypoints, altitude, takeoff and ending action. Use the existing Z2-Flight
   **Review & upload** workflow to upload to the aircraft, then its existing
   flight controls to start when appropriate.

`received_for_review` means the **GCS saved the file**, not that the aircraft
accepted it or started flying. The center does not yet receive mission-upload
or mission-progress acknowledgements from Z2-Flight. A timeout means unconfirmed
delivery. Retrying the same plan uses the same UUID; the receiver acknowledges
identical files and rejects a reused UUID with different content.

## Testing and learning the implementation

```bash
docker compose -f docker-compose.rescue.yml up -d --wait
XDG_CACHE_HOME=/tmp/center-rescue-cache \
  ctest --test-dir build/map-check --output-on-failure
```

`rescue_tests` creates and removes its own isolated schema, leaving demo records
untouched. It verifies seed counts, report persistence, revision conflicts,
priority ordering, non-overlapping transport assignments, stale-plan rejection,
mock delivery, authenticated inbox delivery, invalid-token rejection, and QML
creation. Socket/database tests require localhost network access.

Read in this order:

1. `database/schema.sql` and `database/seed.sql`: tables, constraints and sample data.
2. `RescueStore.cpp`: PostgreSQL queries, transactions, plans and delivery state.
3. `RescuePanel.qml`: List, Record and Plans interactions.
4. `MapPanel.qml`: polygons, site markers, route lines and UAV positions.
5. `integration/z2-flight/CenterPlanInbox.cc`: receive, validate and save a route.
6. `tools/mock_gcs.py` and `tools/simulate_uavs.py`: how the local demo works.

Reference: PostgreSQL [JSONB](https://www.postgresql.org/docs/current/datatype-json.html)
and MAVLink [mission transfer](https://mavlink.io/en/services/mission.html).

## Mô phỏng khảo sát và cứu trợ (tiếng Việt)

Chạy từ thư mục dự án:

```sh
bash tools/run-rescue-demo.sh
```

Mỗi lần mở tạo một lược đồ PostgreSQL `rescue_demo_<thời gian>_<tiến trình>` riêng.
Dữ liệu bình thường và các phiên trước được giữ nguyên. Phiên mới bắt đầu với **3 vùng
có nguy cơ ngập, chưa có điểm cứu trợ**. R1 khảo sát phía Bắc, R2 phía Đông, R3 phía Tây.
Đây là tình huống dựng sẵn: mười điểm giả định chỉ được đưa vào danh sách/bản đồ khi UAV
phụ trách đến trong phạm vi 100 m; không phải nhận dạng người bằng camera thật.
Các điểm có tên, vị trí, số người, nhu cầu, mức vàng/cam/đỏ và UAV phát hiện.
Ứng dụng đọc cập nhật PostgreSQL mỗi giây. UAV bay giả lập 25 m/s; cần vài phút để khảo sát hết.

1. Chuyển sang **Bản đồ**, nhấn **Kết nối CSDL** để hiện ba vùng dự báo ngập.
   Vào **Kế hoạch → Tạo R1**, chọn vùng, lưu để hiện tuyến; sau đó nhấn
   **Gửi đến trạm mô phỏng** để R1 bay. Lặp lại cho R2 và R3 khi muốn bắt đầu.
2. Theo dõi **Danh sách** tăng dần; chọn điểm để xem **Hồ sơ**, nhu cầu và nhật ký phát hiện.
3. Trong **Kế hoạch**, chọn **T1 · Vận tải** rồi **Tạo dự thảo đường bay**.
   Các điểm đã phát hiện, chưa phân công được chia giữa UAV vận tải còn sẵn sàng, ưu tiên đỏ,
   cam rồi vàng. Nếu cả mười điểm đã xuất hiện, ba kế hoạch lần lượt nhận 4, 3 và 3 điểm.
4. Kiểm tra tuyến trên bản đồ, bấm **Gửi đến trạm mô phỏng**. Lặp lại cho T2 và T3.
   Mỗi trạm giả lập nhận đúng tuyến theo số UAV; chỉ UAV đó bắt đầu di chuyển.
5. UAV thả hàng ở các điểm rồi về căn cứ. Điểm chuyển xanh dương (chờ xác nhận).
   Khi đúng UAV trinh sát phụ trách bay lại trong phạm vi 100 m, điểm chuyển xanh lá.
   Nhật ký ghi riêng giao hàng, xác nhận và hoàn thành tuyến.

Các tuyến đều hiển thị; chọn một kế hoạch để làm đậm tuyến đó. T1–T3 đứng tại căn cứ
trước khi được gửi kế hoạch. Đóng cửa sổ để dừng mô phỏng; mở lại lệnh trên để bắt đầu
một phiên mới. Các kế hoạch đã giao giữ các điểm của mình, các điểm phát hiện muộn cần
một lượt điều phối bổ sung. Tuyến là đoạn thẳng giả lập, chưa tính vật cản hay địa hình.
Nút **Gửi dự thảo đến Z2-Flight** vẫn chỉ lưu tại trạm để kiểm tra; không tự khởi động UAV thật.

Kiểm tra quy trình tự động (cần PostgreSQL đang chạy, cổng 8766 trống):

```sh
python3 tests/survey_simulation_test.py
```

Bài kiểm tra dùng một lược đồ tạm rồi xóa đúng lược đồ đó: xác nhận bắt đầu trống,
phát hiện đủ mười điểm đúng UAV phụ trách và ba UAV vận tải nhận tuyến độc lập,
di chuyển, giao hàng và trở về. Kiểm tra không thay thế chuyến bay thật hoặc video RTSP.


### Pin, hàng và tốc độ mô phỏng

Trong bảng vị trí UAV, mỗi UAV có mức pin, thời gian bay còn lại ước tính và trạng thái
nhiệm vụ. UAV vận tải bắt đầu với **20 kg**; mỗi điểm nhận **5 kg**, tải hàng giảm một lần
khi bay qua điểm đó. Mỗi tuyến tối đa bốn điểm và không vượt số hàng còn lại. Không gửi
đè tuyến khi UAV đang bay. UAV hết hàng cần chọn chiếc khác hoặc bắt đầu phiên mới;
mô phỏng chưa có thao tác nạp hàng.

Pin là mô hình minh họa **30 phút bay cho 100%**, giảm theo thời gian bay mô phỏng,
không phải thông số pin thực tế. UAV trinh sát xuống 20% sẽ về căn cứ, sạc giả lập
2% mỗi giây mô phỏng rồi tiếp tục khảo sát/xác nhận. UAV vận tải dưới 20% không nhận
chuyến mới. Tải hàng, pin và thời gian còn lại trên bảng này chỉ hiện trong phiên mô phỏng;
chưa bổ sung luồng pin/tải thực từ Z2-Flight.

Chọn **Tốc độ mô phỏng** trong bảng điều hành: **Tạm dừng, 1×, 2×, 5×, 10×, 20×**.
Tốc độ thay đổi ngay trong phiên, không đặt lại vị trí hoặc pin. Khi tạm dừng, máy tính
vẫn nhận tín hiệu vị trí để kết nối không bị báo mất, nhưng vị trí, pin và tiến trình bay
đứng yên. Hệ thống chia nhỏ thời gian mô phỏng để tăng tốc không bỏ qua điểm phát hiện
hay giao hàng. Tốc độ bay hiển thị vẫn là vận tốc mô hình (25 m/s); 20× là tốc độ phát lại.

Điểm **vàng/cam/đỏ → xanh dương chờ xác nhận → xanh lá đã xác nhận**.
Chỉ UAV trinh sát được giao vùng mới xác nhận được điểm sau khi nhận hàng; UAV vận tải
không tự chuyển điểm sang xanh lá. Trong phiên mô phỏng, nút đánh dấu xử lý bằng tay
bị vô hiệu hóa để giữ đúng quy trình này. Tất cả phát hiện/xác nhận vẫn là kịch bản giả lập.

### Tạo kế hoạch riêng cho từng UAV

Trong tab **Kế hoạch**, dùng sáu nút **Tạo R1/R2/R3/T1/T2/T3**:

- **Trinh sát:** chọn vùng PostgreSQL, cao độ 10–120 m, khoảng cách dải 20–1000 m và góc
  0–359°. Mặc định chọn vùng có số tương ứng UAV. Thuật toán dựa trên
  `SITLControl/MissionPlanner.cpp`: chiếu tọa độ sang mặt phẳng cục bộ, xoay vùng,
  cắt các dải song song và nối xen kẽ hai đầu dải. CenterDisplay bổ sung ghép cặp
  giao điểm cho các vùng lõm trong dữ liệu mẫu. Đoạn nối giữa dải và đường đến/về
  căn cứ có thể ra ngoài vùng; đây không phải chức năng tránh vật cản. Tuyến tối đa
  100 điểm (giới hạn hộp nhận Z2-Flight), không yêu cầu đã có điểm cứu trợ. Trong kịch bản mô phỏng, phát hiện và
  xác nhận vẫn theo UAV được giao trong `rescue_site.recon_uav`; chọn đúng vùng phụ trách.
- **Vận tải:** chọn 1–4 điểm đang cần cứu trợ. Thứ tự chọn là thứ tự waypoint;
  bỏ chọn rồi chọn lại để đổi thứ tự, hoặc dùng **Chọn lại**. Danh sách cho biết
  mức ưu tiên và số người. Mỗi điểm tương ứng 5 kg trong mô phỏng. Khi lưu, hệ thống
  đọc lại PostgreSQL và từ chối điểm đã xử lý, đã được kế hoạch khác giữ, trùng ID
  hoặc vượt tải còn lại. Chức năng **Tự động phân tuyến** vẫn phân chia theo ưu tiên.

Bấm **Lưu và xem tuyến** để lưu bản dự thảo riêng cho UAV và làm đậm tuyến trên bản đồ.
Thông tin kế hoạch gồm chiều dài và thời gian ước tính ở 25 m/s; kiểm tra cùng mức pin
trước khi gửi. Mô hình pin không phải bộ tính khả năng hoàn thành nhiệm vụ thực tế.
Sau đó dùng nút gửi trạm mô phỏng hoặc Z2-Flight của đúng kế hoạch. Cao độ được giữ
trong từng waypoint và được mô phỏng hiển thị; bản dự thảo không tự kích hoạt UAV thật.

Dữ liệu sử dụng:

| Bảng | Vai trò khi lập kế hoạch |
|---|---|
| `rescue_area` | Tên và đa giác vùng khảo sát |
| `rescue_site` | Tọa độ, nhu cầu, số người, ưu tiên, trạng thái và phiên bản điểm |
| `rescue_plan` | UAV nhận kế hoạch, các waypoint theo thứ tự, danh sách điểm và phiên bản đã chọn |
| `rescue_record` | Nhật ký tạo và gửi kế hoạch |

Việc chọn điểm, thay thế dự thảo cũ và lưu kế hoạch diễn ra trong một giao dịch.
Nếu dữ liệu không còn hợp lệ, dự thảo cũ được giữ lại. Tuyến trinh sát lưu ảnh chụp
hình học vùng tại thời điểm tạo; nếu vùng thay đổi, tạo lại kế hoạch trước khi gửi.

### Hủy thao tác chưa xác nhận

- **Hủy dự thảo** trên từng kế hoạch chưa gửi: lưu trạng thái đã hủy, ghi nhật ký,
  bỏ tuyến khỏi bản đồ và giải phóng các điểm để UAV khác có thể nhận.
- **Hủy chỉnh sửa báo cáo**: bỏ ghi chú đang nhập và khôi phục ưu tiên/trạng thái đã lưu.
- Các cửa sổ lập kế hoạch, cấu hình trạm Z2-Flight và kết nối vị trí có nút **Hủy**;
  đóng mà không ghi thay đổi. Máy chủ bản đồ tùy chỉnh có **Hủy sửa** để khôi phục URL.
- Kế hoạch đang gửi, gửi lỗi/chưa xác nhận hoặc đã nhận không được hủy bằng nút hủy
  dự thảo: trạm có thể đã nhận và UAV có thể đang thực hiện. Cần đối chiếu tại trạm;
  thao tác hủy dự thảo không phát lệnh dừng hoặc thu hồi nhiệm vụ trên UAV.


### Trình tự do người điều hành chủ động bắt đầu

Khi mở mô phỏng mới, CSDL chưa kết nối với giao diện, chưa hiện vùng ngập hoặc tuyến
bay. Cả sáu UAV đứng tại căn cứ với pin 100%; tăng tốc cũng không tự cho UAV bay.

1. **Kết nối CSDL**: tải ba vùng từ PostgreSQL và tự căn bản đồ để xem các vùng.
2. **Kế hoạch → Tạo R1 → Lưu và xem tuyến**: tạo bản dự thảo và hiện đường bay.
   R1 vẫn đứng chờ; có thể kiểm tra hoặc hủy dự thảo.
3. **Gửi đến trạm mô phỏng** trên kế hoạch R1: R1 bắt đầu bay theo tuyến đã nhận.
   R2/R3 và UAV vận tải vẫn chờ kế hoạch riêng của mình.
4. Tạo và gửi kế hoạch cho các UAV khác khi cần. Các bước phát hiện điểm, giao hàng,
   giảm tải, xác nhận xanh, pin và tốc độ mô phỏng tiếp tục hoạt động như trước.

Các phiên cũ được giữ trong PostgreSQL; mỗi lần mở launcher tạo một phiên riêng,
không tự gửi lại các kế hoạch từ phiên trước.
