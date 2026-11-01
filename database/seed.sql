BEGIN;
INSERT INTO rescue_uav(id,name,role) VALUES
(1,'R1','recon'),
(2,'R2','recon'),
(3,'R3','recon'),
(4,'T1','transport'),
(5,'T2','transport'),
(6,'T3','transport')
ON CONFLICT DO NOTHING;
INSERT INTO rescue_site(id,name,latitude,longitude,priority,need,people,recon_uav) VALUES
(1,'Trường học ven sông',10.8,106.705,3,'Nước uống và bộ sơ cứu',45,1),
(2,'Trạm y tế phía Bắc',10.817,106.703,3,'Vật tư y tế',18,1),
(3,'Khu dân cư ven kênh',10.81,106.714,2,'Các phần thực phẩm',32,1),
(4,'Điểm trú ẩn phía Đông',10.805,106.725,2,'Chăn và nước uống',60,2),
(5,'Chốt kiểm soát cầu',10.792,106.72,1,'Trinh sát và kiểm tra tình hình',5,2),
(6,'Khu dân cư trên cù lao',10.783,106.709,3,'Nước uống và vật tư khẩn cấp',24,2),
(7,'Kho phía Nam',10.777,106.692,1,'Đánh giá tuyến tiếp cận',8,3),
(8,'Chung cư phía Tây',10.791,106.682,2,'Thực phẩm và bộ vệ sinh',40,3),
(9,'Nhà sinh hoạt cộng đồng',10.805,106.684,1,'Theo dõi mực nước',22,3),
(10,'Điểm tập kết sơ tán',10.818,106.691,3,'Vật tư sơ cứu cho người chờ sơ tán',16,1)
ON CONFLICT DO NOTHING;
INSERT INTO rescue_area VALUES
(1,'Vùng 1 · Bắc · R1 khảo sát','Dự báo nguy cơ ngập cao','[{"latitude":10.798,"longitude":106.688},{"latitude":10.821,"longitude":106.688},{"latitude":10.821,"longitude":106.717},{"latitude":10.798,"longitude":106.717}]'::jsonb),
(2,'Vùng 2 · Đông · R2 khảo sát','Dự báo nguy cơ ngập cao','[{"latitude":10.780,"longitude":106.707},{"latitude":10.797,"longitude":106.718},{"latitude":10.808,"longitude":106.718},{"latitude":10.808,"longitude":106.728},{"latitude":10.780,"longitude":106.728}]'::jsonb),
(3,'Vùng 3 · Tây · R3 khảo sát','Dự báo nguy cơ ngập','[{"latitude":10.774,"longitude":106.679},{"latitude":10.807,"longitude":106.679},{"latitude":10.807,"longitude":106.687},{"latitude":10.794,"longitude":106.698},{"latitude":10.774,"longitude":106.698}]'::jsonb)
ON CONFLICT DO NOTHING;
INSERT INTO rescue_record(event,detail) SELECT 'demo_seed','Tình huống giả lập: 3 vùng dự báo ngập, 3 UAV trinh sát và 3 UAV vận tải.' WHERE NOT EXISTS(SELECT 1 FROM rescue_record WHERE event='demo_seed');
COMMIT;
