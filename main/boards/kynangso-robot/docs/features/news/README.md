# Hiển thị tin tức trên robot KyNangSo

## Luồng

Backend KyNangSo gọi RSS khi người dùng hỏi và trả kết quả cho MCP client. Với
lệnh `news_headlines` mặc định, server gửi một lệnh hiển thị ngắn qua WSS Device
Bridge. Firmware không kết nối tới báo, không polling tin và không tải bài đầy đủ.

```text
MCP server -> WSS Device Bridge: display.news
            -> RobotController::ShowNews
            -> TFT notification hiện có
            -> OLED marquee hiện có (nếu đã bật và sẵn sàng)
```

## Lệnh WSS

```json
{
  "type": "display.news",
  "headline": "Tiêu đề tin ngắn",
  "source": "Tên nguồn báo"
}
```

`headline` tối đa 160 byte UTF-8; `source` tối đa 48 byte. Lệnh hợp lệ được xếp
qua `Application::Schedule()` để phần cập nhật UI chạy ở luồng ứng dụng, không
chặn vòng lặp mạng hoặc audio. Khi UI nhận lệnh:

- TFT dùng notification sẵn có, tối đa 12 giây.
- Nếu OLED phụ đang bật và driver sẵn sàng, OLED dùng marquee sẵn có trong 12
  giây; nếu không, firmware chỉ cập nhật TFT.
- Hết hạn, hai màn hình tự trở về nội dung trước đó theo vòng đời notification
  hiện có.

Không tạo task, timer polling, buffer bài báo, file trên microSD hay dữ liệu NVS
mới. Chỉ chuỗi tiêu đề và tên nguồn đi tới robot. Màn TFT vẫn tuân theo cơ chế
notification/UI hiện có; firmware không thay trạng thái kết nối, playback, TV,
hoặc cấu hình Info Shuttle.

## An toàn và giới hạn

- Lệnh chỉ tới từ Device Bridge đã xác thực và giới hạn theo robot của Access
  Point.
- Firmware kiểm tra kiểu, độ dài và ký tự điều khiển trước khi xếp lệnh hiển thị.
- Tin tức chỉ hiện sau yêu cầu MCP; không phát sinh lưu lượng mạng khi robot rảnh.
- OLED là tùy chọn. Robot không có OLED vẫn nhận tin qua TFT.
- Build firmware chưa đồng nghĩa đã nạp hay kiểm tra vật lý. Cần xác minh riêng
  TFT, OLED tùy chọn và các màn hình đang phát khác trên robot thật.

Phần RSS, cache và quyền sử dụng được mô tả trong `docs/features/news/README.md`
của repo server `xiaozhi-kynangso-services`.
