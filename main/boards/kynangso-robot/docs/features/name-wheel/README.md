# Vòng quay chọn ngẫu nhiên

Người dùng nêu danh sách 2-16 lựa chọn trong hội thoại. Mỗi mục có thể là tên người, hoạt động,
món ăn hay phương án bất kỳ, ví dụ “đọc sách”, “đi dạo”, “nghe nhạc”. MCP backend chuẩn hóa
khoảng trắng và Unicode, từ chối mục trống/trùng hoặc dài quá 48 byte UTF-8, rồi chọn kết quả
bằng `secrets.choice`. Tiếng Việt và English đều được hỗ trợ; nhãn được giữ nguyên khi chuyển lên
robot.
Backend gửi danh sách cùng lựa chọn thắng tới đúng robot đã chọn qua Device Bridge WSS; ESP32 không
tự chọn lại kết quả và không lưu danh sách vào NVS hay microSD.

## Câu nói mẫu cho người dùng

- “Chọn giúp tôi một món: phở, bún bò, cơm tấm hay bánh cuốn.”
- “Bốc thăm người thuyết trình giữa An, Bình, Chi và Dũng.”
- “Cuối tuần nên đi bộ, xem phim hay đọc sách? Chọn giúp tôi.”
- “Quay ngẫu nhiên một màu trong xanh, vàng, đỏ và tím.”

Nêu rõ từ 2 đến 16 lựa chọn khác nhau. Trợ lý gửi danh sách tới đúng robot; robot
trình chiếu vòng quay trên TFT, còn kết quả được chọn ở backend.

## Luồng MCP và firmware

1. MCP tool `random_choice(choices, device_id?)` nhận lựa chọn từ câu nói của người dùng, ví dụ
   “Chọn giúp tôi giữa đọc sách, đi dạo và nghe nhạc” hoặc “Pick one: reading, walking, or music”.
2. Backend kiểm tra dữ liệu, chọn một tên ngẫu nhiên không thiên lệch và gọi
   `device_bridge_hub.request` với `type=wheel.start`.
3. Firmware xác thực cấu trúc lệnh, giới hạn số tên/độ dài và chuyển cập nhật giao diện qua
   `Application::Schedule`; không chạy animation trong callback WSS.
4. TFT chỉ hiện tiêu đề hardcode “Random Wheel” và bánh xe chia lát màu, mỗi lựa chọn nằm trên
   lát tương ứng; nhãn dài tự rút gọn bằng dấu ba chấm. Kim cố định ở đỉnh bánh xe; lát thắng,
   nhãn và kết quả footer dùng chung phép tính góc để luôn khớp với `choice` backend đã chọn.
   Bánh xe giảm tốc trong khoảng 5 giây, chạy pháo hoa nhiều đợt và giữ kết quả thêm 10 giây trước
   khi trở về giao diện trước đó.
5. MCP trả `choice` để trợ lý thông báo kết quả. Chỉ đọc kết quả sau phản hồi thành công của tool.

## Giới hạn và ưu tiên màn hình

Vòng quay không điều khiển motor, không phát âm thanh riêng và không tạo yêu cầu mạng từ robot.
Một lần chạy tối đa 16 lựa chọn, mỗi mục tối đa 48 byte UTF-8. Yêu cầu bị từ chối khi TFT chưa sẵn sàng,
đang hiện QR/đồng hồ hoặc cảnh báo thời gian/Bluetooth/Live TV, đang phát nhạc/radio hoặc đang trình chiếu ảnh.
Các hiệu ứng audio và trạng thái robot không bị thay đổi.

Các nhãn LVGL nằm ở `lv_layer_top`, nên không tự nhận font mới từ theme. Khi theme nạp lại font,
`OnThemeTextFontChanged` cập nhật fallback cho toàn bộ nhãn trước khi font cũ được giải phóng; nếu
không, LVGL có thể dereference font pointer cũ khi tính layout và làm robot reset. Tiêu đề dùng
Noto Sans Bold 20 px; nhãn lựa chọn và kết quả dùng Noto Sans 14 px board-local, có fallback sang
theme cho ký tự ngoài bộ glyph tiếng Việt.

Nếu có nhiều robot trực tuyến, cần chỉ định `device_id`; backend không tự đoán robot đích. Việc
chọn kết quả diễn ra trên backend, nên animation TFT chỉ là trình diễn cho kết quả đã chọn.
