# ESP32 D1 Bluetooth

Đây là subproject chính thức trong `tools/esp32-d1-bluetooth`, dùng ESP-IDF 6.1 để vận hành ESP32-WROOM-32 D1 Mini như A2DP Source. Firmware có trang local để quét, chọn, kết nối và chuyển giữa các loa Bluetooth; sau mỗi kết nối mới, mạch phát âm thử 440 Hz để xác nhận đường âm thanh. Khi chưa có nguồn I2S từ robot, âm thử được dùng thay cho nhạc. Cấp nguồn/nạp firmware bằng cổng USB-C D1 Mini; chưa cần đấu dây với robot.

Project D1 Mini quản lý Bluetooth Classic riêng; firmware XiaoZhi của robot vẫn chạy trên ESP32-S3 và không được nạp lên D1. Kết nối I2S từ robot tới D1 là bước mở rộng phần cứng riêng, chưa thuộc firmware hiện tại. Sơ đồ và trạng thái tích hợp nằm trong [tài liệu mạch Bluetooth của KyNangSo Robot](../../main/boards/kynangso-robot/docs/bluetooth-speaker-wiring.md); tổng quan bo robot ở [readme của kynangso-robot](../../main/boards/kynangso-robot/readme.md). Source tham khảo trình tự API trong ví dụ `examples/bluetooth/bluedroid/classic_bt/a2dp_source` của ESP-IDF. Codec SBC dùng cấu hình stereo 44,1 kHz.

## Build và nạp

Mở PowerShell tại thư mục này. Kích hoạt ESP-IDF đã cài và gọi trực tiếp script để tránh nhầm launcher `idf.py` trên PATH:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
$env:PYTHONUTF8 = '1'
python C:\esp\v6.1\esp-idf\tools\idf.py -B build-v3 -DIDF_TARGET=esp32 build
python -m serial.tools.list_ports -v
python -m esptool --port COM13 flash-id
```

`COM13` là cổng đã nhận trong phiên cài đặt; kiểm tra lại khi đổi cổng USB. Thiết bị đã đọc được: ESP32-D0WD-V3 revision 3.1, flash 4 MB, MAC `30:76:f5:e4:cb:2c`. Chỉ tiếp tục khi đúng chip ESP32 của D1 Mini.

Trước lần nạp đầu, sao lưu toàn bộ 4 MB flash bằng `esptool read-flash 0 0x400000 <backup.bin>`. Trong phiên này tốc độ 460800 khi đọc đã lỗi, vì vậy dùng 115200. Sau khi sao lưu hoàn tất:

```powershell
python C:\esp\v6.1\esp-idf\tools\idf.py -B build-v3 -p COM13 -b 115200 flash
python serial_console.py --port COM13 --reset --seconds 45
```

Build và sdkconfig được sinh trong `build-v3/` của subproject này, cấu hình đầu vào nằm ở `firmware.defaults`; thư mục build được Git bỏ qua. Firmware D1 dùng partition table riêng. Không chạy lệnh flash từ thư mục gốc repo vì đó là project robot S3.

## Wi-Fi local, ghép đôi và điều khiển

Bản firmware tạo mạng Wi-Fi riêng để cấu hình Bluetooth từ điện thoại hoặc máy tính:

- SSID from current source: `KyNangSo Robot Bluetooth`
- Wi-Fi mở, không cần mật khẩu
- Mở `http://192.168.4.1/`

Mạng này nối trực tiếp tới D1 Mini, không cung cấp Internet. Nếu điện thoại tự chuyển sang dữ liệu di động, giữ kết nối Wi-Fi `KyNangSo Robot Bluetooth` rồi mở địa chỉ trên bằng trình duyệt.

Trong trang local, bấm **Quét thiết bị Bluetooth**, chọn loa theo tên, rồi bấm **Kết nối loa đã chọn**. Nếu đang có loa kết nối và cần quét mới, firmware sẽ dừng âm thanh, ngắt loa hiện tại rồi mới bắt đầu scan; chọn loa mới và bấm kết nối. Nếu loa đích đã có trong danh sách scan cũ, có thể bấm kết nối trực tiếp: firmware gửi yêu cầu ngắt A2DP với loa đang dùng, đợi sự kiện xác nhận ngắt rồi mới kết nối địa chỉ mới. Trang hiển thị riêng loa hiện tại, loa đích và trạng thái chuyển. Nếu yêu cầu ngắt thất bại sau các lần thử, firmware giữ trạng thái hiện tại và báo lỗi; người dùng có thể bấm **Ngắt kết nối** rồi thử lại. A2DP chỉ duy trì một loa tại một thời điểm. Bật loa đích ở chế độ pairing và ngắt loa khỏi điện thoại/máy tính nếu thiết bị đó đang giữ kết nối. Danh sách hiển thị RSSI và địa chỉ để phân biệt loa trùng tên. Bấm **Phát tiếng thử nhỏ** để phát lại; **Dừng tiếng thử** tắt âm mà vẫn giữ kết nối.

Tên Bluetooth nội bộ của D1 là `ESP32-D1-Bluetooth`. Pairing kiểu Just Works; PIN legacy mặc định `0000`.

## Đưa điều khiển về webserver và MCP của robot

Wi-Fi AP và trang riêng của D1 hiện là cách bring-up trực tiếp. Khi tích hợp hoàn chỉnh, người dùng sẽ vào Wi-Fi/IP của robot và chỉ dùng một giao diện: webserver ESP32-S3 hoặc lệnh MCP. Không cần cấp thông tin Wi-Fi nhà cho D1 và không cần kết nối điện thoại vào AP riêng của D1.

Luồng dự kiến có hai đường độc lập:

- **Điều khiển:** webserver/MCP trên S3 → `RobotController`/`BluetoothService` dùng chung → UART nội bộ → D1. D1 trả danh sách thiết bị, trạng thái scan, kết nối, stream và lỗi. Dùng kết quả scan để phân giải tên loa; nếu tên khớp nhiều địa chỉ thì yêu cầu chọn rõ ràng. Khi đổi loa, D1 dừng stream, ngắt loa cũ, chờ xác nhận rồi kết nối loa mới.
- **Âm thanh:** ngõ I2S loa hiện tại của S3 → D1 → Bluetooth A2DP → loa. UART chỉ mang lệnh/trạng thái, không mang PCM.

Các chân UART **đề xuất, chưa đấu/kiểm tra thực tế** theo sơ đồ hiện tại: S3 GPIO10 TX → D1 IO19 RX; S3 GPIO21 RX ← D1 IO18 TX; nối GND chung. Cần xác nhận pad và dây thực tế trước khi thêm vào harness. Khi chạy chế độ tích hợp, AP/web riêng của D1 sẽ tắt hoặc chỉ giữ làm chế độ bảo trì; đây là mục tiêu kiến trúc, chưa có trong image hiện tại.

API dự kiến trên webserver robot: `GET /api/bluetooth/status`, `POST /api/bluetooth/scan`, `GET /api/bluetooth/devices`, `POST /api/bluetooth/connect`, `POST /api/bluetooth/disconnect`. MCP dùng các tool riêng `self.robot.bluetooth.scan`, `.connect`, `.disconnect`, `.status`. Cả hai adapter gọi cùng service trên S3, không tự gửi UART riêng và không duy trì hai trạng thái Bluetooth khác nhau. Phần giao diện/API/MCP/UART này chưa được triển khai; D1 hiện vẫn được điều khiển trực tiếp qua trang `192.168.4.1`.

Theo dõi các mốc log: `FOUND` → `PAIRING: SUCCESS` nếu có ghép đôi mới → `CONNECTED` → `CODEC: ready=1` → `AUDIO: STARTED` → `TEST_TONE: complete`. Chỉ log kết nối chưa chứng minh loa đã phát ra âm thanh; cần người nghe xác nhận.

Console dùng 115200 baud, kết thúc lệnh bằng Enter:

| Lệnh | Tác dụng |
| --- | --- |
| `status` | Tên loa đích, kết nối, streaming, số mẫu PCM, thời gian âm thử còn lại |
| `tone` | Phát lại sáu tiếng bíp khi stream đang hoạt động |
| `stop` | Tắt âm thử; vẫn giữ stream im lặng |
| `scan` | Quét lại khi chưa kết nối; firmware cũng tự thử lại |
| `pin 0000` | Lưu PIN legacy bốn chữ số và khởi động lại |
| `restart` | Khởi động lại D1 Mini |
| `help` | In danh sách lệnh |

Ví dụ gửi lệnh mà không mở monitor tương tác:

```powershell
python serial_console.py --port COM13 --command status --seconds 3
python serial_console.py --port COM13 --command tone --seconds 10
python serial_console.py --port COM13 --command stop --seconds 3
```

Mỗi lần kết nối loa mới sẽ tự phát âm thử một lần. Muốn thử lại thì bấm **Phát tiếng thử nhỏ** hoặc gửi `tone`. Nếu loa giữ kết nối với điện thoại/máy tính khác hoặc không ở pairing, firmware báo lỗi kết nối; log liệt kê tên và địa chỉ đã quét thấy. Chỉ mở một chương trình serial tại một thời điểm.

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
- Đã nạp riêng app tại offset `0x10000` trên COM13, 115200 baud; esptool báo `Hash of data verified`. Boot log xác nhận DHCP `192.168.4.1`, AP mở `KyNangSo Robot Bluetooth`, HTTP server và A2DP profile sẵn sàng.
- D1 khởi động GAP scan thành công. Chủ robot đã xác nhận ở lần chạy trước truy cập `192.168.4.1`, quét và kết nối `Tronsmart Element Groove` thành công. Bản vừa nạp thêm hành vi ngắt loa hiện tại khi yêu cầu quét lại; vẫn cần thử trực tiếp đổi loa thứ hai qua trang web và xác nhận nghe tiếng thử.
- Host dùng để nạp vẫn nối Wi-Fi `RUBY`, nên chưa gọi trực tiếp HTTP API của D1 từ máy này. Để thử bằng điện thoại, kết nối Wi-Fi mở `KyNangSo Robot Bluetooth` rồi vào `http://192.168.4.1/`.
- Log và bản sao lưu nằm trong `tmp/d1-mini-bt-backup/`, không đưa vào Git. Đã format source C và build firmware; firmware I2S và luồng MCP/webserver tập trung vẫn chưa được tích hợp.
