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
- `GET/POST /api/qr_config`: `{ "payload": "nội dung QR gốc", "caption": "chú thích" }`. Payload tối đa 256 byte UTF-8, caption 96 byte, không nhận ký tự điều khiển. Lưu một QR mặc định trong NVS; payload rỗng chọn lại bitmap ngân hàng ban đầu. GET trả thêm `valid` và `source`; không cache response vì có thể chứa dữ liệu tài khoản.
- `POST /api/qr/show`: `{ "type": "payment|custom|ip", "duration_ms": 30000 }`; `custom` nhận thêm `payload` và `caption`, chỉ hiện tạm, không lưu. `ip` tạo URL `http://<IP hiện tại>/`; `payment` chỉ dùng QR chủ robot đã lưu hoặc bitmap ban đầu. Thời lượng 5–120 giây, mặc định 30; response `scheduled` không phải xác nhận màn hình đã vẽ xong.
- `POST /api/prompt`: `{ "prompt": "..." }` (tối đa 1000 byte UTF-8) trả HTTP 202 khi được xếp hàng, 409 nếu thiết bị bận. Đây là cầu nối MCP thử nghiệm cho server tenclass.net: firmware chỉ gửi trigger ngắn `web_chat` qua `listen/detect`, còn câu lệnh đầy đủ chỉ được chuyển khi AI gọi tool `self.web_chat.consume_pending`. Không dùng `detect` để gửi văn bản dài.
- `GET /api/prompt_status`: `queued`, `trigger_sent`, `consumed`, `failed` hoặc `timeout`. `trigger_sent` **không** có nghĩa AI đã nhận câu lệnh; chỉ `consumed` xác nhận tool đã được gọi. Phản hồi TTS vẫn cần thử trên phần cứng và phụ thuộc server/agent; sau 30 giây không gọi tool, câu lệnh bị xóa và trạng thái chuyển `timeout`.

OLED dùng renderer bitmap cục bộ với các mode `clock`, chữ chạy, `status` và `off`; trước khi đồng bộ giờ, đồng hồ hiển thị `--:--:--`. Mode `status` được bố trí hai nửa trên OLED 128×32: đồng hồ `HH:MM:SS` ở dòng trên bên trái, ngày `dd/mm/yy` ở dòng dưới bên trái, cột sóng Wi-Fi ở phía trên bên phải và `IDLE`/`LISTENING`/`SPEAKING` bên dưới. Font 5×7 được giữ cho chữ Latin/số/dấu câu; các dấu tiếng Việt phổ biến được vẽ bổ sung phía trên hoặc dưới glyph. Firmware cũng có một bộ emoji đơn sắc nhỏ (ví dụ 😀 😢 😡 🤖 👍 👎 ❤ ⚠ ✅ ⭐). Unicode ngoài bộ glyph sẽ hiện `?`, emoji phức hợp/đa màu không được hỗ trợ như trên màn hình điện thoại. Tùy chọn xoay OLED 180 độ áp dụng khi vẽ pixel, còn TFT xoay qua LVGL/panel. Motor còn có MCP user-only tools `move_motor`, `stop_motor`, `set_motor_speed`; `move_motor` dùng `steps` có dấu để tiến/lùi và `set_motor_speed` giữ tên API cũ nhưng thực chất chỉnh mili-giây mỗi đơn vị.

HTTP chưa có TLS/xác thực. Chỉ dùng trong mạng tin cậy, không forward cổng 80 ra Internet. Body JSON tối đa 8192 byte; riêng media giới hạn 48 dòng lyric, mỗi dòng tối đa 96 byte UTF-8. Cấu hình NVS vẫn được kiểm tra trước khi ghi.

## Nguồn tham khảo cho các tính năng robot

Tham khảo [hudrucan/xiaozhi-desk-robot](https://github.com/hudrucan/xiaozhi-desk-robot) khi phát triển web chat, điều khiển, cảm biến và giao diện robot. Đối chiếu thiết kế và giao thức với upstream XiaoZhi, phần cứng của board này và backend đang dùng trước khi áp dụng; không sao chép GPIO/board identity hay giả định server tùy biến của dự án tham khảo tương thích tenclass.net.

## Build và kiểm thử

Board chọn qua `config.json` → `scripts/build.py` → `main/Kconfig.projbuild` → `main/CMakeLists.txt`. Dùng ESP-IDF 6.1 nếu có; tối thiểu 6.0.1, không hỗ trợ 5.x. Trong Windows terminal của dự án, kích hoạt môi trường trước khi build:

```powershell
idf-init
idf.py build
```

Lệnh build kiểm tra bản refactor, giữ wake word Jarvis đang được chọn:

```powershell
idf-init
python scripts/build.py mrkenddo-robot --name mrkenddo-robot --language vi-VN --wake-word wn9_jarvis_tts
```

`--language` và `--wake-word` là lựa chọn khi build; không đưa chúng vào
`config.json` của board. Build thành công chưa thay thế kiểm tra trên robot.

Để chọn/build variant từ đầu, sau `idf-init` chạy `python scripts/build.py --list-boards` và `python scripts/build.py mrkenddo-robot --name mrkenddo-robot --language vi-VN`. Script variant có thể thay đổi `sdkconfig` và trạng thái build cục bộ. Có thể dùng `mrkenddo-build-firmware.bat` và `mrkenddo-flash-firmware.bat` sau khi kiểm tra đường dẫn ESP-IDF và cổng COM. Output firmware ở `build/`.

Kiểm tra host/style: `python -m unittest discover -s scripts/tests -v`, `git diff --check` và `clang-format --dry-run -Werror` với các file C/C++ đã sửa. Không sửa thủ công `build/`, `managed_components/`, `components/`, `sdkconfig*` hay file generated/vendor.

Test nhận diện nhạc/lyrics nằm tại `tests/music_session_test.cc`, chạy trên
host bằng C++23 cùng `robot_music_session.cc` (không được compile vào firmware).
Test replay marker/tên bài/lời trong log tenclass, giới hạn UTF-8 và reset sau
khi kết thúc bài. Khi thêm file `.cc` mới vào board, chạy `idf.py reconfigure`
trước build vì danh sách source board dùng GLOB ở thời điểm cấu hình.

## Cần xác nhận trên phần cứng

- Nâng bánh khỏi mặt đất, kiểm tra Forward/Backward và Left/Right với lệnh ngắn, Stop và timeout. Xác nhận motor A thực sự nằm bên trái; nếu không, điều chỉnh hai cờ đảo chiều theo thực tế. Kiểm tra nhiệt độ driver/motor và nguồn cấp riêng cho motor.
- Kiểm tra góc OLED 0/180 và TFT 0/90/180/270, sau khi khởi động lại, cả khi NVS có cấu hình lỗi. TFT 240×240 vẫn vuông ở mọi góc.
- Kiểm tra micro, loa, wake/VAD, phát/ngắt audio, reconnect Wi-Fi/protocol và WebSocket/MQTT-UDP; kiểm tra partition và OTA trên board thật. Build thành công không thay thế kiểm thử phần cứng.

## Trạng thái kế hoạch

Các tính năng trong kế hoạch hiện tại đã được hiện thực ở mức mã nguồn: điều
khiển motor và routine qua webserver/MCP, OLED clock/marquee/status, hiển thị
IP khi boot, mắt robot cyan có animation, boot console, audio starting/wake,
Audio Visualizer theo PCM playback và hiển thị QR thanh toán trên TFT. QR mặc
định giữ trên màn hình 30 giây và có thể cấu hình thời gian từ 5 đến 120 giây
qua MCP. Visualizer hiển thị phổ tần FFT thật, chỉ khi nhạc/radio có PCM thực
sự phát ra loa; không bật vì boot sound, beep, thông báo hoặc giọng chatbot.
Màn hình chatbot giữ mắt cyan và miệng đóng/mở theo mức âm thanh TTS; miệng
tự khép khi im lặng hoặc chuyển sang listening/idle. Đây là animation theo
âm lượng, không phải lip-sync theo âm vị. Nhạc tenclass qua tool `play_music`
còn hiển thị tên bài và lời theo từng câu server gửi; metadata tác giả chưa
có trong payload hiện tại. Giao diện visualizer dùng palette
gradient nhiều màu trên nền midnight; màu cyan chỉ giữ cho robot eyes và các
UI đã được thiết kế riêng theo nhận diện của robot.

Các mục còn trong phần “Cần xác nhận trên phần cứng” là kiểm thử thực tế, không
phải tính năng chưa triển khai. Cần flash firmware mới trước khi kiểm tra các
thay đổi trên robot.

## MCP va routine robot

Firmware được tổ chức theo trách nhiệm, tập trung các phần riêng của sản phẩm
trong thư mục board này. `mrkenddo_robot.cc` là nơi khởi tạo phần cứng và nối
các module; các tính năng không đọc ngược vào board qua biến global.

| Module | Trách nhiệm |
| --- | --- |
| `robot_controller.*` | Một đường xử lý chung cho lệnh HTTP và MCP; kiểm tra yêu cầu, lưu cấu hình rồi chuyển việc cập nhật UI/phần cứng sang task phù hợp. |
| `robot_settings.*` | Nguồn duy nhất cho schema, giá trị mặc định, validation và migration của NVS motor/OLED/TFT/âm thanh boot. |
| `secondary_display.*` | Sở hữu OLED SSD1306, I2C, framebuffer, mutex, task cập nhật và nội dung IP/boot/clock/marquee/status. |
| `oled_display_controller.*`, `oled_clock_font.h` | Vẽ bitmap, ký tự tiếng Việt và emoji lên framebuffer; không đọc NVS hoặc xử lý HTTP/MCP. |
| `web_server.*`, `robot_mcp_tools.*` | Adapter nhận đầu vào, kiểm tra JSON/tham số và gọi controller; giữ tên route/tool hiện có. |
| `web_chat_bridge.*` | Giữ tối đa một prompt chờ, trigger `web_chat`, trạng thái consume và timeout 30 giây. |
| `mrkenddo_robot_display.*`, `robot_eyes.*` | Điều phối màn TFT và vẽ mắt/boot console/QR/visualizer. |
| `robot_spectrum.*` | FFT 512 điểm, Hann window, loại DC, 12 dải bass→treble và mức RMS điều khiển miệng; dùng lại `espressif/dl_fft` có sẵn. |
| `robot_music_session.*` | Nhận diện marker tool nhạc tenclass trong `sentence_start`, giữ tên bài/lời hiện tại/dòng trước với giới hạn UTF-8; không thay giao thức hay đoán nhạc từ nội dung người dùng. |
| `robot_qr_code.*` | Mã hóa text/URL thành QR động bằng Nayuki có sẵn trong LVGL, kiểm tra UTF-8, giữ ma trận module 1-bit; không tải ảnh hoặc tạo thông tin ngân hàng. |
| `robot_drive.*` | Điều khiển motor, timeout và routine; nhận cấu hình từ controller thay vì tự đọc NVS. |

Luồng đổi chế độ OLED từ web và từ giọng nói đều là
`HTTP/MCP → RobotController → RobotSettings → Application::Schedule → SecondaryDisplay`.
NVS được lưu trước khi báo đã nhận cấu hình; việc khởi tạo lại panel chạy ở
main task và được tuần tự với task OLED bằng mutex. Phản hồi “scheduled” xác
nhận yêu cầu đã được nhận, không xác nhận panel đã cập nhật xong. Văn bản OLED
tạm thời chỉ thay nội dung runtime, không ghi đè marquee đã lưu.

Các namespace/key NVS `motor_config`, `oled_config`, `tft_config` và
`boot_sequence` được giữ nguyên, gồm migration góc TFT cũ và âm lượng boot cũ.
QR mới dùng namespace `qr_config`, key `record`: một blob version 1 chứa
payload/caption để không ghép nhầm payload mới với caption cũ khi lỗi ghi.
Không có record thì dùng bitmap gốc; record lỗi/schema lạ thì từ chối hiện
QR thanh toán thay vì âm thầm đổi người nhận. Dữ liệu NVS/HTTP không được
mã hóa bởi tính năng này; chỉ dùng LAN tin cậy, không forward cổng 80.
Tên Wi-Fi provisioning/hostname `MrKenDDo-Robot` do board cung cấp qua hook;
các board Wi-Fi khác vẫn dùng mặc định của XiaoZhi.

TFT chỉ chọn một chế độ từ trạng thái hiện tại, theo thứ tự ưu tiên
`QR thanh toán → boot console → audio visualizer → robot eyes`. QR hết hạn
khôi phục chế độ phù hợp lúc đó; status/emotion không tự giành quyền hiển thị.
Visualizer bị chặn trong giai đoạn boot. Core chuyển cửa sổ PCM mới nhất qua
mailbox có kích thước cố định, cập nhật UI tối đa 25 lần/giây; không xếp thêm
một callback vào hàng đợi main task cho từng PCM frame. Timer one-shot chỉ
được hẹn khi có frame chờ; lúc idle không có timer visualizer chạy định kỳ.
Các display khác không đăng ký capability visualizer sẽ không nhận callback
PCM phục vụ tính năng này.

Tham khảo cách `develop_vn` gắn spectrum với vòng đời media player, nhưng
không nhập nguyên player hay tạo task FFT riêng. Packet được gắn nguồn nội
bộ `Conversation`/`Effect`/`Notification`/`Media`, giữ qua bước decode/resample;
định dạng WebSocket và MQTT/UDP không đổi. Player chung nhận radio qua
`radio.start`/`POST /api/radio/start`, còn track qua `media.play` hoặc
`POST /api/media/play` với metadata và subtitles có mốc thời gian. `/api/notify`
vẫn dành cho thông báo; firmware không chứa provider, station ID hay logic Zing.
MCP sở hữu tìm kiếm, resolve URL, chuyển đổi Ogg Opus và LRC; Zing client
Python được tích hợp ngay trong MCP server. Radio dùng
`radio_search` cùng lifecycle `radio.start`/`radio.stop` riêng. Track có thể
được thay hoặc dừng bằng `media.play`/`media.stop` và chờ hội thoại hiện tại về
idle trước khi bắt đầu. Giới hạn firmware là tối đa 48 dòng lyric, 96 byte mỗi
dòng và body HTTP 8 KiB.
Luồng tenclass tích hợp có ngoại lệ quan trọng: `search_music`/`play_music`
phát nhạc qua cùng transport TTS, nên state machine vẫn là `speaking`. Custom
board nhận diện đúng marker assistant `% play_music...` (không phải
`search_music` hay câu nói nhắc tên tool); chỉ khi có PCM mới bật spectrum,
TFT hiện `Music`, không đổi state sang radio. Tên bài dạng `《...》` đi vào dòng
tiêu đề; các `sentence_start` tiếp theo đi vào vùng lyrics, giữ thêm một dòng
trước. Khi tạm thiếu PCM vẫn giữ session/lời; listening/idle/error/boot mới
xóa session, tránh TTS kế tiếp bị coi nhầm là nhạc. Marker `stop_music` và
`pause_music` cũng xóa session. Đây là adapter theo marker hiện có của backend,
không phải phân loại giọng/nhạc bằng DSP; nếu backend đổi marker hoặc không
gửi marker, cần metadata media rõ ràng thay vì đoán từ thời lượng/tiếng Trung.

Lyrics dùng font Noto hiện tại và glyph push có sẵn, được cập nhật khi server
gửi từng câu (không phải karaoke theo từ hay đồng bộ chính xác đồng hồ PCM).
Không tải LRC trùng, không lưu cả bài: title tối đa 192 byte UTF-8, mỗi dòng
lời tối đa 384 byte, giữ hai dòng; cắt tại biên ký tự. Ký tự ngoài font vẫn cần
glyph do server cung cấp. QR tiếp tục ưu tiên cao hơn; trong lúc QR hiện, lời
vẫn cập nhật để khi QR hết hạn hiển thị nội dung mới nhất.

PCM media hoặc nhạc tenclass đã nhận diện bật spectrum; PCM hội thoại thường
chỉ điều khiển miệng. Audio task
copy tối đa 512 mẫu mono (1 KiB); FFT/render chạy trên main task tối đa 25 FPS,
không cấp phát mỗi frame. FFT theo sample rate thực, 12 dải 40 Hz–16 kHz bị
giới hạn bởi Nyquist; ở 24 kHz độ phân giải khoảng 47 Hz. Attack nhanh, decay
mượt và thang dBFS cố định tránh khuếch đại nhiễu khi im lặng. Không phân tích
micro, không tải thêm model, và không thay pipeline âm thanh hội thoại.

Core vẫn sở hữu state machine và quyền sử dụng audio. `BootEvent`, hook `Board`
và hook `Display` là hợp đồng dùng chung; chuỗi thương hiệu, âm thanh, cấu hình
boot và cách trình bày do robot cung cấp. Module radio/notify generic không
phụ thuộc GPIO, QR, OLED hay lớp board cụ thể. Các file C/C++ của robot hiện
được giữ trực tiếp trong thư mục board để phù hợp cơ chế chọn source của CMake.

Firmware custom cung cap cac tool MCP de XiaoZhi co the thuc hien truc tiep cac lenh tu nhien:

- `self.robot.move`: di chuyen `forward`, `backward`, `left` hoac `right` theo thoi gian ngan.
- `self.robot.stop`: dung ngay ca hai motor va huy routine dang chay.
- `self.robot.be_lively` chay lap vo han chuoi tien/lui/quay trong task rieng cho den khi goi `self.robot.stop` hoac gui lenh di chuyen moi; `self.robot.dance` chay routine ngan co gioi han.
- `self.robot.set_emotion`: doi mat TFT sang cac emotion custom nhu `happy`, `sad`, `surprised`, `sleepy` hoac `confused`.
- `self.robot.set_oled_mode`: chon `clock`, `marquee`, `status` hoac `off` va luu mode vao NVS.
- `self.robot.show_ip` va `self.robot.get_status`: hien/tra ve IP local va trang thai Wi-Fi, IP, routine.
- `self.robot.show_oled_text`: hien mot cau tuy chon bang marquee tren OLED trong thoi gian tam thoi, khong ghi de noi dung da luu. Vi du: "Hien tren OLED: Xin chao moi nguoi".
- `self.robot.show_payment_qr`: hien ma QR tai khoan ngan hang tren TFT trong thoi gian tam thoi. Vi du: "Hien ma QR thanh toan".
- `self.network.ip2qrcode`: hiện QR mở webserver theo IP Wi-Fi hiện tại, không thay QR ngân hàng.
- `self.robot.show_qr`: hiện QR URL/text tạm thời, nhận `payload`, `caption` và `duration_ms`; không lưu. Yêu cầu thanh toán phải dùng `show_payment_qr`, không suy đoán tài khoản từ chatbot. Cấu hình QR ngân hàng chỉ đổi qua web của chủ robot.

Music va radio duoc MCP tach thanh hai flow: music dung `music_search`/`music_play`,
con radio dung `radio_search`/`radio_start`. Firmware khong chua provider, station ID
hay logic Zing; player chung nhan URL da duoc resolve qua `media.play` hoac `radio.start`.
Track co metadata va timed subtitles, co the stop bang `media.stop`; radio co API
`/api/radio/start|stop|status` va reconnect stream. Yeu cau media den luc robot ban
duoc giu mot slot toi khi conversation ve `idle`; lenh moi nhat thay the lenh dang cho.

Dashboard web co them nut `Hieu dong`, `Dancing` va `Dung`. Routine chay trong task rieng, tu dong dung sau khi hoan thanh, bi huy boi Stop, hoac bi thay the boi lenh di chuyen thu cong. Hay nang robot khoi mat dat khi test.

Mode status cua OLED hien ngay theo dang `dd-MMM` (vi du `14-Oct`) thay cho `dd/mm/yy`.

> Bitmap 1-bit 37x37 gốc trong `payment_qr_bitmap.h` (185 byte) vẫn là mặc định
> dự phòng khi chưa cấu hình QR mới. QR động dùng bộ mã hóa Nayuki có sẵn
> trong LVGL, bật bằng `CONFIG_LV_USE_QRCODE=y` ở variant board. Ma trận tối đa
> 65x65 chiếm khoảng 0,6 KiB, buffer mã hóa tạm khoảng 1 KiB; không tạo canvas
> RGB565 240x240 (~112 KiB). Renderer vẽ đen/trắng, scale nguyên ít nhất 2 px/module,
> quiet zone 4 module và caption tùy chọn nằm ngoài vùng QR. Hết hạn sẽ về
> đúng boot/nhạc/mắt hiện tại, không nhất thiết luôn về mắt.

Dashboard có mục "QR thanh toán & chia sẻ": dán **nội dung gốc đọc từ QR**
(ví dụ chuỗi VietQR/EMV hoặc URL), nhập caption rồi xem thử và quét kiểm tra
người nhận/số tiền trước khi lưu làm mặc định. Đây không phải upload ảnh PNG,
không nhận link ảnh để tự tải/giải mã, không tự sửa checksum/tài khoản/số tiền.
Xóa payload và lưu để trở lại bitmap cũ. QR tạm hoặc QR IP không ghi đè QR đã lưu.
Không đưa dữ liệu QR ngân hàng vào log. Test host nằm ở `tests/qr_code_test.cc`;
còn cần quét trên TFT thật, thử xoay màn hình, reboot, renew timeout và QR
hết hạn trong lúc nhạc/boot vẫn chạy.

Đối chiếu `develop_vn`: module `features/QRCode` tạo QR từ text/URL và tool
`self.network.ip2qrcode` hiện URL IP; checkout tham khảo chưa có cấu hình
ngân hàng trong module này. Mượn khả năng QR động và caption, nhưng giữ
renderer gọn/overlay priority/thời gian quét của robot thay vì nhập canvas
RGB565 toàn màn hình, singleton và static callback context của họ.
