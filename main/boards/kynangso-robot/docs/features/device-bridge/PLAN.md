# Cấp thông tin xác thực và kết nối nhiều robot

2026-10-31: Đã đổi thương hiệu, định danh board/package và giao thức provision USB (`KNSBRIDGE`) sang KyNangSo. Source và docs đã đồng bộ. Bản này cần nạp firmware và deploy backend đồng thời; kết quả triển khai bên dưới thuộc bản trước.

Ngày chốt: 30/10/2026. Trạng thái: đã deploy VPS, nạp firmware và kiểm chứng một robot thật cùng client giả lập; hai robot vật lý còn chờ.

## Hợp đồng

- Mỗi robot có device_id ổn định và token riêng; tên hiển thị có thể thay đổi.
- Server lưu bản băm token trong SQLite trên volume bền vững, không dùng token chung cho cả nhóm.
- Quản trị qua CLI trên VPS; cấp mã đăng ký ngắn hạn, chỉ dùng một lần.
- Công cụ Windows đổi mã qua HTTPS có xác thực chứng chỉ, rồi truyền thông tin qua USB UART. Không nhập token lâu dài qua HTTP LAN.
- Firmware lưu một bản cấu hình NVS có commit được kiểm tra, ưu tiên cấu hình runtime; fallback cấu hình build để chuyển tiếp robot hiện tại.
- Build mới không bắt buộc có token. Robot chưa cấp thông tin vẫn khởi động các tính năng khác.
- NVS không mặc nhiên bảo vệ trước việc trích xuất flash vật lý; flash/NVS encryption cần đánh giá riêng trước sản xuất.
- Mỗi device_id có một phiên WSS; request/ACK/timeout và dữ liệu TV phải tách theo robot.
- Nếu chưa chọn robot và có nhiều thiết bị khả dụng, trả lỗi yêu cầu chọn; không tự chọn robot đầu tiên.

## Checklist

- [x] Registry SQLite, enrollment, CLI cấp/thu hồi và tương thích legacy.
- [x] WSS nhiều phiên, xác thực và định tuyến theo thiết bị.
- [x] Music/radio/TV và RPC ngược có ngữ cảnh thiết bị.
- [x] Firmware NVS và công cụ provisioning USB UART.
- [x] Docker volume, hướng dẫn triển khai và chuyển đổi.
- [x] Kiểm tra auth, isolation, enrollment replay/expiry: 103 bài backend qua; Compose config hợp lệ; firmware ESP-IDF 6.1 build thành công, app 3.302.624 byte, OTA slot còn khoảng 20%.
- [x] Thử robot thật: provisioning, reboot, reconnect, revoke; xem kết quả triển khai bên dưới.
- [ ] Thử ít nhất hai robot thật đồng thời, xác nhận lệnh không đi nhầm.

## Nghiệm thu Live TV đã có

Chủ robot xác nhận ngày 30/10/2026: Live TV chạy ổn liên tục hơn 25 phút. Đây là kết quả quan sát người dùng, không suy ra tải nhiều robot hoặc mọi trường hợp mất mạng.

## Bằng chứng kiểm tra 30/10/2026

- Backend: 103 pytest qua; Compose fleet config hợp lệ.
- Provisioning PC: 12 unittest qua.
- Firmware: `kynangso-build-firmware.bat` chạy xong exit code 0, compile/link/partition/merge thành công.
- Suite host tổng: 116 qua, 11 lỗi môi trường Windows, 11 skip; không tuyên bố toàn bộ suite qua.
- Chưa deploy VPS, chưa flash hoặc provision robot thật; cần đo lại heap/stack khi TV hoạt động vì UART worker có chi phí thường trực.

## Triển khai và thử nghiệm trên robot 30/10/2026

- VPS image `device-fleet-20261030` healthy; giữ catalog, cấu hình và volume.
- Gia hạn chứng chỉ TLS đã hết hạn và sửa hook renewal; HTTPS được Windows xác minh bằng CA mặc định.
- UART COM6: flash app ở 0x20000, hash esptool xác nhận, giữ NVS/assets/partition.
- Enrollment HTTPS, từ chối replay, NVS commit ACK, reboot và WSS runtime: qua.
- Thu hồi ID thật: WSS biến mất sau khoảng 8 giây; cấp lại/ACK/reboot/kết nối lại: qua.
- Một robot thật và một client giả lập đồng thời: cô lập đích/RPC, từ chối thiếu ID khi có nhiều robot online. Ba ID kiểm tra đã thu hồi và dọn khỏi registry; chưa kiểm chứng hai robot vật lý.
- Catalog 7 kênh giữ nguyên. Kênh Phim Vặt chạy khoảng 60 giây, 10,27–11,82 FPS; audio position tăng, Stop về idle. Người dùng xác nhận hình/tiếng ổn.
- Lượt đầu phát TV ghi nhận SRAM nội bộ tối thiểu 408 byte: chưa chốt headroom. Đang thay stack UART thường trực bằng PSRAM và worker ghi NVS nội bộ tạm thời; cần nạp/đo lại.
- Một số nguồn ngoài không truy cập được: Phim TVB/On Cine/On Kids gặp TLS/ConnectError, Đồng Tháp timeout; không sửa catalog hoặc bỏ xác minh TLS để che lỗi.

## Bản firmware cuối sau đo SRAM

- UART RX dùng stack/buffer dòng PSRAM; task ghi NVS nội bộ chỉ tạo theo lệnh.
- `StreamPlaybackOptions.external_task_stack` mặc định tắt, board chỉ bật cho HTTP audio của TV. Cặp create/delete task theo capability khớp nhau; không đổi các nguồn audio khác.
- Build batch exit 0, app 3.302.960 byte, SHA-256 `449e242f26e8d3adf772bc9f17db1532cce2eedbdc7de3a600a1e900cdee8e60`; flash app COM6 tại 0x20000 và hash xác minh.
- NVS runtime giữ sau flash; UART PING, ba cấu hình lỗi, enrollment mới, ACK/commit/reboot và WSS: qua trên bản tối ưu UART.
- Bản tối ưu TV chạy khoảng 90 giây, 946 frame, rolling FPS 10,14–11,64, audio position 87.640 ms, WSS luôn connected. SRAM nội bộ tối thiểu **12.104 byte**, so với 408–628 byte trước tối ưu. Không ghi nhận reboot/stack overflow/underrun trong lượt đo này; Stop trả về idle.
- IP DHCP hiện tại **192.168.1.6**; không giả định IP này cố định sau reboot.
- Người dùng đã xác nhận hình/tiếng ổn ở lượt kiểm chứng phần cứng trước tối ưu stack. Chưa chạy lại soak 25 phút trên binary cuối, chưa thử hai robot vật lý.
