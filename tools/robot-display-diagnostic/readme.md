# Chẩn đoán riêng TFT và OLED của KyNangSo Robot

Bài thử ESP-IDF 6.1 chạy hoàn toàn trong RAM bằng `esptool load-ram`.
Không dùng lệnh `flash`/`write-flash` với project này. Không ghi NVS, phân vùng,
firmware hoặc assets. Bấm RST hay cấp nguồn lại sẽ chạy firmware đang lưu.
Chế độ chạy RAM là chức năng thử nghiệm của ESP-IDF.

Build:

```powershell
.\tools\robot-display-diagnostic\build.ps1
```

Build, nạp vào RAM và ghi nhận log 30 giây (đóng monitor COM6 trước):

```powershell
.\tools\robot-display-diagnostic\build.ps1 -Run -Port COM6 -Seconds 30
```

Chương trình tiếp tục hiển thị mẫu sau khi hết thời gian đọc log. Muốn quay lại
firmware robot, bấm RST. Thử nghiệm tạm thời không có Wi-Fi/giọng nói.

TFT lấy chân từ `main/boards/kynangso-robot/config.h`: SPI3, MOSI18, CLK17,
CS46, DC3, RST8, BL9. Backlight bật ngay; thanh đỏ/lục/lam/trắng đổi mỗi hai
giây, thử sáu tổ hợp swap XY và RAM gap (0,0)/(80,0)/(0,80). SPI giảm xuống
10 MHz để kiểm tra đường truyền. Đợi ít nhất 12 giây để quan sát mọi tổ hợp.
Mỗi lần ghi pixel phải nhận callback hoàn tất trước khi tái sử dụng buffer.
Log hoàn tất SPI không chứng minh tấm nền đã nhận/hiển thị đúng: SPI không có
phản hồi ACK từ màn này, cần đối chiếu hình ảnh thực tế.

OLED dùng chân mặc định đã xác nhận trong log: SDA2, SCL1, 128x32 SSD1306.
Đọc mức hai chân ở chế độ input có pull-up trước khi tạo I²C, probe 0x3C/0x3D
với timeout 50 ms, khởi tạo/tương phản tối đa, ép sáng toàn màn 5 giây, sau đó
gửi mẫu sọc ở 100 kHz với timeout 100 ms. Nếu OLED lỗi, TFT vẫn tiếp tục chạy.
Mức GPIO là digital 0/1, không thay số đo điện áp.

microSD được giữ CS ở mức cao, motor IN1–IN4 ở mức thấp. Không khởi tạo bus SD,
D1, LVGL, audio hoặc cấu hình đã lưu. Các module còn nối vật lý vẫn tiêu thụ
điện; bài thử không cô lập được tác động của chúng lên đường nguồn.

Diễn giải:

- TFT có màu, OLED timeout: đường TFT có thể hoạt động ở cấu hình/tải này;
  kiểm tra tiếp OLED và luồng init của firmware chính.
- TFT SPI hoàn tất nhưng vẫn đen qua mọi tổ hợp: còn khả năng dây/control,
  nguồn tại TFT, tấm nền hoặc loại controller; chưa đủ kết luận hỏng S3.
- SCL1 hoặc SDA2 thấp ngay khi input có pull-up: đường đó bị giữ thấp trong
  bài thử không chạy SD/D1/LVGL; cần cô lập dây/module để phân biệt nguyên nhân.
- Cả hai hiện mẫu: so sánh firmware chính, NVS, tần số bus và tải hoạt động.

Kết quả điều tra: [display-investigation-20261111.md](../../main/boards/kynangso-robot/docs/display-investigation-20261111.md).
