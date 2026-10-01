Type: prototype
Status: resolved

## Question

Thiết kế UI/UX dashboard v1: layout trang, cách hiển thị trạng thái 4 module (card/list/table?), 1 trang tổng hay trang riêng từng module? Làm prototype throwaway (HTML/JS tĩnh) để xem trước, không cần backend thật.

## Answer

3 biến thể dựng trên chính `attak-iot-firmware/data/index.html` (switcher `?variant=A|B|C`), verify bằng browser thật (agent-browser CLI, không phải giả lập): **A** card grid, **B** sidebar + detail panel, **C** table dày + event log real-time. Screenshot + record verify trong session.

**Verdict**: ghép **sidebar của B** (chọn module bên trái, status dot) + **detail panel/event log của C** (panel chi tiết module đang chọn phía trên, event log toàn hệ thống cuộn real-time phía dưới). Lý do: sidebar để sẵn chỗ cho nút pentest từng module (sniff/replay/clone/jam) ở phase sau; event log giữ tinh thần "pentest tool" theo dõi hoạt động toàn hệ thống thay vì chỉ module đang chọn.

Đã fold vào `main` (commit `f62ec65`). Toàn bộ 3 variant + switcher gốc giữ nguyên làm primary source trên nhánh `prototype/dashboard-ui` (commit `074ce18`), không xóa.
