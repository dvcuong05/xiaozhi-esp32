# MrKenDDo Robot

Board ESP32-S3 cho XiaoZhi với màn hình TFT ST7789, OLED SSD1306 phụ hiển thị đồng hồ/chữ chạy/trạng thái, micro và loa I2S simplex, ba nút bấm, LED và hai motor DC qua hai cầu H của L298N. Build chỉ chọn một board factory qua `DECLARE_BOARD(MrkenDDoRobot)`.

## Phần cứng

| Chức năng | GPIO / cấu hình |
| --- | --- |
| Micro I2S WS/SCK/DIN | 4 / 5 / 6; INMP441 L/R nối GND, slot trái |
| Loa I2S DOUT/BCLK/LRCK | 7 / 15 / 16 |
| LED / BOOT / âm lượng tăng-giảm | 48 / 0 / 38 / 39 |
| L298N IN1/IN2 (motor A) | 11 / 12 |
| L298N IN3/IN4 (motor B) | 13 / 14 |
| OLED SDA/SCL | 2 / 1; I2C_NUM_1, địa chỉ mặc định 0x3C |
| OLED / TFT | SSD1306 128×32 / ST7789 240×240 |
| TFT CS/DC/RST/MOSI/SCLK/BL | 46 / 3 / 8 / 18 / 17 / 9 |

Xem [`docs/circuit_diagram.png`](docs/circuit_diagram.png) hoặc tại https://app.cirkitdesigner.com/project/68109f8c-2d1c-428a-bdca-f183fb424fa7 Firmware giả định motor A ở bên trái, motor B ở bên phải; sơ đồ mạch chỉ cho biết cặp dây, không xác nhận vị trí thực tế trên thân robot. Motor B đảo chiều theo mặc định để hai bánh cùng tiến khi bấm Forward. Nếu lắp motor trái/phải khác giả định, dùng hai ô Reverse trên web để hiệu chỉnh. Không đổi GPIO của board hiện có chỉ để hỗ trợ phần cứng khác; board identity ảnh hưởng OTA.

## Điều khiển và web API

Khi Wi-Fi kết nối, địa chỉ IP hiện trên TFT và HTTP server chạy cổng 80. Trang `/` là dashboard responsive gồm bốn hướng Forward/Backward/Left/Right, Stop, lựa chọn đảo chiều từng motor, chat qua hàng đợi MCP, cấu hình TFT và nhóm điều khiển OLED. LED Control được cố ý loại khỏi UI theo yêu cầu. Forward/Backward chạy hai motor cùng chiều logic; Left/Right quay tại chỗ bằng hai motor ngược chiều logic. L298N không có điều tốc PWM trong triển khai này: hai chân EN của mạch phải được enable/jumper. Hết thời gian lệnh, timer tự đưa cả bốn chân IN về mức thấp; Stop cũng tắt ngay.

Tên trường `steps` được giữ để tương thích API cũ nhưng **không phải số bước motor**. Thời gian chạy xấp xỉ `abs(steps) × delay_ms` mili-giây; `delay_ms` là mili-giây mỗi đơn vị, không phải tốc độ quay. Giới hạn `steps` là 1..30000 với lệnh mới, `delay_ms` là 1..100; mỗi lệnh bị giới hạn tối đa 10 giây để an toàn. Trước khi thử trên sàn, nâng robot khỏi mặt đất và dùng thời gian ngắn (ví dụ 200 đơn vị × 1 ms).

- `POST /api/move`: `{ "direction": "forward", "steps": 200, "delay_ms": 1 }`; direction là `forward`, `backward`, `left` hoặc `right`. API cũ `{ "steps": -200 }` vẫn chạy lùi, giá trị dương chạy tiến.
- `POST /api/stop`: dừng cả hai motor.
- `GET/POST /api/config`: cấu hình `steps`, `delay_ms`, `left_inverted`, `right_inverted`; lưu NVS namespace `motor_config`. Các client cũ không gửi hai cờ đảo chiều sẽ giữ giá trị đã lưu.
- `GET/POST /api/oled_config`: địa chỉ, SDA/SCL, width/height, `rotation` (0 hoặc 180 độ), `mode` (`0=clock`, `1=marquee`, `2=status`, `3=off`), `text`, `speed`, `font_size`, `contrast` và `invert`; lưu NVS và khởi tạo lại OLED sau khi lưu. `POST /api/oled_message` đổi sang mode chữ chạy và cập nhật nội dung mà không reset I2C; `POST /api/oled_clear` tắt nội dung; `POST /api/oled_test` khởi tạo lại OLED.
- `GET /api/status`: trạng thái Wi-Fi, IP, trạng thái hội thoại và free heap để dashboard cập nhật.
- `GET/POST /api/tft_config`: `{ "rotation": 0|90|180|270 }`. Do màn hình lắp xoay phải 90° và mapping phần cứng ST7789 của `esp_lvgl_port`, dùng 90° để bù và hiển thị đúng chiều. Lưu NVS và vẽ lại toàn màn hình qua LVGL, giữ sau khi khởi động lại.
- `POST /api/prompt`: `{ "prompt": "..." }` (tối đa 1000 byte UTF-8) trả HTTP 202 khi được xếp hàng, 409 nếu thiết bị bận. Đây là cầu nối MCP thử nghiệm cho server tenclass.net: firmware chỉ gửi trigger ngắn `web_chat` qua `listen/detect`, còn câu lệnh đầy đủ chỉ được chuyển khi AI gọi tool `self.web_chat.consume_pending`. Không dùng `detect` để gửi văn bản dài.
- `GET /api/prompt_status`: `queued`, `trigger_sent`, `consumed`, `failed` hoặc `timeout`. `trigger_sent` **không** có nghĩa AI đã nhận câu lệnh; chỉ `consumed` xác nhận tool đã được gọi. Phản hồi TTS vẫn cần thử trên phần cứng và phụ thuộc server/agent; sau 30 giây không gọi tool, câu lệnh bị xóa và trạng thái chuyển `timeout`.

OLED dùng renderer bitmap cục bộ với các mode `clock`, chữ chạy, `status` và `off`; trước khi đồng bộ giờ, đồng hồ hiển thị `--:--:--`. Mode `status` được bố trí hai nửa trên OLED 128×32: đồng hồ `HH:MM:SS` ở dòng trên bên trái, ngày `dd/mm/yy` ở dòng dưới bên trái, cột sóng Wi-Fi ở phía trên bên phải và `IDLE`/`LISTENING`/`SPEAKING` bên dưới. Font 5×7 được giữ cho chữ Latin/số/dấu câu; các dấu tiếng Việt phổ biến được vẽ bổ sung phía trên hoặc dưới glyph. Firmware cũng có một bộ emoji đơn sắc nhỏ (ví dụ 😀 😢 😡 🤖 👍 👎 ❤ ⚠ ✅ ⭐). Unicode ngoài bộ glyph sẽ hiện `?`, emoji phức hợp/đa màu không được hỗ trợ như trên màn hình điện thoại. Tùy chọn xoay OLED 180 độ áp dụng khi vẽ pixel, còn TFT xoay qua LVGL/panel. Motor còn có MCP user-only tools `move_motor`, `stop_motor`, `set_motor_speed`; `move_motor` dùng `steps` có dấu để tiến/lùi và `set_motor_speed` giữ tên API cũ nhưng thực chất chỉnh mili-giây mỗi đơn vị.

HTTP chưa có TLS/xác thực. Chỉ dùng trong mạng tin cậy, không forward cổng 80 ra Internet. Body JSON tối đa 2048 byte và cấu hình được kiểm tra trước khi ghi NVS.

## Nguồn tham khảo cho các tính năng robot

Tham khảo [hudrucan/xiaozhi-desk-robot](https://github.com/hudrucan/xiaozhi-desk-robot) khi phát triển web chat, điều khiển, cảm biến và giao diện robot. Đối chiếu thiết kế và giao thức với upstream XiaoZhi, phần cứng của board này và backend đang dùng trước khi áp dụng; không sao chép GPIO/board identity hay giả định server tùy biến của dự án tham khảo tương thích tenclass.net.

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

## Trạng thái kế hoạch

Các tính năng trong kế hoạch hiện tại đã được hiện thực ở mức mã nguồn: điều
khiển motor và routine qua webserver/MCP, OLED clock/marquee/status, hiển thị
IP khi boot, mắt robot cyan có animation, boot console, audio starting/wake và
hiển thị QR thanh toán trên TFT. QR mặc định giữ trên màn hình 30 giây và có
thể cấu hình thời gian từ 5 đến 120 giây qua MCP.

Các mục còn trong phần “Cần xác nhận trên phần cứng” là kiểm thử thực tế, không
phải tính năng chưa triển khai. Cần flash firmware mới trước khi kiểm tra các
thay đổi trên robot.

## MCP va routine robot

Firmware custom cung cap cac tool MCP de XiaoZhi co the thuc hien truc tiep cac lenh tu nhien:

- `self.robot.move`: di chuyen `forward`, `backward`, `left` hoac `right` theo thoi gian ngan.
- `self.robot.stop`: dung ngay ca hai motor va huy routine dang chay.
- `self.robot.be_lively` chay lap vo han chuoi tien/lui/quay trong task rieng cho den khi goi `self.robot.stop` hoac gui lenh di chuyen moi; `self.robot.dance` chay routine ngan co gioi han.
- `self.robot.set_emotion`: doi mat TFT sang cac emotion custom nhu `happy`, `sad`, `surprised`, `sleepy` hoac `confused`.
- `self.robot.set_oled_mode`: chon `clock`, `marquee`, `status` hoac `off` va luu mode vao NVS.
- `self.robot.show_ip` va `self.robot.get_status`: hien/tra ve IP local va trang thai Wi-Fi, IP, routine.
- `self.robot.show_oled_text`: hien mot cau tuy chon bang marquee tren OLED trong thoi gian tam thoi, khong ghi de noi dung da luu. Vi du: "Hien tren OLED: Xin chao moi nguoi".
- `self.robot.show_payment_qr`: hien ma QR tai khoan ngan hang tren TFT trong thoi gian tam thoi. Vi du: "Hien ma QR thanh toan".

Dashboard web co them nut `Hieu dong`, `Dancing` va `Dung`. Routine chay trong task rieng, tu dong dung sau khi hoan thanh, bi huy boi Stop, hoac bi thay the boi lenh di chuyen thu cong. Hay nang robot khoi mat dat khi test.

Mode status cua OLED hien ngay theo dang `dd-MMM` (vi du `14-Oct`) thay cho `dd/mm/yy`.

> Ma QR thanh toan duoc luu board-local dang bitmap 1-bit 37x37 trong `payment_qr_bitmap.h`, du lieu anh chi khoang 185 byte va khong can thu vien QR runtime. QR hien tren TFT trong 30 giay, sau do tu quay lai giao dien mat robot.
