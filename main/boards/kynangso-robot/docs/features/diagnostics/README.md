# Nhật ký chẩn đoán offline

Nhật ký offline là tùy chọn riêng với console WebSocket. Người dùng bật hoặc tắt trong mục **Chẩn đoán** trên dashboard; lựa chọn được lưu trong NVS và mặc định tắt.

## Luồng hoạt động

1. Khi bật, firmware chỉ đưa các mã sự kiện hệ thống cố định vào hàng đợi 16 mục. Hàng đợi đầy thì bỏ sự kiện mới và tăng bộ đếm; không chặn tác vụ đang phát audio.
2. Worker microSD có sẵn ghi log khi thẻ đã mount, không phát file từ thẻ và không có upload đang chạy. Nếu thẻ bận, worker hoãn xử lý; nếu thẻ không sẵn sàng, sự kiện có thể bị bỏ khi hàng đợi đầy.
3. File được lưu tại `/robot/logs/system.jsonl`; file xoay vòng trước tối đa 32 KiB sang `system.1.jsonl`, tổng dung lượng tối đa khoảng 64 KiB. Khi có thời gian hệ thống hợp lệ, file quá 7 ngày được dọn trong lần ghi kế tiếp.
4. Mở trang **Thẻ nhớ**, vào `/robot/logs` và tải file để đọc. Nội dung là JSON Lines, mỗi dòng có uptime, epoch (0 nếu chưa đồng bộ giờ), loại sự kiện và mã số.

## Sự kiện và quyền riêng tư

V1 ghi mã lý do khởi động lại, trạng thái mount/tháo thẻ và lỗi đọc/ghi microSD. Không ghi nội dung log tự do, prompt, transcript, âm thanh, URL, tên file, SSID hoặc thông tin xác thực. Dữ liệu chỉ nằm trên thẻ; không có kết nối mạng hay gửi log tới MCP/backend.

Tắt công tắc sẽ ngừng nhận sự kiện mới và xóa hàng đợi RAM. File cũ được giữ cho tới khi người dùng xóa qua trang quản lý thẻ hoặc tới khi chính sách tuổi thọ dọn file trong lần ghi sau. Thẻ microSD vẫn là tùy chọn; thiếu thẻ hoặc lỗi ghi không làm thay đổi luồng audio, kết nối hay trạng thái robot.

## Phân biệt với console trực tiếp

Console WebSocket `/api/logs` vẫn chỉ bật khi người dùng mở console; nó chuyển các dòng `esp_log` mới tới trình duyệt và không lưu lịch sử. Nhật ký offline chỉ ghi các mã sự kiện đã định nghĩa, có giới hạn dung lượng và được điều khiển bằng công tắc **Lưu sự kiện hệ thống** tại `/api/diagnostics`.
