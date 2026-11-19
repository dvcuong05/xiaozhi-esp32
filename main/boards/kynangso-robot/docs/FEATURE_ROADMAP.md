# Feature plan — KyNangSo Robot

Cập nhật lần cuối: 19/11/2026. Tài liệu này là mục lục trạng thái và đề xuất
tính năng cho firmware custom `kynangso-robot`. Luồng kỹ thuật chi tiết nằm
trong các tài liệu được liên kết và [README phần cứng](../readme.md).

> **Cách đọc nhanh**
>
> ✅ Tính năng đã triển khai được tổng hợp ở cuối tài liệu.
>
> 🧪 Các mục đầu tài liệu là phần còn chờ nghiệm thu trên robot thật.
>
> 💡 Backlog là ý tưởng đề xuất, chưa phải cam kết triển khai.

## 🏷️ Quy ước trạng thái

- ✅ **Hoàn thành**: phần mã nguồn và tích hợp đã có; bằng chứng build, triển khai
  hoặc kiểm tra robot được ghi riêng, không suy ra từ dấu `[x]`.
- 🧪 **Nghiệm thu còn mở**: implementation đã có nhưng còn bước xác nhận trên
  phần cứng/bản firmware hiện tại.
- 💡 **Backlog đề xuất**: chưa chốt phạm vi hoặc điều kiện tiên quyết; chưa phải
  cam kết triển khai.
- ⏸️ **Tạm hoãn**: giữ quyết định hiện tại, không tính là việc đang làm.

## 🧪 Nghiệm thu còn mở

Các mục dưới đây không phải feature thiếu mã nguồn. Chúng cần thiết bị thật,
thẻ nhớ hoặc nhiều robot để xác nhận hành vi cuối cùng.

- [ ] **Theme tùy chỉnh V2 và GIF**: sau khi nạp firmware hiện tại, thử lưu,
  preview và khôi phục theme hình học; thử trường hợp không có microSD và có
  microSD; kiểm tra mapping đủ 21 biểu cảm, GIF lỗi/thiếu, chuyển biểu cảm khi
  phát nhạc SD và mức dùng PSRAM. Người dùng đã xác nhận GIF Cherry PoC chạy
  mượt; kết quả đó chưa thay cho kiểm tra editor V2 trên bản hiện tại.
- [ ] **microSD trên cấu hình đang dùng**: thử khởi động lại nhiều lần, thao tác
  đọc/ghi, phát MP3, mở album ảnh và GIF; thử thiếu thẻ, lỗi mount, kết nối lại
  và tháo an toàn. Đối chiếu sơ đồ với pin map hiện hành trong README; không
  tháo thẻ khi đang I/O.
- [ ] **Lịch và âm thanh nhắc**: kiểm tra nhắc lặp qua các ngày, chờ đồng bộ NTP,
  chuông khi robot đang phát media/nghe/nói, âm thanh fallback khi không có SD,
  và chạm xác nhận/snooze. Countdown/Pomodoro dùng thời gian đơn điệu và không
  khôi phục sau reboot theo quyết định hiện tại.
- [ ] **Nhật ký offline**: bật/tắt chủ động, xác nhận mặc định tắt, kiểm tra file
  `/robot/logs/system.jsonl`, xoay vòng/giới hạn dung lượng và bảo đảm không có
  log được gửi qua mạng. [Workflow](features/diagnostics/README.md).
- [ ] **Device Bridge nhiều robot**: đã provision và thử một robot thật cùng
  client giả lập; xác nhận thêm ít nhất hai robot thật trực tuyến đồng thời và
  kiểm tra lệnh/MCP luôn đi đúng `device_id`.
- [ ] **Smoke test sau các thay đổi gần đây**: xác nhận OLED theo pin SDA1/SCL2,
  TFT đúng chiều, motor/Stop, mic/loa, touch, Wi-Fi reconnect, QR và chuyển giao
  diện giữa mắt robot, nhạc/radio, TV và lịch.

### 📺 Live TV — nghiệm thu mở rộng tùy chọn

Chủ robot đã xác nhận TV chạy liên tục hơn 25 phút với hình và tiếng ổn; đây là
mốc nghiệm thu thực tế đã đạt. Nếu cần tăng độ tin cậy trước khi mở rộng nguồn
kênh hoặc số robot, có thể chạy thêm soak test 30–60 phút và đo RAM/FPS/WSS,
đứt mạng, cùng độ lệch A/V. Đây là kiểm tra tăng cường, không phải điều kiện để
coi feature đã triển khai.

Chi tiết và bằng chứng: [kế hoạch Live TV](features/live-tv/PLAN.md) và
[workflow firmware](features/live-tv/README.md).

## 💡 Backlog đề xuất — chưa chốt triển khai

Thứ tự dưới đây là gợi ý theo mức phụ thuộc, không phải cam kết hay thứ tự bắt
buộc. Trước khi chọn một mục, cần chốt hành vi người dùng, nguồn dữ liệu/phần
cứng, giới hạn tài nguyên và phần nào thuộc MCP server.

### 📡 Dịch vụ và dữ liệu

- [ ] **Tin tức và dữ liệu thị trường Việt Nam**: cải thiện trải nghiệm hỏi tin,
  giá và mã chứng khoán, có thể hiển thị tóm tắt/biểu đồ trên TFT. Tái sử dụng
  hợp đồng hiển thị `display.news` hiện có; chốt provider, giấy phép dữ liệu,
  attribution, đơn vị và tần suất cập nhật ở MCP server trước khi mở rộng.
  Firmware chỉ nhận nội dung đã rút gọn. Tham khảo
  [hợp đồng hiển thị tin tức](features/news/README.md).
- [ ] **Kết quả xổ số tỉnh/thành và Vietlott**: tìm nguồn chính thức, thời điểm
  cập nhật và quyền sử dụng; MCP truy vấn theo đài/ngày, robot đọc kết quả và
  có thể trình bày hiệu ứng quay số trên TFT. Không dùng hiệu ứng quay làm
  nguồn kết quả ngẫu nhiên hay thay cho dữ liệu công bố.
- [ ] **Nhà thông minh**: xác định thiết bị và API cần điều khiển trước. Với
  SmartThings, ghép TV/tài khoản và xác nhận quyền API; backend giữ credential,
  firmware chỉ gửi yêu cầu MCP. IR là đường điều khiển riêng, không giả định
  thay thế hoàn toàn SmartThings.
- [ ] **Gói dịch vụ và thanh toán**: trước tiên xác định gói, quyền lợi, chủ thể
  sở hữu robot và quy tắc gia hạn/thu hồi. Sau đó thiết kế entitlement giữa
  `myrobot.kynangso.com`, MCP server và firmware; chưa thêm cơ chế thanh toán
  hoặc khóa tính năng phía robot khi nghiệp vụ chưa chốt.

### 💬 Trải nghiệm hội thoại

- [ ] **Gia sư học tập**: gom Toán, Lý, Hóa, Văn, Sinh, Sử (Việt Nam và thế giới)
  vào một trải nghiệm; xác định cách hỏi/giải thích theo độ tuổi và dùng MCP cho
  nội dung cần tra cứu.
- [ ] **Gia sư ngôn ngữ**: Anh, Pháp, Trung, Hàn, Nhật, Thái, Nga và Việt; chốt
  các chế độ luyện nói, sửa câu và bài tập thay vì chỉ liệt kê ngôn ngữ.
- [ ] **Kể chuyện và hài hước cho trẻ**: xác định nhóm tuổi, độ dài, giọng đọc,
  nội dung phù hợp và cách dừng/phát tiếp.
- [ ] **Gói âm thanh và phản hồi chạm**: hiện đã có âm báo chạm ngắn; cân nhắc
  cho người dùng chọn thêm âm thanh theo cảm xúc từ SD. Chuyển động bánh xe theo
  chạm vẫn phải tùy chọn, mặc định tắt và chỉ mở sau khi xác nhận điều kiện an
  toàn cũng như không làm gián đoạn audio/hội thoại.
- [ ] **Nhắc lịch âm**: tra cứu sự kiện âm lịch theo ngày đã có qua MCP và TFT;
  trước khi thêm nhắc lặp âm lịch, chốt quy tắc năm nhuận, ngày 30 không tồn
  tại, nguồn dữ liệu và hành vi khi server mất kết nối.
- [ ] **Thần số học/tử vi**: nếu triển khai, xác định đây là nội dung giải trí,
  cách hỏi dữ liệu cần thiết và cách tránh trình bày suy đoán như thông tin chắc
  chắn.

### 🛠️ Phần cứng và tích hợp tại nhà

Các mục này chỉ chuyển sang implementation sau khi có đúng module, pin, driver,
nguồn cấp và cách điều khiển để kiểm tra trên robot.

- [ ] **Tránh vật cản VL53L1X**: module hiện chưa lắp. Xác nhận giao tiếp, vị trí,
  ngưỡng khoảng cách và phản ứng an toàn; lỗi hoặc dữ liệu cũ phải dừng chuyển
  động thay vì tiếp tục routine.
- [ ] **Đèn Edison trái phải hoặc phía sau**: led sẽ như là hơi thở, biểu cảm, chuẩn đoán, signal của robot. Có các mcp đều khiển riêng hoặc tùy kich bản mà các đèn được hiệu chỉnh, animation phù hợp. Ngôn ngữ ánh sáng cho các đèn quy ước trạng thái khởi động, ready/listening/processing/speaking, vui khi chạm, di chuyển/rẽ, vật cản,  dừng khẩn cấp, pin yếu và lỗi. Tách hiệu ứng khỏi state machine; xác nhận driver, PWM và tổng dòng trước khi bật hiệu ứng dài hoặc theo nhạc. Hỗ trợ bật tắt hoàn toàn tiết kiệm pin.
- [ ] **Điều khiển hồng ngoại**: xác nhận module phát; nếu cần học remote thì
  chọn thêm module thu. Bắt đầu với tập lệnh và model TV/điều hòa cụ thể.
- [ ] **Camera giao thông TP.HCM và camera IP gia đình**: hai use case riêng;
  cần xác nhận API/URL, quyền truy cập, codec/độ phân giải và khả năng giải mã
  trên robot. Không mặc định Live TV HLS hỗ trợ được mọi camera.
- [ ] **Loa Bluetooth**: chỉ làm prototype khi có nhu cầu rõ; chọn phần cứng
  co-processor, đường PCM, độ trễ, audio focus và reconnect trước.
- [ ] **Hồ sơ/tài nguyên robot trên SD**: chỉ mở rộng khi có cấu hình cụ thể cần
  chia sẻ giữa các máy; schema phải validate và có fallback. Không lưu Wi-Fi,
  token hoặc bí mật dạng rõ trên thẻ.

## ⏸️ Tạm hoãn và quyết định đã chốt

- Tạm hoãn pause nhạc/radio bằng TTP223. Chạm đôi hiện dừng playback và đưa
  robot về `idle`; chạm đơn chuyển bài chỉ áp dụng trong playlist album/ca sĩ
  từ SD.
- Countdown/Pomodoro không cần khôi phục sau reboot; tiếp tục dùng bộ đếm đơn
  điệu khi robot đang chạy.
- Laser chưa đưa vào roadmap triển khai. Nếu có nhu cầu tương tác sau này, ưu
  tiên LED chỉ điểm an toàn và rà soát phần cứng trước.

## ✅ Hoàn thành

Các mục đã rời backlog implementation. Trạng thái kiểm chứng cụ thể được ghi
trong feature doc hoặc README; dấu hoàn thành không có nghĩa mọi tổ hợp phần
cứng/nguồn dữ liệu đều đã được kiểm thử.

### 🧩 Nền tảng và vận hành

- **Firmware và dashboard** — source được chia theo board/core/feature/display/
  integration/web; HTTP và MCP dùng chung controller; dashboard điều khiển
  motor, routine và thiết lập robot. [README board](../readme.md)
- **Flash 16 MiB** — partition riêng cho N16R8, NVS mở rộng, hai OTA slot và
  vùng assets. [Cấu hình board](../readme.md)
- **Device Bridge và OTA** — token riêng mỗi robot, enrollment/provision USB,
  registry nhiều thiết bị; cập nhật firmware có kiểm tra tương thích, SHA-256
  và rollback của bootloader. [Device Bridge](features/device-bridge/README.md) ·
  [OTA](features/firmware-ota/README.md)

### 🎨 Màn hình và biểu cảm

- **TFT/OLED** — cấu hình qua web/MCP, font tiếng Việt cho OLED chạy chữ, màn
  hình thông tin động, QR và điều phối quyền hiển thị. [README board](../readme.md) ·
  [Màn hình thông tin](features/info-shuttle/README.md)
- **Theme robot** — 21 biểu cảm; theme LVGL dựng sẵn; editor JSON màu/hình học;
  upload nhiều GIF, mapping 21 trạng thái, preview trong trình duyệt và chỉ nạp
  GIF cần dùng từ SD. [Workflow và giới hạn](features/face-themes/README.md)

### 🎵 Âm thanh và nội dung microSD

- **Nhạc/radio** — Zing MP3 chạy trong MCP server, radio là provider riêng;
  player chung hỗ trợ metadata/lyrics; TFT có radio status, spectrum cho nhạc/
  radio và khẩu hình khi robot nói. [Sơ đồ MCP music/radio](features/music-radio-mcp-flow.svg)
- **microSD** — quản lý file/thư mục, phát MP3/Ogg/Opus, tìm và chuyển bài trong
  playlist SD, xem JPEG/album, chuông lịch có fallback khi thiếu thẻ; nhật ký
  offline chỉ lưu khi người dùng bật. [README board](../readme.md) ·
  [Nhật ký offline](features/diagnostics/README.md)
- **TTP223** — chạm đơn chọn biểu cảm hoặc chuyển bài trong playlist SD; chạm
  đôi điều khiển hội thoại/dừng media; nhấn giữ dừng motor. Pause media chưa hỗ
  trợ. [README board](../readme.md)

### ⏰ Tiện ích và giải trí

- **Lịch cá nhân** — báo thức, nhắc việc lặp, countdown, Pomodoro; trang `/time`,
  API/MCP và UI TFT theo từng mode. [README board](../readme.md)
- **Ngày âm lịch và sự kiện V1** — hiển thị ngày âm Việt Nam trên `/time` và MCP
  `self.calendar.lunar` hỗ trợ ngày âm hôm nay, đổi dương → âm, âm → dương
  offline theo UTC+7; phát hiện tháng nhuận để hỏi lại thay vì đoán. Không cần
  worker hoặc dữ liệu lưu trên robot. MCP server `lunar_calendar_events` lấy
  sự kiện tháng từ Nhật Nguyệt, cache có giới hạn và gửi tối đa ba tiêu đề qua
  Device Bridge; robot tính ngày âm offline rồi vẽ tờ lịch màu trên TFT trong
  5 giây. Form nhắc việc vẫn hiển thị ngày âm ngay dưới trường chọn ngày. Nhắc
  việc lặp theo ngày âm còn nằm trong kế hoạch. [Workflow và giới hạn](../readme.md).
- **Thời tiết và Random Wheel** — Open-Meteo qua MCP/WSS, làm mới theo yêu cầu;
  vòng quay 2–16 lựa chọn, tiếng Việt/English, giữ nhãn và hiệu ứng pháo giấy.
  [Màn hình thông tin](features/info-shuttle/README.md) ·
  [Random Wheel](features/name-wheel/README.md)
- **Live TV** — catalog kênh phía MCP, trang `/tv`, MCP search/play/status/stop;
  video MJPEG và audio Opus với giới hạn tài nguyên, tên kênh tiếng Việt trên
  TFT. Chủ robot xác nhận hình/tiếng ổn và chạy liên tục hơn 25 phút.
  [Workflow](features/live-tv/README.md) ·
  [Kết quả kiểm chứng](features/live-tv/PLAN.md)

### 📚 Tài liệu tham khảo

- [Sơ đồ mạch hiện hành (PNG)](circuit_diagram.png) · [SVG](circuit_diagram.svg).
- [Tổ chức module và luồng MCP](../readme.md).
- Kế hoạch backend đi kèm nằm trong repo `xiaozhi-kynangso-services`; trạng
  thái deploy/backend cần được xác nhận tại tài liệu của repo đó, không suy ra
  từ việc firmware đã build.
