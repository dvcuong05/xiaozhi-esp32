# MrKenDDo Robot Feature Roadmap

Đây là checklist triển khai cho firmware custom `mrkenddo-robot`. Các mục
trong kế hoạch hiện tại đã được hiện thực; phần còn lại bên dưới chỉ là kiểm
thử trên phần cứng và các ý tưởng mở rộng trong tương lai.

## Working rules

- [ ] Chạy `idf-init` trước mỗi lần build hoặc flash firmware.
- [ ] Giữ thay đổi dành riêng cho robot trong `main/boards/mrkenddo-robot/`
      nếu tính năng không cần chia sẻ cho board khác.
- [ ] Build trước khi flash và ghi rõ giới hạn của kiểm thử phần cứng.
- [ ] Cập nhật file này và `readme.md` khi trạng thái tính năng thay đổi.

## Planned features
- [x] Nâng cấu hình N16R8 lên flash 16 MiB: CSV riêng của board, NVS 64 KiB,
      hai OTA slot 3,9375 MiB và assets 8 MiB; hướng dẫn chuyển lần đầu qua
      USB/serial, backup flash và dọn phần mở rộng NVS. Cần xác nhận giữ cấu hình,
      boot/assets và OTA/rollback trên robot thật.
- [x] MicroSD đợt 1: SPI2 CS42/SCK41/MOSI40/MISO39; GPIO39 thay nút giảm âm lượng theo xác nhận chủ robot. Module đã nhận thẻ khi VCC được chuyển sang 5 V; mount ở worker, lỗi/thiếu thẻ không chặn boot, không tự format.
- [x] MicroSD đợt 2: trang `/storage` có trạng thái/dung lượng, danh sách phân trang UTF-8, mkdir, upload/download giới hạn, xóa file và thư mục đệ quy có xác nhận, hủy upload và tháo thẻ an toàn; thêm volume 0–100 trên dashboard. Hoàn tất ở mức source, còn kiểm tra thẻ thật.
- [x] MicroSD đợt 3: expose `sd_status`, `sd_search`, `sd_play`, `sd_stop` qua MCP service/device WSS; tìm MP3 theo ID3v2 title/artist/album/genre và fallback folder/file, phát bằng player local dùng chung. Quét/kết quả có giới hạn, không tạo catalog trên thẻ.
- [x] Audio Visualizer trên TFT: chỉ nhạc/radio đang phát PCM mới chuyển sang
    spectrum FFT 512 điểm, 12 dải tần với palette nhiều màu trên nền midnight;
    không bật vì boot/beep/thông báo/TTS. Dừng media thì khôi phục mắt robot.
- [x] Chatbot speaking giữ mắt cyan và thêm miệng đóng/mở theo mức TTS,
    tự khép khi im lặng; emotion không làm mất animation miệng.
- [x] Nhạc tenclass qua `play_music` trên transport TTS vẫn hiện spectrum;
    TFT có tên bài, lời hiện tại và dòng trước từ `sentence_start`. Không
    tải thêm LRC, không lưu cả bài; giữ QR priority và reset khi hết session.
- [x] QR địa chỉ IP webserver qua `self.network.ip2qrcode` và dashboard.
- [x] Tích hợp music MCP theo provider: Zing MP3 migrate vào client Python
    trong MCP server; radio-browser là provider/tool riêng. Firmware nhận URL
    đã chuẩn hóa; deployment không cần repo hay service API Zing chạy riêng.
    Track dùng `media.play` với metadata/LRC có giới hạn, `media.stop` dừng
    track; radio vẫn có lifecycle riêng `radio.start`/`radio.stop`.
- [x] Dừng nhạc online/SD hoặc radio đang phát và trở về `idle` bằng double-tap cảm biến TTP223.
- [ ] Kết nối API và hiện camera giao thông HCM
- [ ] Stream live television and support config from webserver
- [ ] Feature ask chatbox to set and weak the alarm
- [ ] Tạm hoãn theo yêu cầu: chuyển bài và pause bằng TTP223; dừng nhạc/radio bằng double-tap đã triển khai ở trên.
- [ ] Linh hoạt hơn việc flash firmware với XIAOZHI_MCP_DEVICE_WSS_TOKEN, có thể nhận từ webserver robot đc không? hoặc qua tool nạp firmware dyanmic chẳng hạn

### Ý tưởng mở rộng từ trao đổi về mạch robot (18/10/2026)

Ghi chú: đã đối chiếu câu hỏi và câu trả lời trong bản export của session
ChatGPT. Đây là backlog để bạn chọn dần, không có nghĩa mọi mục đã được chốt
để triển khai.

- [ ] **P0 - Khảo sát phần cứng trước khi lập trình:** xác nhận module
      microSD, chân còn dùng được và xung đột bus với TFT; kiểm tra mức logic,
      nguồn cấp và định dạng thẻ. Xác nhận mạch TTP223, VL53L1X, bộ phát/thu IR,
      loại đèn Edison và driver. Với thông tin 3 V/120 mA, chưa được giả định
      LED là addressable hay 120 mA cho từng đèn/cả cụm; không cấp dòng tải từ
      GPIO ESP32.
- [x] **P1 - Nền tảng lưu trữ microSD:** `SdStorage` sở hữu SPI2/FatFs,
      mount/unmount, trạng thái lỗi, truy cập file giới hạn và đồng bộ vòng đời.
      Async HTTP worker phục vụ quản lý file, không chặn audio/main loop.
      Không có chân card-detect: nhận lỗi khi I/O, cần remount thủ công; không
      cam kết hot-plug. Kiểm thử nguồn/thẻ thật còn mở.
- [x] **P1 - Phát nhạc và âm thanh offline:** duyệt/phát file local qua player
      dùng chung; thiết kế Audio Manager để ưu tiên TTS, nhạc, hiệu ứng và tránh
      tranh chấp I2S/loa. `/storage` phát `.mp3`, `.ogg`, `.opus` qua cùng
      `NotifyPlayer`/`AudioStreamDecoder` như HTTP. MP3 dùng streaming decoder
      Espressif, trộn stereo về mono/resample, giữ visualizer và timestamp subtitle.
      Thẻ được giữ mở khi phát; thao tác ghi/xóa/tải/tháo bị từ chối. MCP dùng
      passthrough cho MP3 nguồn hợp lệ, Opus cho codec khác/HLS hoặc đoạn cắt có
      giới hạn thời gian. Không tạo player riêng hoặc đưa provider vào firmware.
      Build ESP-IDF 6.1 đã qua; app còn 91.328 byte (~3%). Chưa có playlist/tự
      chuyển bài hoặc WAV; nghe loa, heap/CPU, lyrics và ngắt phát trên robot
      thật vẫn cần kiểm chứng.
- [x] **P1 - Tương tác TTP223:** GPIO20, chạm đơn luân phiên biểu cảm mắt kèm
      âm báo ngắn và lưu biểu cảm làm mặc định idle trong NVS khi robot rảnh
      (không tự chạy bánh xe), chạm đôi bắt đầu hội thoại, hủy phiên nghe hoặc
      ngắt lời để chuyển sang nghe thủ công tùy trạng thái; nhấn giữ dừng
      motor/routine/nhịp cảm xúc ở mọi trạng thái. Dùng debounce và nhận diện
      chạm đơn/đôi của thư viện nút bấm; tắt USB Serial/JTAG phụ của board.
      Chưa kiểm chứng trên robot thật.
- [ ] Âm thanh local và phản ứng chuyển động theo chạm: chỉ xem xét sau khi
      thống nhất âm báo và có điều kiện an toàn cho bánh xe; không tự bật mặc định.
- [ ] **P1 - Tránh vật cản bằng VL53L1X:** đọc ToF theo chu kỳ, dừng an toàn khi
      cảm biến lỗi hoặc dữ liệu quá cũ; tích hợp vào routine khám phá/di chuyển
      để giảm tốc, dừng hoặc đổi hướng. Ngưỡng phải hiệu chỉnh trên robot thật.
- [ ] **P1 - Ngôn ngữ ánh sáng cho ba đèn sau:** quy ước trạng thái khởi động,
      ready/listening/processing/speaking, vui khi chạm, di chuyển/rẽ, vật cản,
      dừng khẩn cấp, pin yếu và lỗi. Tách hiệu ứng khỏi state machine; xác nhận
      driver, PWM và tổng dòng trước khi bật hiệu ứng dài hoặc theo nhạc.
- [ ] **P1 - Điều khiển thiết bị bằng IR:** thêm tool/lệnh tổng quát và phát mã
      IR; bắt đầu với các lệnh TV Samsung phổ biến, đồng thời ghi rõ giới hạn
      của remote Smart đời mới có thể cần Bluetooth/SmartThings cho một số lệnh.
      Nếu cần học remote, bổ sung module thu tương thích và lưu mã đã học trên
      microSD; kiểm tra tương thích model trước khi mở rộng sang máy lạnh.
- [ ] **P2 - SmartThings qua MCP/backend:** sau khi xác nhận TV đã được thêm và
      điều khiển được trong SmartThings, tích hợp qua service MCP ở
      `xiaozhi-kenddo-services`; backend giữ credential và gọi API, ESP32 chỉ
      gửi tool request/nhận kết quả. Không lưu token SmartThings trên robot.
      IR và SmartThings là hai đường điều khiển riêng, không giả định thay thế
      hoàn toàn cho nhau.
- [ ] **P2 - Hồ sơ robot và tài nguyên trên thẻ:** cấu hình JSON có validate
      cho tốc độ, ngưỡng cảm biến, âm lượng và chế độ; nạp âm thanh cảm xúc,
      biểu cảm/animation OLED và tính cách từ file nhẹ, có fallback khi thiếu
      hoặc hỏng. Không lưu Wi-Fi token, SmartThings token hay bí mật dạng rõ.
- [ ] **P2 - Nhật ký và dữ liệu offline:** ghi sự kiện lỗi/reset, mất Wi-Fi,
      dừng motor khẩn cấp và thống kê cảm biến để chẩn đoán; có giới hạn dung
      lượng/ghi luân phiên. Không lưu hội thoại hoặc ghi âm mặc định; nếu thêm
      ghi âm hay bộ nhớ hội thoại thì cần bật chủ động, kiểm soát thời hạn và
      cung cấp cách xóa dữ liệu.
- [x] **P2 - Xem ảnh từ microSD trên TFT:** `/robot/images/` làm root cố định,
      thư mục con là album. Index lazy trên PSRAM tối đa 256 ảnh, làm mới sau thay
      đổi file/mount; MCP hỗ trợ tìm ảnh, hiện JPEG, trình chiếu ngẫu nhiên có
      giới hạn và dừng. Uploader web tối ưu ảnh ngay trên trình duyệt (đầu vào tối
      đa 5 MiB, cạnh dài 480 px, JPEG đầu ra tối đa 512 KiB); firmware vẫn giữ
      giới hạn giải mã 512 KiB/512 × 512 và giải mã ngoài task giao diện. Cần xác
      nhận upload/hiển thị trên TFT thật.
- [ ] **P3 - Cầu nối loa Bluetooth:** chỉ làm prototype nếu thực sự cần loa BT;
      dùng một ESP32 có Bluetooth Classic/A2DP làm co-processor nhận PCM từ S3
      (đánh giá I2S trước), đo độ trễ và xử lý audio focus/reconnect. ESP32-S3
      hiện tại không tự đảm nhiệm A2DP Classic.
- [ ] **P3 - Laser chỉ điểm có kiểm soát:** xác minh class/công suất, bước sóng,
      driver và hướng lắp trước. Mặc định tắt, lệnh bật có chủ ý và timeout;
      chỉ chiếu cố định xuống mặt sàn/bề mặt an toàn, không quét tự động vào
      người, thú cưng hay bề mặt phản quang. Dùng LED chiếu điểm thay laser cho
      trò chơi với thú cưng.
## Completed implementation

- [x] Refactor kiến trúc firmware: board khởi tạo phần cứng, HTTP/MCP cùng
      gọi `RobotController`, NVS tập trung trong `RobotSettings`, OLED có
      service riêng sở hữu I2C/framebuffer/task; bỏ C wrapper và global board.
- [x] Tập trung quyền hiển thị TFT theo thứ tự QR → boot → visualizer → eyes;
      QR hết hạn quay về chế độ phù hợp với playback/boot hiện tại.
- [x] Chuyển mẫu visualizer qua mailbox cố định, giới hạn UI 25 FPS và xử lý
      playback stopped; bỏ callback xếp hàng theo từng PCM frame.
- [x] Core dùng boot config do board cung cấp; giữ branding, defaults/migration
      NVS và tên Wi-Fi provisioning của MrKenDDo trong board riêng.
- [x] Board custom, TFT ST7789, OLED SSD1306, robot drive, microphone,
      speaker, eyes và web server được tách dưới `mrkenddo-robot`.
- [x] Web dashboard điều khiển Forward/Backward/Left/Right, Stop, đảo
      chiều từng motor, cấu hình TFT/OLED và gửi prompt qua MCP queue.
- [x] Hai routine chuyển động: `Hiếu động` chạy lặp vô hạn tới khi Stop hoặc
      có lệnh mới; `Dancing` chạy routine ngắn có giới hạn.
- [x] MCP điều khiển chuyển động, Stop, routine, emotion, OLED mode, IP,
      status và văn bản tạm thời trên OLED.
- [x] OLED clock, marquee, status, off; hỗ trợ tiếng Việt bitmap, emoji đơn
      sắc, xoay 180 độ, contrast, invert và lưu cấu hình NVS.
- [x] OLED boot hiển thị lời chào `MrKenDDo Robot` và IP local trước khi
      chuyển về mode đã lưu khi Wi-Fi/robot sẵn sàng.
- [x] OLED status hiển thị giờ, ngày dạng `dd-MMM`, cột sóng Wi-Fi và trạng
      thái `IDLE`/`LISTENING`/`SPEAKING`.
- [x] TFT 240x240 hỗ trợ xoay cấu hình qua webserver và panel offset `(80, 0)`
      cho phần cứng lắp ngang hiện tại.
- [x] TFT dùng nền đen, mắt robot cyan `#0abde3`, emotion custom, wake-up,
      blink, glance, curiosity và animation riêng cho speaking/listening.
- [x] Boot console trên TFT có hiệu ứng gõ chữ và vùng log cuộn; âm thanh
      typing đã được loại bỏ để không làm chậm boot hoặc ảnh hưởng hội thoại.
- [x] Tích hợp audio starting và robot-eye wake; không phát typing sound.
- [x] QR thanh toán mặc định đổi qua web/NVS, QR URL/text tạm qua MCP,
      QR IP hiện tại; bitmap ngân hàng gốc giữ làm mặc định khi chưa cấu hình.
      Ma trận 1-bit, quiet zone 4 module, caption tùy chọn, giữ 5–120 giây
      và phục hồi đúng UI. Cấu hình lỗi từ chối hiển thị thay vì đổi người nhận.
- [x] Audio Visualizer nhận PCM playback và vẽ các cột màu trên TFT; không
      dùng font trong overlay boot-time để tránh phụ thuộc font assets.
- [x] QR payment dùng bitmap 37x37 chỉ khoảng 185 byte, không cần QR runtime
      decoder/renderer; thời lượng MCP hỗ trợ 5–120 giây, mặc định 30 giây.
- [x] Build firmware bằng ESP-IDF 6.1 cho ESP32-S3; cấu hình flash 16 MiB
      cấp 3,9375 MiB cho mỗi OTA slot thay cho giới hạn 2,9375 MiB cũ.
      Dung lượng trống thực tế phụ thuộc binary của từng lần build.

## Current implementation status

Tất cả hạng mục trong kế hoạch hiện tại đã hoàn tất ở mức mã nguồn:

- [x] P0 — Điều khiển robot và routine từ webserver/MCP.
- [x] P0 — Gửi câu chữ tùy chọn lên OLED mà không ghi đè marquee đã lưu.
- [x] P0 — Hiển thị IP, status và đổi OLED mode từ MCP.
- [x] P1 — Mắt robot sinh động: blink, glance, curiosity, emotion và state
      animation.
- [x] P1 — OLED status dùng ngày `dd-MMM` và layout hai cột.
- [x] P1 — QR thanh toán trên TFT với thời gian hiển thị đủ dài để quét.
- [x] P1 — Routine chạy trong task FreeRTOS riêng, không chặn audio/main loop.
- [x] P1 — Boot OLED hiển thị tên robot và IP trước khi về mode bình thường.
- [x] P1 - Audio Visualizer nhận PCM media và tính FFT thật trên main task,
      mailbox 512 mẫu cố định, tối đa 25 FPS; TTS chỉ điều khiển miệng robot.

Không còn mục implementation nào đang chờ trong kế hoạch đã chốt. Các thay
đổi tiếp theo chỉ nên mở roadmap mới sau khi xác định rõ phạm vi và ảnh hưởng
đến bộ nhớ, audio hoặc giao thức MCP.

- [x] P1 - Track từ MCP dùng player chung `NotifyPlayer` với source `Media`,
      nhận metadata và timed lyrics qua `media.play`; `media.stop` dừng track.
      Radio dùng provider/tool và lifecycle riêng. Provider Zing MP3, LRC và
      URL proxy thuộc MCP server, không firmware.

## Hardware validation backlog
- [ ] MicroSD: xác nhận qua nhiều lần reboot với nguồn module 5 V, đọc log `RobotSD`, kiểm tra thiếu thẻ/lỗi mount vẫn có Wi-Fi/web/audio, mount lại và tháo an toàn. Sơ đồ PNG/SVG sẽ do chủ robot cập nhật.
- [ ] Upload/download file có tên tiếng Việt, so SHA-256 trước/sau, file rỗng và nhiều khối; từ chối trùng tên, sai offset, vượt giới hạn và traversal. Thử ngắt upload, đầy thẻ, tháo thẻ lúc I/O; xác nhận web Stop và hội thoại vẫn phản hồi khi truyền file.

- [ ] Sau refactor, xác nhận cấu hình NVS cũ vẫn được giữ; đổi mode OLED từ
      HTTP và MCP cho cùng kết quả, xoay TFT/OLED và reboot không mất cấu hình.
- [ ] Xác nhận QR hết hạn khi đang nghe radio, kết thúc playback khi QR đang
      hiện, và boot audio không làm visualizer giành màn hình trước READY.
Đây không phải là feature chưa implement; các mục này cần robot thật để xác
nhận vì build không thể kiểm tra dây nối, cực tính motor hoặc chất lượng hiển
thị:

- [ ] Nâng bánh khỏi mặt đất, xác nhận Forward/Backward và Left/Right, Stop,
      timeout và cờ đảo chiều motor.
- [ ] Xác nhận TFT sau khi khởi động lại với offset `(80, 0)` và góc lắp thực tế.
- [ ] Xác nhận OLED 0/180 độ, layout status và IP trên module 128x32.
- [ ] Xác nhận INMP441, wake/VAD, loa, ngắt audio và reconnect Wi-Fi/protocol.
- [ ] Kiểm tra thời gian boot từ power-on tới Wi-Fi, web server, IP và READY.
- [ ] Quét thử QR ở nhiều khoảng cách và xác nhận QR tự quay lại mắt robot sau
      timeout.


## Decisions and references

- Board definition mô tả phần cứng và nhận diện OTA; firmware tùy biến bổ sung
  các module sản phẩm. Giữ module robot trong thư mục board hiện tại, chỉ đưa
  logic ra phần dùng chung khi có hợp đồng độc lập với phần cứng/branding.
- Tham khảo checkout `TienHuyIoT/xiaozhi-esp32_vietnam`, nhánh `develop_vn`,
  commit `287e0ea`, về cách tách player/UI/MCP; không nhập nguyên bộ feature
  hoặc thay audio pipeline chỉ vì dự án tham khảo dùng cách đó.
- Luồng và trách nhiệm sau refactor được mô tả trong
  [`readme.md`](../readme.md), phần “MCP va routine robot”.
- Board vẫn dùng transport và MCP chính thức của XiaoZhi/tenclass.net.
- `hudrucan/xiaozhi-desk-robot` là repository reference cho ý tưởng UI và
  hành vi robot; không sao chép GPIO, board identity hoặc giả định backend.
- Boot OLED độc lập với mode OLED đã lưu, kể cả `OFF`, để luôn có thể hiển thị
  IP local trong giai đoạn kết nối.
- Khi READY, OLED quay về mode đã lưu và TFT chuyển sang robot eyes.
- QR được lưu trong `payment_qr_bitmap.h`; không lưu thông tin tài khoản dạng
  text trong firmware ngoài dữ liệu bitmap người dùng cung cấp.
- [ ] Kiểm tra boot/beep/TTS không bật spectrum; nhạc/radio chỉ bật sau PCM
      đầu tiên, dừng/reconnect/QR expiry khôi phục đúng mode. Xác nhận miệng
      nói theo TTS và khép khi pause/listening; không ảnh hưởng wake/VAD/audio.
- [ ] Replay nhạc bằng tool tenclass `search_music` → `play_music`: spectrum
      hiện sau PCM đầu tiên, tên bài/lyrics tiếng Trung hoặc Việt đọc được;
      sau bài hát, câu nói thường trở lại mắt/miệng. Thử QR trong lúc hát.
- [ ] Quét QR URL/IP và QR thanh toán mới trên TFT thật, kiểm tra đúng người
      nhận/số tiền, giữ sau reboot; QR tạm/IP không thay mặc định. Thử record
      NVS lỗi, xoay TFT, renew timer và expiry khi nhạc/boot/eyes đang hoạt động.
