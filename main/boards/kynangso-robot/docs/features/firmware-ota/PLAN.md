# Kế hoạch OTA firmware

Workflow và ranh giới firmware: [README.md](README.md).

Plan chốt và bằng chứng kiểm thử chung được giữ tại `xiaozhi-kynangso-services/docs/features/firmware-ota/PLAN.md` để không duy trì hai checklist kết quả khác nhau.

Đã hiện thực client board-local, kiểm tra hash/size trong bộ OTA chung, xác nhận image trễ, panel TFT, telemetry WSS và khả năng chờ idle. Build thành công và robot thật đã OTA từ `2.5.3` sang `2.5.4`, giữ Wi-Fi/WSS. Đã thử reset trước xác nhận của bản thử và bootloader quay lại `2.5.4` thành công. USB stable là bản `2.5.4` đã kiểm chứng, rollout OTA đang tắt. Mất mạng giữa lúc tải còn là mục nghiệm thu riêng; không suy ra đã đạt chỉ từ build hoặc backend tests.
