# Theme khuôn mặt robot

Kho theme cho renderer LVGL của KyNangSo. Các theme thủ tục dùng cùng bộ biểu cảm, chuyển động mắt và miệng TTS. Ngoài ra có GIF PoC thử hai ảnh động từ thẻ nhớ như hướng dẫn bên dưới.

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
2. FaceThemeService đọc gói JSON trên worker riêng qua SdStorage; tối đa 7 theme thủ tục, 1 mục GIF PoC và 16 theme SD. Chọn theme lưu snapshot vào NVS rồi cập nhật display trên application task.
3. RobotEyesView vẽ theo design space 240×144 và tự co theo FaceRegion LVGL. Nền theme phủ toàn layout TFT theo kích thước firmware khai báo; vùng thông tin bên dưới chọn màu chữ có tương phản.
4. Renderer chạy theo timer 40 ms khi mặt đang hiển thị. Ẩn mặt hoặc tắt animation sẽ dừng cập nhật khung hình. Renderer thủ tục không giải mã GIF/video và không cấp framebuffer theme theo từng frame. GIF PoC có bộ nhớ riêng như mô tả bên dưới.

holo và plasma thêm một số lớp hình có opacity; orbital vẽ vòng bằng các đoạn ngắn. Khối lượng vẽ vẫn giới hạn theo mỗi frame để giữ công việc audio và giao diện khác ổn định. Trên TFT 240×240, vùng artwork được scale từ layout hiện có; không cần nhập kích thước màn hình trên webserver.

Nguồn cảm hứng tham khảo: [Kho giao diện DB-ROBOT](https://dbrobot.vn/theme.html). Đây là liên kết tham khảo ý tưởng thư viện theme; artwork và cách dựng trong firmware KyNangSo là bản tự triển khai.

## Nghiệm thu

Build firmware không thay thế kiểm tra thực tế. Sau khi nạp, kiểm tra theme tích hợp và bốn gói tải thêm với idle, happy, surprised, listening và speaking; quan sát phụ kiện, màu chữ bên dưới, tắt/bật animation, rồi đổi theme trong lúc TV hoặc QR đang hoạt động.


## GIF PoC từ thẻ nhớ

PoC thử hai GIF của người dùng, chưa phải kho artwork chính thức. Chuẩn bị:

```text
/robot/themes/gif-poc/idle.gif
/robot/themes/gif-poc/talking.gif
```

Mỗi file phải là GIF hợp lệ, tối đa 512 KiB và 240×240 pixel. Trong ví dụ Cherry, có thể đổi `Neutral.gif` thành `idle.gif` và chọn một GIF chuyển động làm `talking.gif`. Sao chép qua trang quản lý thẻ nhớ hoặc bằng máy tính, sau đó vào Dashboard → Biểu cảm robot → Kho theme khuôn mặt → **GIF PoC (SD)**. Lúc nói dùng `talking.gif`; các trạng thái khác dùng `idle.gif`. Không áp dụng miệng vector và GIF không lip-sync theo giọng nói.

Firmware tải hai GIF trên worker của theme qua `SdStorage::Download` vào PSRAM, kiểm tra header, khung ảnh và cấu trúc khối trước khi hiển thị. LVGL 9.5 giải mã một GIF tại một thời điểm, tạm dừng khi giao diện mặt bị ẩn. Ảnh được thu theo tỉ lệ để vừa vùng mặt và không che phụ đề. Bộ đệm ảnh ARGB8888 205×205 cần khoảng 164 KiB, ngoài tối đa 1 MiB PSRAM dành cho hai file; chi phí thực tế còn có decoder và các bộ đệm giao diện. Dung lượng này được giải phóng khi đổi theme. Nếu không đọc được file, lựa chọn mới bị từ chối; sau reboot thiếu thẻ thì mặt thủ tục dự phòng vẫn xuất hiện và có thông báo trong kho theme. Chọn lại GIF PoC sau khi gắn thẻ để thử lại.

GIF Cherry có nhịp nguồn 50 fps. Bản mẫu cục bộ để thử đã thu về 160×160 và lấy mỗi bốn frame, khoảng 12,5 fps; tốc độ thực tế trên ESP32 phụ thuộc decoder, TFT và công việc đồng thời. Kiểm tra thực tế trên thiết bị, bao gồm âm thanh, chế độ khác và bộ nhớ, trước khi quyết định format chính thức. Việc dùng GIF nguồn bên thứ ba là thử nghiệm cục bộ; chỉ phân phối sau khi xác nhận quyền sử dụng.

Tham khảo [MinhTiLab Theme](https://robot.minhtilab.com/theme): giao diện tùy chỉnh hai GIF chờ/nói, màu nền/chữ và ẩn phụ đề; trang đóng gói GIF cùng font và model vào `assets.bin` để tải hoặc nạp phân vùng flash. PoC KyNangSo chỉ đọc hai file riêng trên SD; không nhập `.bin` hoặc thay model wake word.


### Ghi nhận thử nghiệm microSD và bộ nhớ

Trên robot thử nghiệm, đọc thư mục từng thất bại với `allocate_dma_buf: not enough mem` khi heap DMA nội bộ thấp và phân mảnh; log lỗi đã xuất hiện trước khi bật GIF. `SdStorage` dành sẵn một sector DMA 512 byte lúc khởi tạo và gắn lại bộ đệm sau khi mount thẻ 512-byte-sector, dưới khóa I/O hiện có. Thẻ có sector lớn hơn vẫn dùng cách cấp phát của ESP-IDF. Giao dịch không thẳng hàng được chia theo sector nên có thể giảm tốc độ; đo thử file tạm 64 KiB đã ghi/đọc đúng SHA-256/xóa được, nhưng chưa đo throughput upload, nhạc và ảnh. Lỗi `stat` khi tải file giờ chỉ trả “không tìm thấy” với `ENOENT`; lỗi I/O khác được giữ nguyên.

Cấu hình board đặt `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0` để các cấp phát tổng quát đủ điều kiện ưu tiên PSRAM, giữ heap nội bộ cho DMA và các vùng bắt buộc. `CONFIG_MBEDTLS_HARDWARE_AES=y` vẫn bật. Sau khi nạp qua UART ngày 17/11/2026, API đo khoảng 64–68 KiB RAM nội bộ còn trống, block lớn nhất khoảng 31 KiB; một lệnh web chat qua MQTT hoàn tất và robot trả lời, không thấy lỗi cấp phát AES trong lượt này. Đây là kết quả trên cấu hình và robot hiện tại, chưa chứng minh mọi tải đồng thời đều an toàn.

### Nghiệm thu GIF PoC trên robot

Ngày 17/11/2026, hai GIF Cherry mẫu (160×160, khoảng 12,5 fps) được lưu ở `/robot/themes/gif-poc/`, kiểm tra lại SHA-256 và chọn làm theme trên robot thật qua COM13. Người dùng xác nhận ảnh động khá mượt và dòng chữ phía dưới rõ. Theme vẫn là `gif-poc` sau lượt TV và robot đã trở lại `idle` khi gửi lệnh dừng.

Trên firmware cuối có AES phần cứng bật, lệnh chat MQTT hoàn tất. Lượt TV tiếp theo giữ trạng thái `playing` khoảng 21,6 giây; bộ đếm lên 215 frame, audio position lên 21.580 ms và tốc độ báo cáo xấp xỉ 9–11 fps. Lệnh dừng trả `stopped`, sau đó API báo robot `idle`, Wi-Fi còn kết nối và theme `gif-poc` vẫn active. Đây là telemetry của phiên chạy ngắn; chưa xác nhận bằng tai hình/tiếng đồng bộ trong lượt này và chưa thay thế nghiệm thu dài.

Chuyển GIF `idle`/`talking` theo trạng thái hội thoại đã có trong source, nhưng người dùng chưa xác nhận trực quan GIF talking khi robot nói. Cần kiểm tra riêng trạng thái đó, đo thời gian chạy dài và throughput thẻ trong các tác vụ nhạc/ảnh trước khi biến PoC thành format theme chính thức. GIF talking là ảnh động theo trạng thái, không lip-sync theo âm thanh.
