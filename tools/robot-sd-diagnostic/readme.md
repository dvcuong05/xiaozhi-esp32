# Chẩn đoán microSD bằng RAM

Chương trình riêng cho ESP32-S3, lấy bốn chân SPI từ `main/boards/kynangso-robot/config.h`.
Không khởi tạo LED, TFT, Wi-Fi hoặc PSRAM. Không ghi/format thẻ và không ghi flash.
Nạp qua ROM UART vào RAM; reset sẽ chạy lại firmware lưu trong flash.

```powershell
.\tools\robot-sd-diagnostic\build.ps1 -Run -Port COM13
```

Đóng Serial Monitor trước khi chạy. Robot tạm dừng hoạt động khi nạp RAM.
Script đọc kết quả rồi reset về firmware chính. Nếu bị ngắt giữa chừng, bấm RST.

Khởi tạo ở 100 kHz, kiểm tra CMD0/CMD8/ACMD41/CMD58, CSD/CID và đọc
sector 0 ở 100 kHz, 400 kHz, 1 MHz, 10 MHz; kiểm tra CRC16 của từng khối đọc.
Nếu CMD0 thất bại, thử lại với pull-up MISO nội bộ (chỉ để chẩn đoán).
Cuối cùng đọc ACMD13/SSR
ở 400 kHz để đối chiếu bước khởi tạo ESP-IDF. Không in nội dung file người dùng.
Kết quả này kiểm tra trao đổi dữ liệu qua SPI; không thay thế đo sóng hoặc kiểm tra dây bằng đồng hồ.

Kết quả kiểm tra trên robot được ghi trong [RESULTS.md](RESULTS.md).
