# MrKenDDo Robot Feature Roadmap

This is the working checklist for the custom `mrkenddo-robot` firmware. Keep
board-specific plans, decisions, hardware notes, and validation results here.

## Working rules

- [ ] Run `idf-init` before every firmware build or flash session.
- [ ] Keep robot-specific changes under `main/boards/mrkenddo-robot/` whenever
      the feature does not need to be shared by other boards.
- [ ] Build before flashing and record any physical-hardware limitation.
- [ ] Update this file whenever a feature is completed, deferred, or changed.
- [ ] Update readme.MD whenever a feature is completed, deferred, or changed in Vietnamese within explanation flow.

## Completed

- [x] Custom board definition, display, robot drive, OLED controller, eyes, and
      web server separated under `mrkenddo-robot`.
- [x] Web server robot control with motor movement and MCP-backed text prompt.
- [x] Secondary OLED clock, status, marquee, text normalization, Vietnamese
      character support, and configuration from the web server.
- [x] Secondary OLED rotation, contrast, invert, and persistent settings.
- [x] TFT rotation and panel RAM offset configuration for the 240x240 display.
- [x] TFT color inversion corrected for the installed panel.
- [x] Custom cyan robot eyes on a black TFT background; legacy XiaoZhi emoji
      hidden for the custom board.
- [x] Robot eye wake animation, blinking, glances, curiosity, and speaking /
      listening state animations.
- [x] Boot console typing effect on the TFT with a scrolling-style log area.
- [x] Boot and robot-eye wake audio assets integrated; typing sound removed.
- [x] Firmware build and flash verified on the ESP32-S3 robot hardware.

## Current change to verify

- [ ] ...

## Hardware validation backlog

- [ ] Verify both motors move in the same physical forward direction and the
      same physical reverse direction.
- [ ] Verify TFT orientation with the installed horizontal mounting and the
      current panel offset `(80, 0)`.
- [ ] Verify OLED orientation and status layout on the actual 128x32 module.
- [ ] Verify INMP441 capture stability during wake-word detection and speech.
- [ ] Check boot timing from power-on through Wi-Fi, web server, IP, and ready.

## Planned features

- [ ] Thêm webserver feature các option moving of robot: Hiếu động (đi chuyển tới lui vài giây, rồi xoay trái phải vài lần và cứ thế), Dancing (tương tự nhưng nhịp điệu nhanh hơn, sôi động hơn)
- [ ] Mắt robot có hiếu động, liếc trái phải và chớp mắt nhưng chậm quá, nên liếc nhiều sang 2 bên kiểu láo lia quan sát ấy
- [ ] Phần date dd/mm/yy mình muốn chuyển sang dd-MMM. eg: 32-Dec
- [ ] Hiện tại các tính năng còn mang tính manual chưa tích hợp sâu với chatbox xiaozhi kiểu như ra lệnh: "di chuyển như tiến về phía trước", "nhảy nhót", "đứng im", "hiện mode đồng hồ màn hình phụ", màn hình chính như "hiện mặt buồn đi", hiện địa chỉ IP (thì robot sẽ show và đọc lên)"...Mình muốn
mỗi tính năng, mỗi linh kiện pin vào GPIO đều hỗ trợ MCP ra lệnh tích hợp với chatbox xiaozhi (tham khảo nguồn decision notes thử)

## Decisions and notes

- The custom board uses the community XiaoZhi transport and MCP protocol; do
  not replace the transport with a private web-server protocol without a new
  design decision.
- `hudrucan/xiaozhi-desk-robot` is the current reference repository for robot
  UI and behavior ideas.
- The secondary OLED boot layout is intentionally independent of the saved
  OLED mode, including `OFF`, so the local web-server address is always shown.
- The OLED returns to the saved mode at `READY`, which is also when the custom
  robot eyes wake on the TFT.


## Implementation priority and status

The roadmap is now grouped by risk and dependency so the firmware stays responsive:

- P0 - Done: MCP tools for movement, stop, lively routine, dance, emotion, OLED mode, IP and status.
- P0 - Done: webserver buttons for the lively and dance routines, including stop and running status.
- P0 - Done: lively routine repeats continuously until stop or a new movement command; dance remains bounded.
- P0 - Done: MCP can display a user-provided temporary scrolling sentence on the secondary OLED without replacing the saved marquee text.
- P1 - Done: faster idle eye glances, blinks and curiosity movement.
- P1 - Done: OLED status date format `dd-MMM`.
- P1 - Done: routine execution runs in its own FreeRTOS task and does not block the audio/main loop.
- P2 - Pending hardware verification: motor direction and routine timing on the assembled robot.
- P2 - Pending hardware verification: MCP voice commands, OLED mode switching and IP reporting.
- P2 - Pending hardware verification: TFT/OLED orientation, eye animation and boot timing.
- P3 - Future: extend MCP coverage to additional board-specific controls only after each GPIO has a safe, documented action.

The P2 items are intentionally kept separate from the implementation status: a successful build cannot verify wiring, motor polarity, display orientation or end-to-end voice behavior.
