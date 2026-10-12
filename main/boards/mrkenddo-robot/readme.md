# MrKenDDo Robot

Board ESP32-S3 cho XiaoZhi với màn hình TFT ST7789, OLED SSD1306 phụ hiển thị đồng hồ, micro và loa I2S simplex, ba nút bấm, LED và hai motor DC qua hai cầu H của L298N. Build chỉ chọn một board factory qua `DECLARE_BOARD(MrkenDDoRobot)`.

## Phần cứng

| Chức năng | GPIO / cấu hình |
| --- | --- |
| Micro I2S WS/SCK/DIN | 4 / 5 / 6 |
| Loa I2S DOUT/BCLK/LRCK | 7 / 15 / 16 |
| LED / BOOT / âm lượng tăng-giảm | 48 / 0 / 38 / 39 |
| L298N IN1/IN2 (motor A) | 11 / 12 |
| L298N IN3/IN4 (motor B) | 13 / 14 |
| OLED SDA/SCL | 2 / 1; I2C_NUM_1, địa chỉ mặc định 0x3C |
| OLED / TFT | SSD1306 128×32 / ST7789 240×240 |
| TFT CS/DC/RST/MOSI/SCLK/BL | 46 / 3 / 8 / 18 / 17 / 9 |

Xem [`docs/circuit_diagram.png`](docs/circuit_diagram.png). Firmware giả định motor A ở bên trái, motor B ở bên phải; sơ đồ mạch chỉ cho biết cặp dây, không xác nhận vị trí thực tế trên thân robot. Motor B đảo chiều theo mặc định để hai bánh cùng tiến khi bấm Forward. Nếu lắp motor trái/phải khác giả định, dùng hai ô Reverse trên web để hiệu chỉnh. Không đổi GPIO của board hiện có chỉ để hỗ trợ phần cứng khác; board identity ảnh hưởng OTA.

## Điều khiển và web API

Khi Wi-Fi kết nối, địa chỉ IP hiện trên TFT và HTTP server chạy cổng 80. Trang `/` có bốn hướng Forward/Backward/Left/Right, Stop, lựa chọn đảo chiều từng motor, cấu hình OLED và lựa chọn góc xoay TFT. Forward/Backward chạy hai motor cùng chiều logic; Left/Right quay tại chỗ bằng hai motor ngược chiều logic. L298N không có điều tốc PWM trong triển khai này: hai chân EN của mạch phải được enable/jumper. Hết thời gian lệnh, timer tự đưa cả bốn chân IN về mức thấp; Stop cũng tắt ngay.

Tên trường `steps` được giữ để tương thích API cũ nhưng **không phải số bước motor**. Thời gian chạy xấp xỉ `abs(steps) × delay_ms` mili-giây; `delay_ms` là mili-giây mỗi đơn vị, không phải tốc độ quay. Giới hạn `steps` là 1..30000 với lệnh mới, `delay_ms` là 1..100; mỗi lệnh bị giới hạn tối đa 10 giây để an toàn. Trước khi thử trên sàn, nâng robot khỏi mặt đất và dùng thời gian ngắn (ví dụ 200 đơn vị × 1 ms).

- `POST /api/move`: `{ "direction": "forward", "steps": 200, "delay_ms": 1 }`; direction là `forward`, `backward`, `left` hoặc `right`. API cũ `{ "steps": -200 }` vẫn chạy lùi, giá trị dương chạy tiến.
- `POST /api/stop`: dừng cả hai motor.
- `GET/POST /api/config`: cấu hình `steps`, `delay_ms`, `left_inverted`, `right_inverted`; lưu NVS namespace `motor_config`. Các client cũ không gửi hai cờ đảo chiều sẽ giữ giá trị đã lưu.
- `GET/POST /api/oled_config`: địa chỉ, SDA/SCL, width/height và `rotation` (0 hoặc 180 độ); lưu NVS và khởi tạo lại OLED sau khi lưu. `POST /api/oled_test` cũng khởi tạo lại OLED và hiển thị đồng hồ.
- `GET/POST /api/tft_config`: `{ "rotation": 0|90|180|270 }`; 270° tương đương -90°. Lưu NVS và vẽ lại toàn màn hình qua LVGL, giữ sau khi khởi động lại.
- `POST /api/prompt`: `{ "prompt": "..." }`; trả HTTP 202 khi được xếp hàng, 409 nếu thiết bị bận. Firmware mở phiên hội thoại rồi gửi `listen/detect/text` (không gửi MCP); backend phải hỗ trợ văn bản theo giao thức này.
- `GET /api/prompt_status`: trả `idle`, `queued`, `connecting`, `sent`, `responding` hoặc `failed`. `sent` chỉ xác nhận transport đã gửi, chưa xác nhận backend sẽ trả lời.

OLED dùng font bitmap 5×7 cục bộ, hiển thị `HH:MM:SS` mỗi giây; trước khi đồng bộ giờ, hiển thị `--:--:--`. Tùy chọn xoay OLED 180 độ áp dụng khi vẽ pixel, còn TFT xoay qua LVGL/panel. Motor còn có MCP user-only tools `move_motor`, `stop_motor`, `set_motor_speed`; `move_motor` dùng `steps` có dấu để tiến/lùi và `set_motor_speed` giữ tên API cũ nhưng thực chất chỉnh mili-giây mỗi đơn vị.

HTTP chưa có TLS/xác thực. Chỉ dùng trong mạng tin cậy, không forward cổng 80 ra Internet. Body JSON tối đa 2048 byte và cấu hình được kiểm tra trước khi ghi NVS.

## Build và kiểm thử

Board chọn qua `config.json` → `scripts/build.py` → `main/Kconfig.projbuild` → `main/CMakeLists.txt`. Dùng ESP-IDF 6.1 nếu có; tối thiểu 6.0.1, không hỗ trợ 5.x. Trong Windows terminal của dự án, kích hoạt môi trường trước khi build:

```powershell
idf-init
idf.py build
```

Để chọn/build variant từ đầu, sau `idf-init` chạy `python scripts/build.py --list-boards` và `python scripts/build.py mrkenddo-robot --name mrkenddo-robot --language vi-VN`. Script variant có thể thay đổi `sdkconfig` và trạng thái build cục bộ. Có thể dùng `mrkenddo-build-firmware.bat` và `mrkenddo-flash-firmware.bat` sau khi kiểm tra đường dẫn ESP-IDF và cổng COM. Output firmware ở `build/`.

Kiểm tra host/style: `python -m unittest discover -s scripts/tests -v`, `git diff --check` và `clang-format --dry-run -Werror` với các file C/C++ đã sửa. Không sửa thủ công `build/`, `managed_components/`, `components/`, `sdkconfig*` hay file generated/vendor.

## Cần xác nhận trên phần cứng

- Nâng bánh khỏi mặt đất, kiểm tra Forward/Backward và Left/Right với lệnh ngắn, Stop và timeout. Xác nhận motor A thực sự nằm bên trái; nếu không, điều chỉnh hai cờ đảo chiều theo thực tế. Kiểm tra nhiệt độ driver/motor và nguồn cấp riêng cho motor.
- Kiểm tra góc OLED 0/180 và TFT 0/90/180/270, sau khi khởi động lại, cả khi NVS có cấu hình lỗi. TFT 240×240 vẫn vuông ở mọi góc.
- Kiểm tra micro, loa, wake/VAD, phát/ngắt audio, reconnect Wi-Fi/protocol và WebSocket/MQTT-UDP; kiểm tra partition và OTA trên board thật. Build thành công không thay thế kiểm thử phần cứng.
