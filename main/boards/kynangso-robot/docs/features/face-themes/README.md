# Kho theme khuôn mặt

Theme thay bảng màu và hình dáng nét vẽ cho toàn bộ biểu cảm robot trên TFT. Đây là gói
JSON cho renderer thủ tục của KyNangSo, không phải file firmware hay assets `.bin` của board khác.
Tham khảo cách trình bày kho từ [DB-ROBOT](https://dbrobot.vn/theme.html); không tải hay ghi
đè phân vùng assets bằng các gói của trang đó.

## Sử dụng

Dashboard → **Biểu cảm robot → Kho theme khuôn mặt**:

- **Classic Cyan**: mắt cyan và hình dáng gốc.
- **Candy Rose**: mắt hồng, nét tròn mềm, điểm nhấn vàng trên nền tím đậm.
- **Neon Violet**: mắt tím sáng và điểm nhấn mint trên nền midnight.
- **Pixel Mint**: mắt xanh mint với góc vuông, đường nét không bo tròn.

Bấm **Chọn theme** và chờ hoàn tất. Màu/dáng mới áp dụng khi TFT hiển thị khuôn mặt;
không ngắt TV, nhạc, QR hoặc thông báo ưu tiên. Biểu cảm idle, chớp mắt, nhìn quanh và
miệng theo mức âm thanh TTS tiếp tục hoạt động. Hình trên web chỉ minh họa mắt trung tính.
Theme và biểu cảm là hai lựa chọn độc lập; chạm đổi biểu cảm vẫn giữ theme.

Không cần SD để dùng bốn theme có sẵn. Bấm **Tải gói mẫu**, chỉnh JSON rồi **Nhập theme vào SD**.
File được lưu tại `/robot/themes/<id>.json`, không ghi đè ID/file đã tồn tại. Có thể upload
bằng trang quản lý thẻ nhớ rồi bấm **Tải lại danh sách**. Xóa gói bằng trang thẻ nhớ rồi tải lại;
theme đang chọn vẫn giữ bản cấu hình đã nạp. Muốn đổi gói đã chọn sau khi sửa file, tải lại và chọn lại.

Ví dụ hội thoại (MCP tool phía firmware, không cần thay backend):

- “Robot có những theme khuôn mặt nào?”
- “Đổi mặt robot sang Candy Rose.”
- “Cho robot dùng theme Pixel Mint.”
- “Tải lại danh sách theme trên thẻ nhớ.”
- “Switch the robot face theme to Neon Violet.”

## Định dạng gói

```json
{"version":1,"id":"my-theme","name":"My Theme","style":"soft","primary":"#FF8AC6","accent":"#FFD166","tear":"#92E7FF","background":"#140D22"}
```

JSON phẳng, đủ đúng tám trường, tối đa 2 KiB; không chấp nhận object/array lồng nhau,
khóa lặp hay khóa không hỗ trợ. `id` dài 1–32 ký tự `a-z`, `0-9`, `_`, `-`; tên file phải
khớp ID. `name` dài 1–64 byte UTF-8, không có ký tự điều khiển. `style` là `rounded`,
`soft` hoặc `pixel`. Màu theo `#RRGGBB`; primary/background phải đủ chênh lệch độ sáng.
Các ID `classic`, `candy`, `neon`, `pixel` được dành cho theme có sẵn.

Không có GIF, JPEG, script hoặc URL trong gói. Mọi biểu cảm dùng chung hình học hiện tại;
gói này chưa hỗ trợ nhân vật hoạt hình có bộ animation riêng. Mẫu nằm trong thư mục `examples/`.

## Workflow và module

1. HTTP `GET /api/face_themes` hoặc MCP `self.robot.face_theme(action=list/status)` đọc catalog RAM.
2. `POST /api/face_themes` nhận `action=refresh/select/install`, `id` cho select và `manifest`
   là chuỗi JSON cho install. MCP nhận `status/list/refresh/select`; chọn theo ID từ danh sách.
3. `RobotController::FaceThemes()` chuyển lệnh tới `FaceThemeService`. Chỉ một thao tác chờ/chạy;
   HTTP trả 202 khi nhận, 409 nếu bận hoặc sai yêu cầu. Không đọc SD trong callback MCP/HTTP.
4. Worker kiểm tra gói, đọc/ghi qua `SdStorage` với khóa I/O sẵn có. Catalog gồm bốn theme
   có sẵn và tối đa 16 theme SD; một lần quét xét trang đầu tối đa 24 mục. Thư mục nên chỉ chứa
   các file gói. `skipped` báo gói lỗi/trùng tên; `truncated` báo giới hạn danh sách.
5. Select lưu snapshot nhỏ tại NVS `face_theme/manifest`, qua `RobotSettings`, rồi schedule
   thay đổi trên application task. Display lấy khóa LVGL và gọi `RobotEyesView::SetTheme`.
6. Client đọc lại `busy`, `operation`, `error`, `active_id`. Chỉ xác nhận chọn thành công khi
   `busy=false`, `error` rỗng và `active_id` khớp. Lệnh đã nhận chưa đồng nghĩa đã lưu thành công.

Khởi động khôi phục snapshot NVS; bản lưu lỗi dùng Classic Cyan. Thiếu/tháo SD không làm mất
theme đang chạy. Lần đầu thẻ mount sẽ quét catalog; sau thay đổi file/remount dùng nút refresh.
Ghi thẻ bị từ chối khi SD đang phát nhạc hoặc bận. Lỗi gói/lưu NVS không đổi theme đang chạy.

Renderer dùng shape LVGL có sẵn và timer 40 ms. Không tạo decoder, framebuffer theme hay
animation task mới. Worker theme dùng stack 6 KiB, ưu tiên 2, queue một yêu cầu và catalog hữu hạn.

## Nghiệm thu

Ngày 17/11/2026: build board qua `idf-init` và `kynangso-build-firmware.bat` thành công;
app 3.454.512 byte, còn 16% phân vùng ứng dụng. Kiểm tra host dùng chính parser C++ đã qua
round-trip, ba style, gói mẫu, sai ID/style/màu, khóa lặp/thiếu, JSON lồng và vượt kích thước.
HTML được xuất từ header nhúng; JavaScript qua `node --check` và kiểm tra chuỗi UTF-8.

Chưa nghiệm thu trên robot thật: chọn từng theme, thử vui/buồn/nghe/nói và touch; reboot/tháo SD
vẫn giữ theme; nhập gói hợp lệ, từ chối gói lỗi/trùng ID; đổi theme khi TV/QR đang mở rồi quay về mặt.
Build firmware thành công không thay thế kiểm tra màu và chuyển cảnh trên TFT.
