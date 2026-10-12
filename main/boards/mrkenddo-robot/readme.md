# MrKenDDo Robot

Board tùy biến cho XiaoZhi trên ESP32-S3, gồm màn hình TFT ST7789, OLED SSD1306 phụ, microphone và loa I2S simplex, ba nút bấm, LED và động cơ bước 28BYJ-48 qua mạch L298N. Firmware này chọn đúng một factory board bằng `DECLARE_BOARD(MrkenDDoRobot)`.

## Kiến trúc và chuỗi build

- `config.json` khai báo board `mrkenddo-robot`, chip `esp32s3`, variant `mrkenddo-robot`, flash 8 MB và partition table `partitions/v2/8m.csv`.
- `scripts/build.py` đọc variant, tạo các tùy chọn `sdkconfig`, rồi Kconfig chọn `BOARD_TYPE_MRKENDDO_ROBOT`.
- `main/CMakeLists.txt` ánh xạ symbol này tới thư mục board và biên dịch thêm `mrkenddo_robot.cc`, `robot_mcp_tools.cc` và `web_server.cc`.
- Board dùng các interface chung `WifiBoard`, `Application`, `Display`, `AudioCodec` và `Board`; logic phần cứng riêng nằm trong thư mục này. ESP-IDF 6.1 được khuyến nghị, tối thiểu 6.0.1; ESP-IDF 5.x không được hỗ trợ.

## Phần cứng và chân GPIO

| Chức năng | GPIO/cấu hình |
| --- | --- |
| Microphone I2S: WS/SCK/DIN | 4 / 5 / 6 |
| Loa I2S: DOUT/BCLK/LRCK | 7 / 15 / 16 |
| LED, BOOT, tăng/giảm âm lượng | 48 / 0 / 38 / 39 |
| L298N IN1..IN4 | 11 / 12 / 13 / 14 |
| OLED SDA/SCL, I2C, địa chỉ mặc định | 2 / 1 / I2C_NUM_1 / 0x3C |
| OLED mặc định | 128 x 32, SSD1306 |
| TFT ST7789: CS/DC/RST/MOSI/SCLK/BL | 46 / 3 / 8 / 18 / 17 / 9 |
| TFT | 240 x 240, SPI mode 0, RGB, không mirror/đảo màu |

Không đổi chân của board hiện có để hỗ trợ một phần cứng khác. Hãy tạo variant/board có identity riêng vì board identity ảnh hưởng tương thích OTA.

## Runtime, audio, network và MCP

`MrkenDDoRobot` khởi tạo SPI/TFT, OLED phụ, nút bấm, audio simplex và motor. Âm lượng mặc định là 40%. Khi Wi-Fi kết nối, địa chỉ IP được hiển thị trên TFT và web server được khởi động ở cổng 80.

Motor được điều khiển bởi task FreeRTOS riêng. `Step()` chỉ cập nhật lệnh rồi trả về; lệnh mới thay thế lệnh đang chạy, `Stop()` hủy chuyển động đang chờ/chạy và `SetSpeed()` áp dụng cho cả chuyển động hiện tại. Điều này tránh chặn main task, HTTP task hoặc luồng MCP.

Các MCP user-only tools:

- `move_motor`: `steps` trong khoảng -10000..30000, `delay_ms` trong khoảng 0..100; giá trị 0 dùng tốc độ đã cấu hình.
- `stop_motor` và `set_motor_speed`.
- `send_prompt`, `toggle_chat`, `start_listening`, `stop_listening`.

Prompt được đưa vào `Application::SendMcpMessage()`, vì vậy việc gửi protocol được schedule về application task. Các thay đổi state runtime khác cũng phải đi qua Application/state machine theo quy tắc chung của dự án.

## Web API

Trang điều khiển được nhúng trong firmware tại `/`. Các endpoint JSON là:

- `POST /api/prompt`: `{ "prompt": "..." }`.
- `POST /api/move`: `{ "steps": -512, "delay_ms": 2 }`; `delay_ms` là tùy chọn.
- `POST /api/stop`.
- `GET/POST /api/config`: đọc/lưu số bước và delay motor trong NVS namespace `motor_config`.
- `GET/POST /api/oled_config`: đọc/lưu địa chỉ, SDA/SCL, width/height trong NVS namespace `oled_config`.
- `POST /api/oled_test`: schedule khởi tạo lại OLED và hiển thị đồng hồ.

Body được đọc đủ theo `content_len`, giới hạn 2048 byte; JSON, số nguyên, GPIO, địa chỉ I2C, kích thước OLED và lỗi NVS đều được kiểm tra. HTTP server vẫn là HTTP thường, không có TLS hay xác thực người dùng; firmware chỉ cảnh báo khi khởi động. Chỉ expose cổng này trong mạng tin cậy hoặc tách VLAN/firewall, không forward cổng 80 ra Internet. Thêm authentication/TLS cần một thiết kế credential và API compatibility riêng.

## Thư viện và tài nguyên liên quan

Board sử dụng các component ESP-IDF `esp_http_server`, `nvs_flash`, `esp_lcd`, `esp_driver_i2c`, `esp_driver_spi`, FreeRTOS và cJSON; các interface XiaoZhi xử lý Wi-Fi, audio, display, Application, Protocol và MCP. Sơ đồ mạch nằm trong [`docs/circuit_diagram.svg`](docs/circuit_diagram.svg) và bản PNG tương ứng.

## Build, flash và test

Trong thư mục gốc repository, sau khi nạp ESP-IDF 6.1:

```powershell
python scripts/build.py --list-boards
python scripts/build.py mrkenddo-robot --name mrkenddo-robot --language vi-VN
```

Firmware/merge output nằm trong `build/`; xem `tools/esp32-devtool/README.md` để biết cách nạp image. Có thể dùng hai script ở thư mục gốc sau khi sửa đường dẫn `IDF_EXPORT` và cổng COM nếu cần:

```powershell
.\mrkenddo-build-firmware.bat
.\mrkenddo-flash-firmware.bat
```

Kiểm tra host và style:

```powershell
python -m unittest discover -s scripts/tests -v
git diff --check
clang-format --dry-run -Werror main/boards/common/stepper_motor.h main/boards/common/stepper_motor.cc main/boards/mrkenddo-robot/mrkenddo_robot.cc main/boards/mrkenddo-robot/web_server.cc
```

Nếu đang ở đúng target ESP32-S3, build nhanh bằng `cmake --build build --parallel 2`; canonical `scripts/build.py` có thể thay đổi `sdkconfig` và trạng thái build cục bộ.

## Giới hạn và kiểm thử phần cứng cần làm

- OLED dùng font bitmap 5x7 cục bộ, không cần component hay asset font bên ngoài; task hiển thị đồng hồ `HH:MM:SS` mỗi giây. Trước khi SNTP đồng bộ thời gian, màn hình hiển thị `--:--:--`.
- HTTP không có auth/TLS như nêu trên.
- Cần kiểm tra thực tế đúng thứ tự coil L298N, chiều quay và nhiệt độ motor; xác nhận `Stop()` dừng trong lúc đang chạy.
- Cần thử OLED ở địa chỉ 0x3C với cấu hình mặc định và cấu hình NVS hợp lệ/không hợp lệ; thử mất kết nối/khởi tạo I2C lỗi.
- Cần kiểm tra microphone, loa, wake/VAD, phát audio, ngắt khi nói, reconnect Wi-Fi/protocol và các đường WebSocket/MQTT-UDP phù hợp.
- Cần flash đúng variant trên board thật, kiểm tra TFT/backlight/nút bấm, partition còn đủ chỗ và xác nhận OTA không nhầm board identity.

Không chỉnh sửa thủ công `build/`, `managed_components/`, `components/`, `sdkconfig*` hoặc output generated/vendor.
