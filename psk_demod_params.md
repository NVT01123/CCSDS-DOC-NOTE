`PSKDemodModule` kế thừa toàn bộ tham số của `BaseDemodModule`, rồi thêm các tham số dành cho chuỗi:

`Base processing → AGC → RRC → (carrier PLL) → Costas loop → (DC block) → OQPSK delay → M&M clock recovery → soft symbols`

## Tham số riêng của PSK

| Tham số | Ý nghĩa |
|---|---|
| `constellation` | Kiểu điều chế: `bpsk`, `qpsk`, `oqpsk`, hoặc `8psk`. Tham số bắt buộc. Nó chọn Costas loop bậc 2, 4, 4, hoặc 8 tương ứng. `oqpsk` cũng tự đặt khoảng SPS mong muốn thành 1.6–2.4. |
| `rrc_alpha` | Roll-off factor của bộ lọc Root Raised Cosine (RRC). Bắt buộc. Giá trị nhỏ cho băng hẹp hơn nhưng nhạy hơn với sai lệch timing; lớn hơn dùng nhiều băng thông hơn nhưng thường dễ khóa hơn. Thường `0.25–0.5`; mặc định khai báo `0.35`. |
| `rrc_taps` | Số hệ số FIR của RRC, mặc định `31`. Nhiều taps hơn cho lọc sắc hơn/ít ISI hơn, đổi lại tốn CPU và tăng trễ. |
| `pll_bw` | Loop bandwidth của Costas loop. Bắt buộc. Lớn hơn: bắt lệch pha/tần nhanh hơn nhưng nhạy nhiễu hơn; nhỏ hơn: sạch và ổn định hơn nhưng khóa chậm, khó bám Doppler hoặc offset. Pipeline thường dùng `0.001–0.02`. |
| `costas_max_offset` | Giới hạn lệch tần Costas loop được phép bám. Nếu truyền vào, đơn vị là **Hz**; mã chuyển nó sang rad/sample theo `final_samplerate`. Nếu không truyền: giới hạn là `1.0 rad/sample`, riêng chế độ carrier là `0.2 rad/sample`. Đặt quá thấp có thể không khóa; quá cao dễ khóa nhầm/nhiễu. |
| `post_costas_dc` | Bật `CorrectIQBlock` sau Costas loop, loại DC offset còn lại trước clock recovery. Hữu ích cho một số tín hiệu; mặc định `false`. |
| `has_carrier` | Bật chế độ BPSK có carrier/AM subcarrier. Chỉ hợp lệ cho `constellation: "bpsk"`; nếu dùng với QPSK/OQPSK/8PSK module ném lỗi. |
| `carrier_pll_bw` | Loop bandwidth của `PLLCarrierTrackingBlock` ở chế độ `has_carrier`. Chỉ bắt buộc khi `has_carrier: true`. Nó bám carrier trước khi Costas loop xử lý dữ liệu BPSK. |
| `carrier_pll_max_offset` | Biên độ offset tối đa cho carrier PLL, đơn vị nội bộ rad/sample. Mặc định `3.14` (xấp xỉ π). Đây không được chuyển từ Hz trong module này. |
| `clock_alpha` | Cách rút gọn để đặt gain cho M&M clock recovery: `clock_gain_mu = clock_alpha`, `clock_gain_omega = clock_alpha² / 4`. Nếu đặt các gain riêng bên dưới, các giá trị riêng sẽ ghi đè kết quả từ `clock_alpha`. |
| `clock_gain_omega` | Gain của vòng điều khiển tốc độ symbol (`omega`) trong M&M clock recovery. Lớn hơn giúp bám sai số samplerate nhanh hơn, nhưng quá lớn gây dao động/jitter. Giá trị nội bộ mặc định là `8.7e-3² / 4`. |
| `clock_mu` | Pha thời điểm lấy mẫu khởi tạo, thường trong khoảng `[0, 1)`. Giá trị nội bộ mặc định `0.5`. Chủ yếu ảnh hưởng quá trình khóa ban đầu. |
| `clock_gain_mu` | Gain điều chỉnh pha timing trong M&M recovery. Cao hơn bám timing nhanh hơn nhưng nhạy nhiễu. Giá trị nội bộ mặc định `8.7e-3`. |
| `clock_omega_relative_limit` | Giới hạn độ lệch tương đối của số mẫu/symbol mà clock recovery được phép bám quanh `final_sps`. Mặc định nội bộ `0.005`, tức ±0.5%. Tăng khi clock của máy phát/thu sai nhiều; không nên quá cao vì có thể bám nhầm. |

## Tham số kế thừa từ `BaseDemodModule`

PSK module cũng nhận các tham số sau:

- Đầu vào: `baseband_format`, `samplerate`, `buffer_size`, `iq_swap`
- Đặc tính tín hiệu: `symbolrate`
- Tiền xử lý: `dc_block`, `freq_shift`, `agc_rate`
- Resampling: `min_sps`, `max_sps`, `custom_samplerate`
- Doppler: `enable_doppler`, `doppler_alpha`, `satellite_frequency`, `satellite_norad`, `qth_lon`, `qth_lat`, `qth_alt`, `start_timestamp`
- Debug: `dump_intermediate`

Ý nghĩa chi tiết của nhóm này giống phần `BaseDemodModule` đã giải thích trước đó. Trong PSK, `symbolrate` đặc biệt quan trọng vì nó xác định RRC và giá trị `final_sps` đưa vào M&M clock recovery.

Ví dụ cấu hình QPSK cơ bản:

```json
{
  "constellation": "qpsk",
  "samplerate": 120000,
  "symbolrate": 48000,
  "rrc_alpha": 0.35,
  "rrc_taps": 31,
  "pll_bw": 0.002,
  "costas_max_offset": 5000,
  "agc_rate": 0.01
}
```

Một chi tiết đáng chú ý: các giá trị `0` trong `getParams()` của nhóm `clock_*` không phải là các giá trị tuning tốt để chủ động truyền vào. Nếu bạn truyền chúng thật sự, chúng sẽ ghi đè các mặc định nội bộ và có thể làm clock recovery không bám được. Khi không cần tinh chỉnh, tốt nhất không đưa các khóa `clock_alpha`, `clock_gain_omega`, `clock_mu`, `clock_gain_mu`, `clock_omega_relative_limit` vào preset.

Phần định nghĩa tham số nằm tại [module_psk_demod.h](/home/nvt/Workspace/SatDump/src-core/pipeline/modules/demod/module_psk_demod.h:52), còn thứ tự áp dụng và điều kiện của từng tham số nằm tại [module_psk_demod.cpp](/home/nvt/Workspace/SatDump/src-core/pipeline/modules/demod/module_psk_demod.cpp:20).