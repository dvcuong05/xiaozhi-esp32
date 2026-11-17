# Theme khuôn mặt robot

Kho theme cho renderer LVGL thủ tục của KyNangSo. Mỗi theme dùng cùng bộ biểu cảm, chuyển động mắt và miệng TTS; theme chỉ thay bảng màu, hình khối và hiệu ứng ánh sáng.

Các trang theme và video đã cung cấp được dùng làm nguồn cảm hứng về cảm giác thị giác, cách trình bày và nhịp chuyển động. Không sao chép mã nguồn, firmware, ảnh GIF hay gói theme của bên khác. Hình ảnh mới được dựng bằng primitive LVGL của firmware.

## Theme tích hợp

- **Classic Cyan** — giao diện cyan gọn, nền đen.
- **Candy Rose** — mắt hồng bo mềm, nền tím đậm.
- **Neon Violet** — mắt tím và điểm nhấn mint.
- **Pixel Mint** — nét vuông kiểu pixel.
- **Aether Hologram** — mắt như tấm kính phát sáng, có lớp đùn, HUD và vệt quét.
- **Quantum Orbit** — vòng quỹ đạo đứt đoạn xoay quanh mắt với các điểm sáng di chuyển.
- **Prism Reactor** — mắt nhiều lớp như khối năng lượng, lõi giữa nhịp sáng và hạt plasma chuyển động.

## Theme tải thêm

Kho theme công khai: https://myrobot.kynangso.com/my/themes. Bốn gói đầu tiên gồm Red Devil, Capybara thư thái, Shiba vàng và Mochi Heart. Đây là gói JSON nhỏ, lưu màu và một kiểu vẽ đã có trong firmware; không mang ảnh, GIF, mã thực thi hay thư viện mới.

Để cài, tải gói JSON, mở **Dashboard → Biểu cảm robot → Kho theme khuôn mặt → Nhập theme vào SD**. Firmware tự lưu file tại `/robot/themes/<id>.json`; sau đó chọn **Tải lại danh sách** và bấm **Chọn theme**. Có thể chép file vào thư mục đó bằng trang microSD thay cho nút nhập. Tối đa 16 theme tùy chỉnh được lưu trên robot; các gói khác vẫn có thể tải từ kho MyRobot khi cần.

Các gói Red Devil, Capybara, Shiba và Mochi cần firmware có renderer tương ứng. Sau khi cài firmware hỗ trợ các style này, có thể tải thêm gói JSON cùng style mà không cần nạp lại firmware.

Các hiệu ứng tạo chiều sâu 2.5D bằng lớp sáng/tối, viền và opacity; firmware không chạy bộ dựng hình 3D thật.

## Sử dụng

Mở Dashboard → **Biểu cảm robot → Kho theme khuôn mặt**, rồi chọn **Dùng theme**. Theme đổi ngay khi robot đang ở giao diện mặt; TV, nhạc, QR và các màn hình ưu tiên vẫn giữ nguyên luồng hiển thị.

Có thể hỏi robot:

- “Đổi mặt robot sang Aether Hologram.”
- “Cho robot dùng Quantum Orbit.”
- “Chuyển sang Prism Reactor.”
- “Switch the robot face theme to Quantum Orbit.”

Theme và biểu cảm mặc định là hai thiết lập riêng. Các cảm xúc happy, surprised, sad, angry, listening, speaking và miệng theo biên độ âm thanh tiếp tục hoạt động với mọi theme.

## Gói theme tùy chỉnh

Không cần microSD cho theme tích hợp. Gói tùy chỉnh được lưu ở /robot/themes/<id>.json; giới hạn 16 gói do người dùng thêm, tối đa 2 KiB mỗi gói. Bỏ thẻ SD không ảnh hưởng theme đang chạy hoặc snapshot NVS.

JSON mẫu:

    {"version":1,"id":"my-aether","name":"My Aether","style":"holo","primary":"#75F5FF","accent":"#A889FF","tear":"#48D7FF","background":"#041326"}

JSON phẳng version 1 gồm đúng tám trường như mẫu; màu dùng #RRGGBB. id dài 1–32 ký tự thường a-z, số, _, -; tên 1–64 byte UTF-8. Các giá trị style hợp lệ là rounded, soft, pixel, holo, orbital, plasma, demon, capybara, shiba và mochi. Gói firmware cũ vẫn tương thích.

Có thể tải gói mẫu trong Dashboard, sửa style và màu, sau đó nhập lại vào SD. Các file mẫu bổ sung nằm trong examples/.

## Luồng và giới hạn kỹ thuật

1. Web gọi GET /api/face_themes để đọc catalog; chọn theme qua POST /api/face_themes. MCP dùng self.robot.face_theme với các action list/status/refresh/select.
2. FaceThemeService đọc gói JSON trên worker riêng qua SdStorage; tối đa 7 theme tích hợp và 16 theme SD. Chọn theme lưu snapshot vào NVS rồi cập nhật display trên application task.
3. RobotEyesView vẽ theo design space 240×144 và tự co theo FaceRegion LVGL. Nền theme phủ toàn layout TFT theo kích thước firmware khai báo; vùng thông tin bên dưới chọn màu chữ có tương phản.
4. Renderer chạy theo timer 40 ms khi mặt đang hiển thị. Ẩn mặt hoặc tắt animation sẽ dừng cập nhật khung hình. Không giải mã GIF/video, không cấp framebuffer theme hay buffer lớn theo từng frame.

holo và plasma thêm một số lớp hình có opacity; orbital vẽ vòng bằng các đoạn ngắn. Khối lượng vẽ vẫn giới hạn theo mỗi frame để giữ công việc audio và giao diện khác ổn định. Trên TFT 240×240, vùng artwork được scale từ layout hiện có; không cần nhập kích thước màn hình trên webserver.

Nguồn cảm hứng tham khảo: [Kho giao diện DB-ROBOT](https://dbrobot.vn/theme.html). Đây là liên kết tham khảo ý tưởng thư viện theme; artwork và cách dựng trong firmware KyNangSo là bản tự triển khai.

## Nghiệm thu

Build firmware không thay thế kiểm tra thực tế. Sau khi nạp, kiểm tra theme tích hợp và bốn gói tải thêm với idle, happy, surprised, listening và speaking; quan sát phụ kiện, màu chữ bên dưới, tắt/bật animation, rồi đổi theme trong lúc TV hoặc QR đang hoạt động.
