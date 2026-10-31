# Live TV — kế hoạch firmware đã chốt

Ngày chốt: 27/10/2026. Trạng thái: đã triển khai và kiểm chứng tích hợp; kiểm chứng mở rộng được ghi riêng bên dưới.

Kế hoạch tổng thể: `xiaozhi-kynangso-services/docs/features/live-tv/PLAN.md`.
File này giữ phạm vi firmware và bằng chứng để tiếp tục sau khi đổi phiên.

- [x] Đổi RobotLiveTvPoc/robot_live_tv_poc thành RobotLiveTv/robot_live_tv.
- [x] RobotTvController dùng chung cho lệnh bridge và web; TvWebServer xử lý
  HTTP bất đồng bộ, giới hạn một worker, stack PSRAM.
- [x] Trang /tv: phong cách giải trí, responsive, tìm kênh, thêm/sửa/xóa URL/tên,
  phát/dừng, thông báo tiếng Việt và phân biệt queued/playing/error.
- [x] RPC chung trong DeviceBridgeClient: một yêu cầu chờ, ID, timeout,
  hoàn tất bằng response; không chạy HTTP/catalog trên ssl_receive.
- [x] Tên kênh góc trên phải TFT, rút gọn tên dài; TV không chiếm quyền báo thức.
- [x] Session ID, status thật, retry có giới hạn, dọn player và khôi phục mặt;
  double-tap dừng TV, không tạo pipeline audio riêng.
- [x] README kỹ thuật có sơ đồ đăng ký MCP/query/start/HTTP streaming/stop,
  API, cấu hình, giới hạn và cách kiểm chứng; cập nhật FEATURE_ROADMAP.
- [x] Build batch ESP-IDF 6.1, flash đúng artifact qua UART, kiểm chứng robot
  và backend riêng; giữ NVS, GPIO, board identity và thay đổi không liên quan.
- [x] Kiểm tra nguồn hỏng, CRUD/phát qua web và MCP, chuyển kênh, dừng/mở lại,
  tài nguyên và phát dài hạn. Ghi rõ phần nào chưa xác nhận trên phần cứng.

Profile đã đo: 240×240/12 fps, RX 6/24, BA 6, TCP window 17.280 byte,
Wi-Fi/LwIP và stack TV trong PSRAM. Không đổi codec trong lượt chính thức hóa.

## Bằng chứng triển khai ngày 27/10/2026

- Đã build ESP-IDF 6.1, nạp app qua UART COM6/0x20000, esptool xác nhận hash;
  giữ NVS, assets, GPIO và board identity.
- Đã deploy backend official, danh sách kênh còn nguyên sau recreate container.
- Web robot CRUD/tìm bí danh/version/check nguồn, 10 kênh tên dài khi TV phát,
  MCP search/play/status/stop và web phát lại đã kiểm tra qua thiết bị thật.
- `playing` đã đọc đúng ContinuousStreamPlayer, có audio_position_ms và fps.
- Người dùng xác nhận hình/tiếng ổn; lỗi mất dấu ở nhãn kênh đã sửa bằng font
  Unicode theme cho lv_layer_top, người dùng xác nhận dấu tiếng Việt đã đúng.
- Đã dọn dữ liệu thử riêng, kiểm tra Stop dọn hai tiến trình FFmpeg trên VPS.
- Checklist và số liệu backend: `xiaozhi-kynangso-services/docs/features/live-tv/PLAN.md`.

## Kiểm chứng phần cứng còn mở

- [ ] Soak test liên tục 30–60 phút trên bản cuối; đo RAM/FPS/WSS và lệch A/V.
- [ ] Thao tác touch và chuyển qua báo thức/QR/ảnh/nhạc trong lúc phát TV.

Các mục mở là kiểm chứng vật lý mở rộng, không phải chưa viết implementation.
Khi đổi phiên: đọc README workflow và PLAN này trước; cập nhật bằng chứng tại
đây, không đánh dấu đã đạt chỉ từ kết quả build hoặc ACK queued.

## Bổ sung sau rà soát cuối

- Backend có timeout gửi track 15 giây, gia hạn lease sau khi gửi thành công;
  producer không tự gia hạn khi client đã ngừng nhận. Test client bị treo đã qua.
- Hội thoại web thực tế đã gọi `tv_search` → `tv_play`; robot đợi nói xong rồi
  chuyển radio nội bộ/TV, không chỉ kiểm thử tools/call trực tiếp.
- Host firmware: 129 test, 12 skip. Harness Windows dùng UTF-8, Git Bash native,
  token giả dành riêng cho unit test và đổi cwd trước khi xóa sandbox; không sửa
  source test. Một test Ogg cần compiler C++ host chưa có; 11 skip vốn có của suite.
- Fault injection cố ý dừng backend bị cơ chế tự động chặn (blocked by policy),
  chưa thực hiện. Reconnect WSS sau nạp firmware/deploy đã quan sát được; không
  coi đó là bằng chứng Stop khi mất VPS hay mọi tình huống đứt mạng.

## Chốt lượt triển khai

- Firmware app: 3.269.328 byte; SHA-256
  `e1cc621a4a35b84a87afebeba8a8b89a605daac1d611949b21a380da619d6f46`.
  UART COM6, app offset 0x20000; nạp thành công và verify hash.
- Backend đang dùng image `live-tv-20261027d`; healthz OK, device WSS và
  XiaoZhi MCP Access Point connected sau deploy. Toàn bộ 51 pytest qua.
- Đã quan sát thao tác người dùng bổ sung/chuyển ba kênh qua web. Giữ nguyên
  catalog và lựa chọn của người dùng; không xóa hay khôi phục dữ liệu của họ.
- Các cửa sổ ổn định đạt khoảng 11,6–11,8 fps. Có các lượt chuyển/dừng chủ động;
  bài đo liên tục một kênh 30 phút chưa hoàn tất, không cộng các lượt ngắn để
  tuyên bố đã qua soak. Cần chạy lại khi robot không được thao tác trong lượt đo.
- Chưa xác nhận đồng bộ môi dài hạn hay mọi nguồn IPTV. Preview ảnh, alarm/QR
  và double-tap đã được bảo vệ trong luồng code; kiểm chứng vật lý mở rộng vẫn mở.

## Nghiệm thu bổ sung 30/10/2026

Chủ robot xác nhận Live TV chạy ổn liên tục hơn 25 phút. Kết quả này là quan sát của người dùng trên robot thật; chưa thay thế đo tải nhiều robot hoặc kiểm thử gián đoạn mạng.
