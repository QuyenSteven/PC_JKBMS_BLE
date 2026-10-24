# JK BMS Studio — Windows C++ BLE + WebView2

Ứng dụng giám sát JK BMS bằng Bluetooth LE của PC, giao diện web nhúng trong EXE, không cần Python.

## Kiến trúc

- `src/main.cpp`: Windows WebView2 host, BLE worker, GATT FFE0/FFE1, kết nối và gửi dữ liệu sang WebUI.
- `src/jk_protocol.h`: bộ ghép khung JK, kiểm tra CRC, giải mã tự động JK02_24S / JK02_32S; JK04 chỉ giải mã điện áp cell.
- `web/index.html`: Overview, Cell pin (thẻ cell + biểu đồ), Control trạng thái MOSFET, Protection đọc trạng thái khung Settings, BLE Diagnostics.
- `tests/jk_protocol_tests.cpp`: test khung BLE/decoder.
- `tests/webui_smoke.cjs`: test JavaScript và cập nhật DOM cell, Control/Protection, trạng thái kết nối.
- `.github/workflows/windows-build.yml`: build và kiểm thử trên Windows; xuất EXE kèm WebUI.

## Build trên Visual Studio 2026

1. Cập nhật mã nguồn từ `main`.
2. Mở `JK_BMS_PC.sln`, chọn `Release | x64`.
3. Restore NuGet Packages (WebView2), rồi Rebuild Solution.
4. Mở EXE, **Quét BLE → chọn JK BMS → Kết nối**, để protocol **Tự nhận dạng (AUTO)**.

## Trang giao diện

- **Tổng quan:** điện áp/dòng/công suất/SOC, cell, MOSFET (dữ liệu BLE thực tế).
- **Cell pin:** danh sách từng cell, min/max/avg, độ lệch mV so với trung bình, biểu đồ so sánh.
- **Điều khiển:** hiển thị trạng thái Charge/Discharge/Balance. Các lệnh ghi/reboot vẫn khóa vì chưa kiểm tra an toàn theo model.
- **Bảo vệ:** khung `0x01` khi được nhận được ghi nhận là *Settings RAW*. Những thông số chưa xác minh offset hiển thị `--`, không đoán giá trị.
- **BLE / Nhật ký:** bộ đếm khung 0x01/0x02/0x03, thời điểm cuối, sao chép/xóa log, xuất dữ liệu RAW.

## Giao thức

Thiết bị sử dụng FFE0/FFE1. Sau khi bật notification, chương trình hỏi `0x97` (thông tin thiết bị); khung `0x03` nhận được kích hoạt yêu cầu trạng thái `0x96`. Nếu không có telemetry `0x02`, chương trình thử lại `0x96` với chu kỳ; khi đã có dữ liệu tự phát thì chỉ thử lại nếu stream gián đoạn.

Bộ giải mã JK02 được kiểm tra bằng mẫu tổng hợp và đã hiển thị số thực từ BMS trong quá trình thử nghiệm trên máy người dùng. **Chưa xác minh đầy đủ mọi dòng model/firmware JK**; JK04 chưa có dữ liệu pack đã xác nhận.

### Giới hạn an toàn

Các lệnh thay đổi ngưỡng bảo vệ, charge/discharge MOSFET, cân bằng, khôi phục cấu hình và reboot BMS **chưa được thực hiện**. Cần xác định đúng model/firmware và khung lệnh ghi, ACK và đọc lại trước khi mở tính năng. EXE/CI build thành công không thay thế thử nghiệm với BMS thật.
