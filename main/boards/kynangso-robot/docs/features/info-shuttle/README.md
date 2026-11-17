# Màn hình chính động (Dynamic Main Screen)

Khi robot rảnh, Màn hình chính động luân phiên ba nội dung trên TFT: khuôn mặt robot giữ nguyên
giao diện gốc, thẻ lịch tiếp theo có giờ và tiêu đề nổi bật, cùng thẻ thời tiết có biểu tượng và
khu vực nhiệt độ/điều kiện. Lịch và thời tiết có màu nhấn, nền riêng; thẻ khuôn mặt không chèn
header, footer hay thành phần trang trí.

## Cấu hình

Thẻ Màn hình chính động trên trang webserver cho phép bật/tắt, chọn thẻ, chọn thứ tự ngẫu nhiên
hoặc tuần tự và đặt chu kỳ 5-120 giây. Webserver cho phép đặt địa điểm thời tiết và chọn riêng một trong bốn tông màu cho thẻ lịch và thẻ thời tiết; trang khuôn mặt luôn giữ nguyên giao diện gốc. Cấu hình đọc/ghi
qua GET/POST `/api/info_shuttle`. NVS đọc được record phiên bản 1/2 cũ và lưu phiên bản 3; địa điểm
được lưu riêng trong namespace `info_shuttle` để không làm mất thiết lập hiện có.

Luồng hội thoại, radio, nhạc, live TV, đồng hồ/đếm giờ, ảnh, Bluetooth, QR, boot console, xem
trước khuôn mặt, phụ đề và thông báo có quyền ưu tiên TFT. Khi một trạng thái như vậy bắt đầu,
Màn hình chính động ẩn ngay và timer luân phiên tạm dừng. Tính năng chỉ tiếp tục sau khi trạng
thái chiếm màn hình kết thúc hoặc bị hủy; khi ẩn phụ đề, tính năng cũng dừng.

## Điều khiển qua hội thoại giọng nói

Robot đăng ký các MCP device tools self.robot.dynamic_main_screen.*, để người dùng điều khiển
Màn hình chính động (Dynamic Main Screen) qua giọng nói trong chat:

- `self.robot.dynamic_main_screen.status`: đọc cấu hình hiện tại.
- `self.robot.dynamic_main_screen.set_enabled`: bật/tắt, giữ nguyên danh sách thẻ và chu kỳ.
- `self.robot.dynamic_main_screen.configure`: chọn ngẫu nhiên/tuần tự, chu kỳ 5-120 giây và các thẻ
  khuôn mặt/lịch/thời tiết. Khi chỉ đổi một tùy chọn, đọc status trước và giữ nguyên các giá trị
  còn lại; luôn bật ít nhất một thẻ.
- `self.robot.dynamic_main_screen.next`: chuyển sang thẻ kế tiếp ngay khi TFT rảnh. Nội dung TFT
  có quyền ưu tiên vẫn được giữ nguyên.
- `self.robot.dynamic_main_screen.refresh_weather`: xếp yêu cầu làm mới thẻ thời tiết trong worker
  nền. Chỉ gọi khi Màn hình chính động và thẻ thời tiết đang bật; robot cần kết nối Device Bridge.
  Lệnh không chặn audio/UI. Backend cache dữ liệu theo địa điểm 15 phút, vì vậy yêu cầu làm mới
  có thể nhận lại bản tin đang cache thay vì gọi provider liên tục.

Các lệnh thay đổi cấu hình dùng chung `RobotController` và NVS với trang webserver; cập nhật màn
hình được đưa vào hàng đợi chính của ứng dụng. MCP server/agent cần có các tool thiết bị này trong
phiên chat để mô hình gọi chúng theo yêu cầu của người dùng.

## Màu sắc, chữ và icon thời tiết

Webserver lưu tông màu Calendar và Thời tiết riêng. Tiêu đề và địa điểm dùng font Noto Sans 16 px có đủ glyph tiếng Việt; địa điểm
được xuống dòng khi dài. Font được tạo bằng `scripts/generate_time_font.py --size 16`. Bộ tạo font tự kiểm tra 142 codepoint tiếng Việt ở cả NFC/NFD, gồm `Ĩ/ĩ` (U+0128/U+0129) và `Ũ/ũ` (U+0168/U+0169), tránh lỗi như “Vũng Tàu” bị mất chữ `ũ`. Tất cả cỡ Noto Sans board-local regular/bold đều dùng dải glyph này; nhãn thời tiết và vòng quay dùng các font đó cùng fallback theme. Khuôn mặt robot không dùng các tuỳ chỉnh này và giữ nguyên giao diện gốc.

Backend trả `icon_kind` cùng bản tin thời tiết theo từng địa điểm: `0` nắng, `1` nhiều mây,
`2` mưa, `3` dông, `4` gió mạnh, `5` tuyết, `6` sương mù. TFT vẽ icon bằng các hình LVGL;
giọt mưa chuyển động và tia chớp nhấp nháy chỉ khi thẻ thời tiết đang hiện. Khi thẻ bị ẩn,
đổi màu hoặc nhường TFT cho radio/nhạc/TV/đồng hồ/hội thoại, các animation dừng.

## Dữ liệu lịch và thời tiết

Thẻ lịch đọc lịch sắp tới từ `RobotTimeService` trên robot và dùng giờ `Asia/Ho_Chi_Minh` (UTC+7).
Nếu chưa đồng bộ giờ, thẻ báo đang đồng bộ; nếu chưa có lịch bật, thẻ báo chưa có lịch.

Firmware nhận tóm tắt thời tiết qua WSS Device Bridge bằng lệnh JSON:
{"type":"display.weather","summary":"Đông Hưng Thuận 28°C, nhiều mây"}

`summary` là chuỗi UTF-8 tối đa 192 byte; dữ liệu chỉ giữ trong RAM và sẽ mất khi robot khởi động
lại. Thẻ tách nhiệt độ °C/°F nếu có, rồi hiển thị địa điểm và điều kiện trong bố cục riêng.

## Cập nhật thời tiết theo từng robot

Robot gửi `weather.current` kèm địa điểm NVS qua WSS đã xác thực. Server trả kết quả trên
chính RPC của robot đó, không dùng địa điểm mặc định chung cho fleet. Thay địa điểm sẽ bỏ
kết quả đang tải của địa điểm cũ. Dữ liệu Open-Meteo được cache theo tên địa điểm 15 phút,
tối đa 256 địa điểm. Robot làm mới mỗi 15–16 phút với jitter; lỗi thử lại sau 2–2,5 phút.
Tắt màn hình chính động hoặc thẻ thời tiết sẽ dừng các lần lấy mới. Worker riêng dùng chung
WSS, không chặn vòng lặp TFT hoặc âm thanh. Dữ liệu cuối được giữ khi lỗi kết nối.

Backend bỏ các tiền tố như "Tỉnh", "Thủ đô", "Thành phố", "Phường" trước khi tra cứu và chỉ tìm tại Việt Nam. Nếu không tìm được phường/xã, server thử thành phố sau dấu phẩy và ghi rõ "theo thành phố". Khi có nhiều kết quả trùng tên, backend ưu tiên thủ đô `PPLC`; ví dụ "Thủ đô Hà Nội" chọn Hà Nội. Open-Meteo không phân giải ổn định tên tỉnh Đồng Tháp, nên truy vấn cấp tỉnh này dùng trung tâm hành chính hiện tại là Mỹ Tho và ghi chú khu vực được ước tính. Các địa danh vẫn mơ hồ sẽ yêu cầu bổ sung tỉnh/thành thay vì chọn ngẫu nhiên.
Nguồn: https://open-meteo.com/en/docs. Provider được tích hợp ở backend và robot hiện gọi qua
Device Bridge `weather.current`; không gửi yêu cầu HTTP thời tiết trực tiếp từ ESP32.

Nền hai thẻ dùng màu phẳng tối kiểu iOS, bỏ gradient và quầng sáng. Nhiệt độ Noto Sans Bold 36 px, thành phố Bold 20 px. Dưới 30°C xanh lá, từ 30 đến dưới 34°C cam, từ 34°C đỏ.

Các nhãn và tiêu đề trên TFT dùng Noto Sans có glyph tiếng Việt. Đồng hồ TFT dùng Noto Sans Bold 36 px cho thời gian ngắn và 20 px khi chuỗi dài; đây là font vector đã raster hóa cho LVGL, không ảnh hưởng font bitmap cỡ nhỏ của OLED. Tạo lại font bằng `scripts/generate_time_font.py` với `--weight regular --size 14|16` hoặc `--weight bold --size 20|36`.

Biểu tượng lịch đặt trước tiêu đề Calendar - What's next. Thẻ bỏ footer và chấm trạng thái cũ; dữ liệu gồm tất cả lịch đang bật, sắp theo giờ đến hạn. Danh sách cuộn dọc từng sự kiện mỗi 3,5 giây, quay về đầu danh sách rồi lặp lại; timer chỉ chạy khi thẻ Calendar đang hiện trên TFT.
