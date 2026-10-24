# JK BMS Studio — Windows BLE (C++20 + WebView2)

Ứng dụng kết nối JK BMS bằng BLE của PC, đọc trực tiếp các thông số pin và cấu hình. Mã nguồn native C++ và giao diện HTML/CSS/JavaScript.

## Cài đặt / Build

1. Mở `JK_BMS_PC.sln` trong Visual Studio 2026, chọn **Release | x64**.
2. Restore NuGet Packages để lấy native SDK `Microsoft.Web.WebView2`.
3. Chọn **Build → Rebuild Solution**.
4. Mở EXE, quét BLE, chọn JK BMS, kết nối và để giao thức AUTO.
5. Nếu chạy EXE thủ công, giữ `web/index.html` và `web/settings.js` trong thư mục `web` cạnh EXE (project sẽ copy tự động sau Build).

## Tính năng

- **Tổng quan:** điện áp/công suất/dòng/SOC, cell, trạng thái MOSFET.
- **Cell pin:** thẻ từng cell, min/max/trung bình, chênh lệch mV và biểu đồ.
- **Bảo vệ:** giải mã Settings frame `0x01` theo bảng JK02_24S/JK02_32S hoặc những offset JK04 được xác minh; điện áp cell OVP/UVP và hồi phục, quá dòng sạc/xả và độ trễ, quá nhiệt/thấp nhiệt, cân bằng, MOS và dung lượng.
- **Chỉnh sửa:** nhập thông số, hiển thị điểm thay đổi, kiểm tra giới hạn, xác nhận `GHI`, gửi từng thanh ghi một, tự đọc lại frame `0x01` ít nhất **2 lần**; chỉ báo hoàn tất khi giá trị đọc lại trùng.
- **Control:** charge/discharge MOSFET, active balance. Đổi trạng thái chỉ khi đã nhận dạng đúng protocol và có cấu hình mới, cần xác nhận `GHI`.
- **Reboot:** lệnh `0x66` từ nguồn giao thức tham chiếu, yêu cầu xác nhận `REBOOT`. Không thể cam kết khởi động lại thành công chỉ từ phản hồi gửi BLE.
- **Sao lưu:** xuất JSON chứa giá trị *đọc từ BMS* kèm model, firmware, serial và MAC BLE.
- **Khôi phục:** tải JSON, chỉ cho cùng model/firmware/serial/MAC, xem trước thay đổi, ghi từng tham số kèm đọc lại. **Không tự động ghi khi chọn file.**
- **Diagnostics:** log GATT, khung 0x01/0x02/0x03, xuất RAW.

## An toàn khi cấu hình

Ghi tham số bảo vệ có thể khiến BMS ngắt tải, sạc hoặc bảo vệ không đúng. Chỉ sử dụng sau khi kiểm tra đúng model/firmware và giá trị an toàn với loại cell, hệ pin. Không thay nhiều ngưỡng quan trọng cùng lúc khi chưa biết ý nghĩa. 
- C++ kiểm tra cấu hình còn mới (trong 30 giây), dữ liệu từ frame CRC hợp lệ, model và giao thức đã nhận dạng từ telemetry, phạm vi giá trị và quan hệ ngưỡng bảo vệ; chỉ có một lệnh ghi đang xử lý.
- Thành công gửi GATT **không có nghĩa** BMS đã lưu. Chương trình phải đọc lại `0x01` hai lần trùng giá trị.
- Những thanh ghi JK04 chưa có bản đồ xác nhận bị khóa; các tham số hỗ trợ sẽ có thể chỉnh sửa.
- Gói backup không thay thế việc đối chiếu các ngưỡng thực tế trước khi restore.
- Chưa có kiểm thử ghi BMS thật trong CI; chỉ bộ giải mã, encoder và state machine phía UI được kiểm thử tự động. Việc kiểm tra ghi sau này phải có thiết bị và tải an toàn.

## Nguồn tham chiếu kỹ thuật

- [syssi/esphome-jk-bms BLE protocol](https://github.com/syssi/esphome-jk-bms/blob/main/docs/protocol-design-ble.md)
- [syssi/esphome-jk-bms decoder & settings register map](https://github.com/syssi/esphome-jk-bms/tree/main/components/jk_bms_ble)
- `src/jk_protocol.h` và `src/jk_settings.h` chứa bảng mã hóa/giải mã.
- `tests/jk_protocol_tests.cpp`, `tests/webui_smoke.cjs`, `tests/settings_ui_smoke.cjs` là kiểm thử hồi quy trong GitHub Actions.
