# Theme khuôn mặt robot

Kho theme cho renderer LVGL của KyNangSo. Các theme thủ tục dùng cùng bộ biểu cảm, chuyển động mắt và miệng TTS. Ngoài theme vẽ sẵn, người dùng có thể tạo theme GIF nhiều biểu cảm trực tiếp từ Dashboard; toàn bộ GIF nằm trên microSD và chỉ được nạp khi cần.

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
2. FaceThemeService đọc gói JSON trên worker riêng qua SdStorage; tối đa 7 theme dựng sẵn và 16 theme tùy chỉnh. Chọn theme lưu snapshot vào NVS rồi cập nhật display trên application task.
3. RobotEyesView vẽ theo design space 240×144 và tự co theo FaceRegion LVGL. Nền theme phủ toàn layout TFT theo kích thước firmware khai báo; vùng thông tin bên dưới chọn màu chữ có tương phản.
4. Renderer chạy theo timer 40 ms khi mặt đang hiển thị. Ẩn mặt hoặc tắt animation sẽ dừng cập nhật khung hình. GIF được giải mã bởi LVGL trong một widget dùng chung; bộ nhớ ảnh được tái sử dụng giữa các biểu cảm.

holo và plasma thêm một số lớp hình có opacity; orbital vẽ vòng bằng các đoạn ngắn. Khối lượng vẽ vẫn giới hạn theo mỗi frame để giữ công việc audio và giao diện khác ổn định. Trên TFT 240×240, vùng artwork được scale từ layout hiện có; không cần nhập kích thước màn hình trên webserver.

Nguồn cảm hứng tham khảo: [Kho giao diện DB-ROBOT](https://dbrobot.vn/theme.html). Đây là liên kết tham khảo ý tưởng thư viện theme; artwork và cách dựng trong firmware KyNangSo là bản tự triển khai.

## Nghiệm thu

Build firmware không thay thế kiểm tra thực tế. Sau khi nạp, kiểm tra theme tích hợp và bốn gói tải thêm với idle, happy, surprised, listening và speaking; quan sát phụ kiện, màu chữ bên dưới, tắt/bật animation, rồi đổi theme trong lúc TV hoặc QR đang hoạt động.


## Theme GIF biểu cảm từ thẻ nhớ

Dashboard → **Biểu cảm robot** → **Tạo theme biểu cảm động** cho phép đặt tên, chọn kiểu/màu dự phòng và gán GIF cho từng trạng thái. Có thể chỉ tải một vài trạng thái; trạng thái chưa có GIF dùng renderer vẽ sẵn. Hiện hỗ trợ 17 khóa: `neutral`, `happy`, `sad`, `angry`, `surprised`, `sleepy`, `confused`, `listening`, `laugh`, `love`, `dizzy`, `distracted`, `devil`, `confident`, `yawn`, `playful`, `speaking`. Các bí danh như `idle`, `talking`, `crying`, `wink` được chuẩn hóa về khóa tương ứng.

### Chuẩn hóa và lưu

1. Trình duyệt kiểm tra từng GIF nguồn (tối đa 10 MiB, 512×512, 120 frame), sau đó gửi file cho Web Worker cục bộ. GIF nguồn không được tải lên dịch vụ backend.
2. Worker ghép frame và transparency/disposal, đổi nền trong suốt sang màu nền theme, thu nhỏ có nội suy, lấy mẫu tối đa 10 fps và giảm palette. Nếu output còn quá lớn, worker lần lượt giảm số màu, kích thước và số frame.
3. Bản cuối có tối đa 160×160, 40 frame từ công cụ chuẩn hóa (firmware chấp nhận tối đa 60 frame) và 256 KiB mỗi biểu cảm. Mỗi file được gửi tuần tự thành chunk 32 KiB lên robot.
4. GIF lưu tại `/robot/themes/<id>/<expression>.gif`; manifest version 2 lưu tại `/robot/themes/<id>.json`. Không ghi đè ID đã có. Tối đa 16 theme tùy chỉnh trên một thẻ. Có thể đổi theme khi đang dùng; muốn xóa phải chuyển sang theme khác trước.

### Tải lên robot và giới hạn tài nguyên

- Bộ giải mã/chuẩn hóa chạy trên máy tính hoặc điện thoại của người dùng. Robot chỉ nhận file đã giới hạn kích thước, kiểm tra GIF, lưu lên SD và phục vụ thư viện JavaScript đã nhúng sẵn trong firmware. Ba asset trình duyệt chiếm 64,736 byte dữ liệu nhúng; không có ảnh GIF nào được thêm vào firmware.
- Trên robot chỉ GIF của biểu cảm hiện tại được đọc từ SD. Tối đa hai GIF nén được giữ trong PSRAM cùng lúc (biểu cảm đang hiển thị và biểu cảm mới/chờ chuyển); LVGL dùng một widget và bộ đệm frame chung. GIF được đọc đúng kích thước file, giới hạn 256 KiB, 160×160 và 60 frame.
- Khi phát âm thanh từ thẻ SD, firmware hoãn đổi GIF để tránh tranh chấp đọc thẻ; khuôn mặt vẽ sẵn tiếp tục hoạt động. Worker xử lý riêng, không chặn application/audio task. Khi theme GIF lỗi, renderer procedural tiếp tục hiển thị.
- MicroSD là tùy chọn. Khi không có thẻ, các theme firmware và theme JSON hiện có tiếp tục hoạt động. Theme GIF tùy chỉnh cần thẻ để nạp biểu cảm sau khởi động.
- Xóa theme tùy chỉnh sẽ xóa manifest và toàn bộ thư mục GIF tương ứng; firmware không cho xóa theme đang được chọn hoặc theme dựng sẵn.

### Kiểm tra trước khi dùng

Cài firmware mới, gắn thẻ SD, mở Dashboard, tạo theme với một GIF nguồn nhỏ trước, rồi chọn theme trong danh sách. Kiểm tra lần lượt các trạng thái đã gán và các trạng thái dự phòng. Sau đó thử nghe nhạc từ thẻ để xác nhận hình procedural vẫn hoạt động và việc đổi GIF không làm gián đoạn âm thanh. Việc chuẩn hóa GIF mới đã được kiểm tra bằng harness trình duyệt và build; cần xác nhận trực quan trên robot sau khi nạp firmware.

GIF Cherry dùng trong lần nghiệm thu PoC trước đó là asset do người dùng cung cấp; chỉ chia sẻ GIF mà bạn có quyền sử dụng. Chi tiết PoC cũ và thử nghiệm thẻ nhớ được giữ bên dưới như lịch sử, không phải quy trình cài theme hiện hành.

### Ghi nhận thử nghiệm microSD và bộ nhớ

Trên robot thử nghiệm, đọc thư mục từng thất bại với `allocate_dma_buf: not enough mem` khi heap DMA nội bộ thấp và phân mảnh; log lỗi đã xuất hiện trước khi bật GIF. `SdStorage` dành sẵn một sector DMA 512 byte lúc khởi tạo và gắn lại bộ đệm sau khi mount thẻ 512-byte-sector, dưới khóa I/O hiện có. Thẻ có sector lớn hơn vẫn dùng cách cấp phát của ESP-IDF. Giao dịch không thẳng hàng được chia theo sector nên có thể giảm tốc độ; đo thử file tạm 64 KiB đã ghi/đọc đúng SHA-256/xóa được, nhưng chưa đo throughput upload, nhạc và ảnh. Lỗi `stat` khi tải file giờ chỉ trả “không tìm thấy” với `ENOENT`; lỗi I/O khác được giữ nguyên.

Cấu hình board đặt `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0` để các cấp phát tổng quát đủ điều kiện ưu tiên PSRAM, giữ heap nội bộ cho DMA và các vùng bắt buộc. `CONFIG_MBEDTLS_HARDWARE_AES=y` vẫn bật. Sau khi nạp qua UART ngày 17/11/2026, API đo khoảng 64–68 KiB RAM nội bộ còn trống, block lớn nhất khoảng 31 KiB; một lệnh web chat qua MQTT hoàn tất và robot trả lời, không thấy lỗi cấp phát AES trong lượt này. Đây là kết quả trên cấu hình và robot hiện tại, chưa chứng minh mọi tải đồng thời đều an toàn.

### Lịch sử nghiệm thu GIF PoC trên robot

Ngày 17/11/2026, hai GIF Cherry mẫu (160×160, khoảng 12,5 fps) được lưu ở `/robot/themes/gif-poc/`, kiểm tra lại SHA-256 và chọn làm theme trên robot thật qua COM13. Người dùng xác nhận ảnh động khá mượt và dòng chữ phía dưới rõ. Theme vẫn là `gif-poc` sau lượt TV và robot đã trở lại `idle` khi gửi lệnh dừng.

Trên firmware cuối có AES phần cứng bật, lệnh chat MQTT hoàn tất. Lượt TV tiếp theo giữ trạng thái `playing` khoảng 21,6 giây; bộ đếm lên 215 frame, audio position lên 21.580 ms và tốc độ báo cáo xấp xỉ 9–11 fps. Lệnh dừng trả `stopped`, sau đó API báo robot `idle`, Wi-Fi còn kết nối và theme `gif-poc` vẫn active. Đây là telemetry của phiên chạy ngắn; chưa xác nhận bằng tai hình/tiếng đồng bộ trong lượt này và chưa thay thế nghiệm thu dài.

PoC ban đầu chỉ chuyển GIF `idle`/`talking`. Tính năng theme GIF 17 trạng thái hiện đã thay thế định dạng PoC; cần xác nhận riêng các biểu cảm mới trên firmware sau khi nạp. GIF `speaking` là ảnh động theo trạng thái, không lip-sync theo âm thanh.
