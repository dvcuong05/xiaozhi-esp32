# ESP32 D1 Bluetooth

Đây là subproject chính thức trong `tools/esp32-d1-bluetooth`, dùng ESP-IDF 6.1 để vận hành ESP32-WROOM-32 D1 Mini như A2DP Source. Firmware có trang local để quét, chọn, kết nối và chuyển giữa các loa Bluetooth; sau mỗi kết nối mới, mạch phát âm thử 440 Hz để xác nhận đường âm thanh. Khi chưa có nguồn I2S từ robot, âm thử được dùng thay cho nhạc. Khi thử độc lập có thể cấp nguồn/nạp firmware bằng USB-C D1 Mini. Khi lắp vào robot, sơ đồ ngày 15/11/2026 dùng rail 3,3 V tới đầu vào 3V3 D1, GND chung; không coi pad VCC/5V/VIN là 3V3 và không cấp đồng thời hai nguồn vào cùng rail. Xem [đấu nối robot](../../main/boards/kynangso-robot/docs/bluetooth-speaker-wiring.md).

Project D1 Mini đảm nhiệm Bluetooth Classic A2DP Source; firmware XiaoZhi của robot vẫn chạy trên ESP32-S3 và không được nạp lên D1. Bản tích hợp nhận lệnh điều khiển qua UART và nhận audio I2S từ S3 để phát lên loa Bluetooth; AP riêng của D1 tiếp tục là đường cấu hình/bảo trì. Sơ đồ và trạng thái tích hợp nằm trong [tài liệu mạch Bluetooth của KyNangSo Robot](../../main/boards/kynangso-robot/docs/bluetooth-speaker-wiring.md); tổng quan bo robot ở [readme của kynangso-robot](../../main/boards/kynangso-robot/readme.md). Source tham khảo trình tự API trong ví dụ `examples/bluetooth/bluedroid/classic_bt/a2dp_source` của ESP-IDF. Codec SBC dùng cấu hình stereo 44,1 kHz.

## Build và nạp

Mở PowerShell tại thư mục này. Liệt kê cổng COM để chọn đúng D1 Mini, rồi chạy script; script tự kích hoạt ESP-IDF 6.1, build và nạp riêng app:

```powershell
Get-CimInstance Win32_SerialPort | Format-Table DeviceID, Name
.\flash-d1-bluetooth.ps1 -Port COM13
```

`COM13` là cổng đã nhận trong phiên cài đặt; kiểm tra lại khi đổi cổng USB. Thiết bị đã đọc được: ESP32-D0WD-V3 revision 3.1, flash 4 MB, MAC `30:76:f5:e4:cb:2c`. Chỉ tiếp tục khi đúng chip ESP32 của D1 Mini.

Lệnh nạp hằng ngày tự build rồi chỉ ghi app tại offset lấy từ `flasher_args.json`; bootloader, partition table và NVS được giữ nguyên. Chọn đúng cổng D1 Mini trong Device Manager, ví dụ `COM13`.

Có thể thêm `-SkipBuild` khi vừa build xong và chỉ muốn nạp lại app. Trước lần cài đặt đầu tiên, giữ một bản sao lưu flash đầy đủ nếu muốn khôi phục firmware xuất xưởng. Lệnh script khóa target là ESP32 cổ điển để tránh nạp nhầm firmware D1 lên S3. Muốn xem log sau khi nạp, kích hoạt ESP-IDF trong PowerShell rồi chạy `python serial_console.py --port COM13 --reset --seconds 45`.

Build và sdkconfig được sinh trong `build-v3/` của subproject này, cấu hình đầu vào nằm ở `firmware.defaults`; thư mục build được Git bỏ qua. Firmware D1 dùng partition table riêng. Không chạy lệnh flash từ thư mục gốc repo vì đó là project robot S3.

## Wi-Fi local, ghép đôi và điều khiển

Bản firmware tạo mạng Wi-Fi riêng để cấu hình Bluetooth từ điện thoại hoặc máy tính:

- SSID from current source: `KyNangSo-Robot BT`
- Wi-Fi mở, không cần mật khẩu
- Mở `http://192.168.4.1/`

Mạng này nối trực tiếp tới D1 Mini, không cung cấp Internet. Nếu điện thoại tự chuyển sang dữ liệu di động, giữ kết nối Wi-Fi `KyNangSo-Robot BT` rồi mở địa chỉ trên bằng trình duyệt.

Trong trang local, bấm **Quét thiết bị Bluetooth**, chọn loa theo tên, rồi bấm **Kết nối loa đã chọn**. Nếu đang có loa kết nối và cần quét mới, firmware sẽ dừng âm thanh, ngắt loa hiện tại rồi mới bắt đầu scan; chọn loa mới và bấm kết nối. Nếu loa đích đã có trong danh sách scan cũ, có thể bấm kết nối trực tiếp: firmware gửi yêu cầu ngắt A2DP với loa đang dùng, đợi sự kiện xác nhận ngắt rồi mới kết nối địa chỉ mới. Trang hiển thị riêng loa hiện tại, loa đích và trạng thái chuyển. Nếu yêu cầu ngắt thất bại sau các lần thử, firmware giữ trạng thái hiện tại và báo lỗi; người dùng có thể bấm **Ngắt kết nối** rồi thử lại. A2DP chỉ duy trì một loa tại một thời điểm. Bật loa đích ở chế độ pairing và ngắt loa khỏi điện thoại/máy tính nếu thiết bị đó đang giữ kết nối. Danh sách hiển thị RSSI và địa chỉ để phân biệt loa trùng tên. Bấm **Phát tiếng thử nhỏ** để phát lại; **Dừng tiếng thử** tắt âm mà vẫn giữ kết nối.

Tên Bluetooth nội bộ của D1 là `ESP32-D1-Bluetooth`. Pairing kiểu Just Works; PIN legacy mặc định `0000`.

## Điều khiển tích hợp qua S3

Khi lắp D1 vào robot và nạp cả hai firmware tích hợp, người dùng điều khiển Bluetooth qua Wi-Fi hiện có của robot, không cần chuyển sang AP D1. Trang robot là `/bluetooth`; API và MCP được mô tả trong [tài liệu sơ đồ và tích hợp](../../main/boards/kynangso-robot/docs/bluetooth-speaker-wiring.md).

Giao thức UART2 115200 baud, 8N1, mức 3,3 V:

- S3 GPIO10 TX → D1 IO23 RX
- S3 GPIO21 RX ← D1 IO4 TX
- GND nối chung
- S3 gửi `STATUS`, `AUTO_CONNECT`, `KEEPALIVE`, `SCAN`, `CONNECT <MAC>`, `DISCONNECT`; D1 gửi frame JSON `state` định kỳ và `device` sau mỗi kết quả quét.

D1 chỉ lưu địa chỉ và tên loa sau khi kết nối A2DP thành công. Khi phần mở rộng Bluetooth được bật trên S3, S3 gửi `AUTO_CONNECT`; D1 thử kết nối lại loa đã lưu sau khi Bluetooth sẵn sàng. Quét thủ công vẫn không tự ghép loa đầu tiên: người dùng chọn đúng thiết bị trong danh sách. Nếu loa đã lưu đang tắt, ngoài vùng phủ sóng hoặc đang bận với thiết bị khác, lần kết nối A2DP sẽ thất bại/timeout và D1 thử lại sau 30 giây. Quét thủ công hoặc ngắt kết nối sẽ tạm dừng tự kết nối cho tới khi người dùng kết nối loa mới hoặc S3 khởi động lại với Bluetooth bật. S3 gửi `KEEPALIVE` mỗi 15 giây; nếu mất điều khiển quá 45 giây, D1 dừng thử lại và ngắt kết nối để tránh tiếp tục chạy độc lập. Khi Bluetooth runtime trên S3 tắt, S3 không khởi tạo UART và không gửi các lệnh này. AP mở `KyNangSo-Robot BT` và trang `192.168.4.1` vẫn được giữ làm đường bảo trì độc lập.

Khi tự kết nối thành công, S3 phát âm thanh khởi động sẵn có của robot và TFT hiển thị tên loa. Firmware không đọc tên loa bằng giọng nói: tên Bluetooth có thể tùy ý và hiện chưa có bộ TTS cục bộ để đọc chuỗi này.

**Pin D1 Mini cho bản lắp chồng:** UART2 dùng TX=IO4, RX=IO23; I²S RX dùng BCLK=IO33, WS=IO34, DATA=IO35. Các chân này được chọn để đi dây song song theo dãy pad của hai board; IO4 thay IO2 để tránh chân boot-strap. Phải nạp firmware D1 có pin map này trước khi đấu dây.

**Tình trạng tích hợp hiện tại:** Firmware D1 và firmware S3 đã build thành công với phần ghi nhớ loa và tự kết nối lại. Bản D1 dùng UART điều khiển và I²S RX slave 24 kHz/32-bit, đổi mẫu lên PCM stereo 44,1 kHz cho A2DP. Bản vừa build chưa được nạp lên mạch; UART/I²S trên dây thật và âm thanh từ robot qua loa Bluetooth vẫn cần kiểm tra thực tế.

## Khôi phục

Dùng bản sao lưu đầy đủ từ đúng D1 Mini, không dùng backup robot S3:

```powershell
python -m esptool --chip esp32 --port COM13 --baud 115200 write-flash 0 <backup.bin>
```

Khôi phục toàn bộ flash sẽ đưa firmware, partition table và NVS về trạng thái tại lúc sao lưu. Bản sao lưu nằm trong `tmp/d1-mini-bt-backup/` tại gốc repo và bị Git bỏ qua; không commit vì có thể chứa cấu hình cũ của thiết bị.

## Kết quả cài đặt ngày 27/10/2026

- Đọc đúng chip ESP32-D0WD-V3 revision 3.1, flash 4 MB trên COM13; Bluetooth MAC `30:76:f5:e4:cb:2e`.
- Sao lưu đủ 4.194.304 byte vào `tmp/d1-mini-bt-backup/esp32-3076f5e4cb2c-backup-115200.bin` tại gốc repo. SHA-256: `f58662a41c9d5423ea618cb71353121a034e2feb4a97e69b2718c7e1cbfab1d5`.
- Build subproject tên `esp32_d1_bluetooth` bằng ESP-IDF 6.1 thành công; app 1.333.520 byte, còn 13% phân vùng app `0x177000`. SHA-256: `71d618d154564d68570e7e01c99dc8d3fcbff0ec3614d56f72bf45b3fd183c02`.
- Đã nạp riêng app tại offset `0x10000` trên COM13, 115200 baud; esptool báo `Hash of data verified`. Boot log xác nhận DHCP `192.168.4.1`, AP mở `KyNangSo-Robot BT`, HTTP server và A2DP profile sẵn sàng.
- D1 khởi động GAP scan thành công. Chủ robot đã xác nhận ở lần chạy trước truy cập `192.168.4.1`, quét và kết nối `Tronsmart Element Groove` thành công. Bản vừa nạp thêm hành vi ngắt loa hiện tại khi yêu cầu quét lại; vẫn cần thử trực tiếp đổi loa thứ hai qua trang web và xác nhận nghe tiếng thử.
- Host dùng để nạp vẫn nối Wi-Fi `RUBY`, nên chưa gọi trực tiếp HTTP API của D1 từ máy này. Để thử bằng điện thoại, kết nối Wi-Fi mở `KyNangSo-Robot BT` rồi vào `http://192.168.4.1/`.
- Log và bản sao lưu nằm trong `tmp/d1-mini-bt-backup/`, không đưa vào Git. Các kết quả nạp và thử D1 độc lập ở trên ghi nhận phiên bản ngày 27/10/2026; phần tích hợp web/MCP trên S3 và luồng I2S robot được triển khai/build sau đó, chưa xác minh vật lý.
