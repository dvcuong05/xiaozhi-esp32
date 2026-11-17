# Thư viện GIF dùng trong Dashboard

Các tệp JavaScript này được nhúng vào firmware để Dashboard hoạt động cả khi không có Internet. Chúng chỉ được gửi xuống trình duyệt khi mở endpoint chuẩn hóa GIF; robot không chạy JavaScript giải mã ảnh.

- `omggif.js`: decoder/reader GIF từ [deanm/omggif](https://github.com/deanm/omggif), giấy phép MIT (thông tin bản quyền nằm đầu tệp).
- `gifenc.js`: encoder/quantizer GIF từ [mattdesl/gifenc](https://github.com/mattdesl/gifenc), giấy phép MIT trong `gifenc-LICENSE.md`.

Không sửa trực tiếp mã vendor. Khi cập nhật thư viện, giữ lại thông báo bản quyền/giấy phép và kiểm tra lại endpoint ghép `exports` cùng Web Worker.
