# Provision WSS riêng cho từng robot

Một firmware dùng chung cho nhiều KyNangSo Robot. Mỗi robot được cấp `device_id`
và `token` riêng qua mã enrollment dùng một lần; firmware không chứa credential
riêng khi build bình thường. Không có HTTP endpoint công khai trên robot để ghi
credential. Backend registry và công cụ cấp mã do service quản lý.

## Quy trình Windows

1. Build bằng `kynangso-build-firmware.bat` (tự export ESP-IDF 6.1). Nếu PowerShell
   có hàm `idf-init`, chạy hàm đó trước. Không cần repo service bên cạnh hoặc secret
   file. Việc nạp firmware là thao tác riêng; script provisioning **không flash**.
2. Cắm cổng **USB–UART console UART0**, đóng IDF Monitor và các app giữ COM.
   Board hiện dùng UART0, 115200 baud; USB Serial/JTAG native không bật trong variant này.
3. Cài `python -m pip install pyserial`, lấy mã enrollment dùng một lần từ quản trị
   backend rồi chạy từ thư mục gốc repo:

   ```powershell
   python scripts/provision_robot_bridge.py --port COM6 --server https://bridge.example.com --uri wss://bridge.example.com/device/bridge
   ```

   Thay hostname và COM bằng cấu hình thật. `--server` là địa chỉ HTTPS gốc của
   service; tiện ích nối thêm `/device/enroll`. `--uri` là URL WSS robot sử dụng,
   không được suy ra từ phản hồi enrollment. Cả hai hỗ trợ hostname hoặc IPv4,
   port tùy chọn; không hỗ trợ IPv6 literal, userinfo, query hoặc fragment.
4. Tiện ích mở serial với read timeout 250 ms, write timeout 3 giây; không bật
   flow control, không pulse DTR/RTS, không reset vào bootloader. Có thể dùng
   `--retries 5` (1..20), `--baud 115200`. Serial chưa mở được hoặc firmware chưa
   trả `KNSBRIDGE READY` thì báo lỗi **trước khi redeem**, chưa hỏi mã.
5. Nhập mã qua `getpass` (ẩn, không truyền qua command line). Terminal không hỗ trợ
   nhập ẩn thì dừng. POST HTTPS có body `{"code":"..."}`, nhận
   `{"device_id":"...","token":"..."}`. TLS và hostname luôn được xác minh;
   CA riêng dùng `--ca-file C:\path\ca.pem`, không có chế độ bỏ xác minh. Redirect
   bị từ chối; HTTP timeout 20 giây, không tự POST lại vì mã có thể đã được dùng.
6. Tiện ích giữ credential trong RAM, gửi qua USB, chờ ACK tối đa 10 giây/lần.
   Nếu mất kết nối có thể mở lại serial và gửi lại **cùng credential**, không redeem
   lại. Không ghi token ra file hoặc in token/JSON serial/phản hồi HTTP.
7. `KNSBRIDGE ACK` chỉ xuất hiện sau `nvs_set_str` và `nvs_commit` thành công.
   Robot đợi TX tối đa 1 giây, chờ thêm 250 ms rồi reboot. Kiểm tra robot online trên
   backend; ACK không xác nhận TLS WSS hoặc quyền registry đã hoạt động.

Nếu không nhận ACK sau khi redeem, robot có thể đã lưu và reboot. Kiểm tra online
trước khi cấp mã mới; không có token file để khôi phục. Nếu enrollment lỗi/mất phản
hồi, kiểm tra backend vì mã có thể đã tiêu thụ. Revoke/rotate credential cũ qua
registry rồi provision mã mới khi cần.

## UART và lưu NVS

Worker chỉ đọc RX UART console bằng buffer cố định 1024 byte, không dùng `fgets`,
không echo, không thay `stdin`/`stdout`, không thay log hook hoặc gửi JSON vào web
log worker. TX phản hồi qua stdio lock; log bình thường vẫn hoạt động. Nếu UART đã
có driver/consumer khác thì không chiếm cổng và không khởi động provisioning.
Bộ nhận UART giữ stack **4 KiB trên PSRAM**, gồm buffer dòng **1 KiB**;
UART RX giữ **1 KiB nội bộ** (cộng overhead driver/task). Bộ nhận không ghi flash.
Khi có cấu hình mới, chỉ tạo một worker ghi với **stack nội bộ 4 KiB** và bản sao
JSON nội bộ có giới hạn. Parse/commit/ACK/reboot chạy ở worker này để không ghi
NVS từ stack PSRAM. Worker được hủy khi cấu hình không hợp lệ hoặc ghi lỗi;
thiếu bộ nhớ trả `ERROR busy`, không ACK hoặc reboot. Không cấp stack ghi thường
trực trong lúc phát TV. Nếu PSRAM/stack ngoài không sẵn sàng, provisioning không
khởi động và có log chẩn đoán; không tự fallback sang stack nội bộ thường trực.

- Probe: `KNSBRIDGE PING\n` → `KNSBRIDGE READY`.
- Ghi: `KNSBRIDGE {"uri":"wss://.../device/bridge","device_id":"robot_01","token":"..."}\n`.
- Thành công: `KNSBRIDGE ACK`; lỗi: `KNSBRIDGE ERROR invalid_config` hoặc
  `KNSBRIDGE ERROR storage`. Không phản hồi nội dung credential.
- Dòng tràn/NUL bị bỏ im lặng đến newline; fragment quá 5 giây bị bỏ đến newline.
  JSON không biết/prefix khác không được log. JSON cấu hình phải đúng ba trường
  chuỗi, không trùng, không thêm trường, không trailing JSON/Unicode escape.
- URI `wss://`, tối đa 256 ký tự ASCII, host/port hợp lệ; ID 1..64 ký tự
  `[A-Za-z0-9_-]`; token 32..128 ký tự ASCII nhìn thấy (không space/control).

Namespace NVS là **`mcp_bridge`**, key vật lý **`config`** chứa một chuỗi JSON với
các khóa **`uri`**, **`device_id`**, **`token`**. Không ghi ba key độc lập để tránh
trộn credential khi mất nguồn. Mọi thao tác C NVS open/set/commit được kiểm tra;
không ACK/reboot khi ghi lỗi. Boot ưu tiên blob runtime hợp lệ; nếu thiếu/hỏng thì
dùng cấu hình compile legacy hợp lệ; nếu cả hai không hợp lệ, bridge chờ provisioning.
Không thay namespace/key NVS của Wi-Fi và các tính năng khác.

Legacy chỉ được bật khi người build chủ động đặt **một** trong
`XIAOZHI_MCP_DEVICE_WSS_TOKEN` hoặc `XIAOZHI_MCP_DEVICE_WSS_TOKEN_FILE`.
Không đặt biến thì build xóa token compile cũ khỏi cấu hình build. Legacy ID/URI
vẫn theo Kconfig; runtime luôn có ưu tiên. Firmware legacy có credential nhúng,
không phù hợp phân phối cùng binary cho nhiều robot cần danh tính độc lập.

## Bảo mật và kiểm chứng

USB provisioning yêu cầu quyền truy cập vật lý và cho phép thay cấu hình hiện có.
Token trong NVS **không tự được mã hóa bởi feature này**; người đọc được flash/NVS
có thể trích xuất token. Với thiết bị triển khai thực tế, xem xét Flash Encryption,
NVS Encryption và Secure Boot theo quy trình sản xuất riêng, đồng thời thu hồi
credential khi mất thiết bị. Không gửi dump NVS, firmware legacy hoặc capture serial
chứa credential vào báo cáo. Xóa toàn bộ NVS làm mất enrollment và cấu hình khác;
cần provision lại sau thao tác đó.

Host tests kiểm tra HTTPS contract/TLS, từ chối redirect, không retry redeem, serial
timeouts/no-DTR, ACK giữa log noise và không redeem khi COM chưa sẵn sàng.
Build chỉ xác nhận compile/link. Cần kiểm tra phần cứng riêng: UART thực tế, log
stream bật/tắt, dòng tràn và fragment, commit lỗi/mất nguồn, ACK/reboot, lưu qua
reboot, WSS riêng cho hai robot, legacy fallback và revoke/rotate registry. Không
coi build hoặc host mock là kiểm chứng thiết bị/backend end-to-end.

TV yêu cầu audio worker dùng PSRAM qua `StreamPlaybackOptions.external_task_stack`; tùy chọn mặc định tắt, được board bật cho TV. Callback của worker này không được ghi flash/NVS. Các nguồn nhạc/radio/thông báo khác giữ chính sách stack cũ.


## Provision và ghép nối từ MyRobot

MyRobot hỗ trợ ghép robot mới và provision credential qua wizard Web Serial. Dùng
cổng USB-UART UART0 115200 như tiện ích Python. KNSBRIDGE INFO trả
KNSBRIDGE ID <device_id> từ cấu hình NVS hợp lệ; khi chưa provision, firmware
sinh ID ổn định dạng kns-<12 ký tự MAC Wi-Fi STA> từ MAC nhà máy. Lệnh
KNSBRIDGE PAIR <challenge> trả KNSBRIDGE PAIRED <challenge> <device_id> để
portal đối chiếu challenge ngẫu nhiên hết hạn sau 5 phút. Firmware không xuất
token, URI hay blob NVS.

Quy trình: người dùng đăng nhập Google, kết nối đúng cổng robot qua USB, nhập tên
hiển thị, ghép ID nhà máy, kích hoạt gói DEMO nếu được mở, rồi chạy wizard USB để
provision WSS. Mã enrollment nội bộ được dùng một lần ở backend; token WSS chỉ ở
RAM trình duyệt trong lúc ghi và không hiển thị cho người dùng. Firmware mới cần
được nạp trước khi dùng nút ghép robot; firmware cũ không có ID nhà máy/PAIR.

PAIR xác nhận challenge trên serial để giảm nhầm robot và ngăn claim chỉ bằng nhập
device_id; đây chưa phải chứng thực mật mã chống firmware giả lập hoặc client USB
tự tạo. ID dựa trên MAC là định danh công khai. Trước khi mở đăng ký thương mại rộng,
cần bổ sung danh sách ID phát hành tại nhà máy hoặc khóa thiết bị tin cậy được cài
riêng trong quy trình sản xuất.
