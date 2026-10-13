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
- [x] Audio Visualizer trên TFT: khi có playback audio, màn hình chuyển sang
    nền midnight với palette gradient tím/xanh/lục/vàng/cam/hồng phản ứng
    theo mức PCM; khi playback kết thúc, giao diện mắt robot được khôi phục.
- [ ] MCP nghe nhạc việt hoặc cách build host server nghe nhạc bằng cách ra lệnh mcp tìm nhạc theo tên, tác giả, thể loại
## Completed implementation

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
- [x] MCP `self.robot.show_payment_qr` hiển thị QR ngân hàng dạng bitmap 1-bit
      board-local trên TFT; QR có quiet zone, nằm trên cùng UI và tự ẩn sau
      timeout cấu hình được.
- [x] Audio Visualizer nhận PCM playback và vẽ các cột màu trên TFT; không
      dùng font trong overlay boot-time để tránh phụ thuộc font assets.
- [x] QR payment dùng bitmap 37x37 chỉ khoảng 185 byte, không cần QR runtime
      decoder/renderer; thời lượng MCP hỗ trợ 5–120 giây, mặc định 30 giây.
- [x] Build firmware bằng ESP-IDF 6.1 đã thành công cho ESP32-S3; binary
      hiện còn khoảng 10% dung lượng app partition.

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
- [x] P1 - Audio Visualizer nhận level envelope từ audio playback mà không
      chặn audio task; UI cập nhật qua main task.

Không còn mục implementation nào đang chờ trong kế hoạch đã chốt. Các thay
đổi tiếp theo chỉ nên mở roadmap mới sau khi xác định rõ phạm vi và ảnh hưởng
đến bộ nhớ, audio hoặc giao thức MCP.

## Hardware validation backlog

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

## Future ideas (not part of the completed plan)

- [ ] Mở rộng MCP cho các GPIO/phụ kiện board-specific mới sau khi có mô tả
      an toàn và kiểm thử riêng.
- [ ] Thêm xác thực/TLS cho webserver nếu cần dùng ngoài mạng LAN tin cậy.

## Decisions and references

- Board vẫn dùng transport và MCP chính thức của XiaoZhi/tenclass.net.
- `hudrucan/xiaozhi-desk-robot` là repository reference cho ý tưởng UI và
  hành vi robot; không sao chép GPIO, board identity hoặc giả định backend.
- Boot OLED độc lập với mode OLED đã lưu, kể cả `OFF`, để luôn có thể hiển thị
  IP local trong giai đoạn kết nối.
- Khi READY, OLED quay về mode đã lưu và TFT chuyển sang robot eyes.
- QR được lưu trong `payment_qr_bitmap.h`; không lưu thông tin tài khoản dạng
  text trong firmware ngoài dữ liệu bitmap người dùng cung cấp.
