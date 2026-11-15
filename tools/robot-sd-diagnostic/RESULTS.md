# Kết quả trên robot ngày 14/11/2026

**Kết quả cuối:** sau khi chủ robot ngắt hoàn toàn nguồn rồi cấp lại, kiểm tra
SPI đọc thành công và firmware chính đã mount thẻ, index được 8 bài nhạc.
Mapping CS48/CLK39/MOSI38/MISO40 hoạt động trong lượt kiểm tra này.

- Thiết bị: ESP32-S3 revision v0.2, COM13; web `192.168.1.14`.
- Mapping lấy từ `config.h`: CS48, CLK39, MOSI38, MISO40.
- Chủ robot xác nhận thẻ Yoosee ghi nhãn 8 GB, nguồn module 3,3 V.
- Nạp diagnostic vào RAM qua ROM UART, không ghi flash/thẻ và không format.
  Sau mỗi lần chạy, script reset để chạy lại firmware chính.

## Firmware chính

Yêu cầu mount qua web được tiếp nhận nhưng thất bại:

```text
gpio: conflict found for GPIO[48]
sdspi_transaction: cmd=5, R1 response: command not supported
sdmmc_sd: sdmmc_init_sd_ssr: sdmmc_send_cmd returned 0x107
RobotSD: SD mount failed: ESP_ERR_TIMEOUT (CS48/SCK39/MOSI38/MISO40)
```

Lỗi xảy ra trong khởi tạo thẻ trước khi mount hệ thống file. Cảnh báo GPIO48
chỉ cho biết chân đã được đánh dấu sử dụng lúc cấu hình lại; chưa chứng minh
LED hoặc ngoại vi khác tranh chân. Source hiện đặt LED là `GPIO_NUM_NC`.

## Diagnostic SPI trực tiếp

Không khởi tạo LED/TFT/PSRAM/Wi-Fi. Gửi lệnh SD bằng SPI mode 0 và CS thủ công.

| Lượt | Kết quả |
| --- | --- |
| 400 kHz | CMD0 trả `00`, khác đáp ứng idle `01` |
| 400 kHz, thêm pull-up MISO nội bộ | Có CMD0=`01`, nhưng CMD8/R7 sai; không có khối đọc CRC hợp lệ |
| 100 kHz | CMD0=`01`, CMD8/R7=`01/000001aa`, ACMD41=`00`, OCR=`80ff8000`; đọc CSD 16 byte CRC16=`00ae` khớp; CID và các lần đọc sector sau đó thất bại |
| 100 kHz, đổi thứ tự đọc sector trước | CMD0 không đáp ứng (`ff`), cả khi bật pull-up MISO nội bộ |

MISO khi CS cao trả 64/64 byte `ff` trong các lượt có đo, với và không có
pull-up nội bộ. Phép đo này không xác nhận điện trở pull-up ngoài đủ tốt.
Không có lần đọc sector 512 byte nào vượt qua CRC; chưa xác nhận hoạt động
ổn định ở 400 kHz, 1 MHz hay 10 MHz.

CSD trước khi ngắt nguồn đọc được một lần là `007f00321b5980f26ebb7fff1640009b`, cấu trúc v1,
dung lượng giải mã khoảng 127.139.840 byte. Thông tin này không khớp nhãn
8 GB, nhưng do giao tiếp đang không ổn định nên cần đọc lại/đối chiếu trên
đầu đọc PC hoặc thẻ khác; chưa kết luận thẻ giả hoặc hỏng.

## Đối chiếu sau khi ngắt hoàn toàn nguồn

Chủ robot xác nhận đã ngắt nguồn robot/module khoảng 5 giây rồi cấp lại.
Chạy lại diagnostic RAM trên COM13:

```text
CMD0 R1=01
CMD8 R1=01 R7=000001aa
ACMD41 R1=00
CMD58 R1=00 OCR=c0ff8000
CMD17 LBA0 at 100 kHz: CRC16=5c36 received=5c36 PASS
CMD10/CID: CRC16=8c0b received=8c0b PASS
CMD9/CSD: CRC16=fd1a received=fd1a PASS
CSD=400e00325b5900003bff7f800a4000eb
CMD17 LBA0 at 400 kHz, 1 MHz, 10 MHz: CRC16 PASS at every speed
ACMD13 at 400 kHz: R1=00, second status=00, CRC16=f022 PASS
```

CSD v2 lần này khai báo 8.053.063.680 byte, phù hợp nhãn khoảng 8 GB.
Giá trị khoảng 127 MB của lượt trước không phải bằng chứng đáng tin cậy
về dung lượng thẻ. Không kết luận thẻ giả hoặc hỏng từ kết quả cũ.

Sau khi script reset về firmware chính, serial xác nhận:

```text
I (1042) RobotSD: SD ready: 00000, SPI2 10MHz
I (1762) RobotSD: Music index ready: 8 tracks, truncated=0
I (1762) RobotSD: SD worker stack low-water mark: 5020 bytes
```

Lượt boot này không thấy cảnh báo conflict GPIO48. Robot có khởi tạo Wi-Fi
nhưng lần đọc web tại IP cũ timeout; kết luận mount/index dựa trên serial.
Log được giữ cục bộ tại `run-output-coldpower.log`.

## Giới hạn và bước tiếp theo

Đã chứng minh có trao đổi SD đúng và một khối CSD có CRC hợp lệ trên mapping
hiện tại, và lượt sau ngắt nguồn đã đọc sector đúng CRC ở các tốc độ nêu trên.
Chưa thể chứng nhận từng dây bằng điện hoặc xác định riêng trạng thái thẻ,
module, nguồn và tiếp xúc dây là nguyên nhân ban đầu. Các lần nạp RAM chỉ reset
MCU, không ngắt nguồn module SD. Ngắt nguồn hoàn toàn đã khôi phục hoạt động
trong lần thử này; nếu lỗi tái diễn cần đối chiếu với thẻ tốt khác, kiểm tra
pull-up/tiếp xúc và đo nguồn dưới tải. Chưa thử ghi file hoặc độ ổn định dài hạn.

Log chi tiết được giữ cục bộ trong `run-output*.log` (không theo dõi bằng Git).
