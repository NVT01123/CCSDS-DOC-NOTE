`CCSDSConvConcatDecoderModule` nhận soft symbols từ demodulator và thực hiện chuỗi:

`điều chỉnh I/Q → Viterbi (convolutional) → NRZ-M → tìm ASM/CADU → derandomize → Reed–Solomon → xuất frame`

Lưu ý: `getParams()` hiện trả về rỗng, nhưng constructor vẫn yêu cầu nhiều khóa JSON. Dưới đây là toàn bộ tham số mà mã thực sự đọc.

| Tham số | Kiểu / giá trị | Vai trò |
|---|---|---|
| `constellation` | `bpsk`, `bpsk_90`, `qpsk`, `oqpsk` | Bắt buộc. Kiểu soft symbols đầu vào và các pha Viterbi thử khi khóa. `bpsk_90` dành cho BPSK quay 90° kèm hoán đổi IQ đặc thù. `oqpsk` cho phép Viterbi kiểm tra sự không ổn định I/Q của OQPSK. |
| `cadu_size` | số nguyên, bit | Bắt buộc. Tổng kích thước CADU/frame, tính cả ASM. Module dùng để tìm khung và xác định kích thước output. Nếu không chia hết cho 8, mã sẽ pad frame đến byte kế tiếp. |
| `viterbi_ber_thresold` | float | Bắt buộc. Ngưỡng BER để Viterbi quyết định khóa/mất khóa. Tên khóa có lỗi chính tả `thresold` — phải dùng đúng tên này. Thường `0.300`; ngưỡng thấp hơn chặt hơn nhưng khó khóa, cao hơn dễ khóa nhầm. |
| `viterbi_outsync_after` | số nguyên | Bắt buộc. Số lần kiểm tra BER không hợp lệ liên tiếp trước khi Viterbi bỏ trạng thái khóa. Lớn hơn chịu được fade/ngắt quãng tốt hơn nhưng phát hiện mất khóa chậm hơn. Thường `20`. |
| `conv_rate` | `"1/2"`, `"2/3"`, `"3/4"`, `"5/6"`, `"7/8"` | Tốc độ mã convolutional K=7. Mặc định `"1/2"`. Các rate lớn hơn dùng depuncturing trước Viterbi. Không dùng giá trị khác các chuỗi trên. |
| `nrzm` | boolean, mặc định `false` | Bật giải mã vi sai NRZ-M sau Viterbi và trước deframer. Dùng khi transmitter đã differential-encode bitstream. |
| `asm` | chuỗi hex | Sync word/ASM để deframer tìm đầu frame, ví dụ `"1ACFFC1D"`. Mặc định CCSDS `0x1ACFFC1D`. |
| `iq_invert` | boolean, mặc định `false` | Hoán đổi I/Q của soft symbols trước Viterbi, dùng khi mapping QPSK/BPSK thực tế bị đảo. |
| `ccsds` | boolean, mặc định `true` | Chỉ quyết định phần mở rộng output: `true` xuất `.cadu`, `false` xuất `.frm`. Nó không bật/tắt thuật toán CCSDS hay Reed–Solomon. |
| `derandomize` | boolean, mặc định `true` | Bật de-randomizer CCSDS trên payload CADU. |
| `derand_after_rs` | boolean, mặc định `false` | Chọn thứ tự derandomize: `false` làm trước Reed–Solomon; `true` làm sau Reed–Solomon. Phải khớp thứ tự phía phát. |
| `derand_start` | byte offset, mặc định `4` | Offset byte bắt đầu derandomize trong CADU. Mặc định 4 để bỏ qua ASM 4 byte. |
| `rs_i` | số nguyên | Bắt buộc. Độ sâu interleaving Reed–Solomon. `0` tắt hoàn toàn RS; `1` là một codeword; `4` hoặc `5` là các giá trị CCSDS thường gặp. |
| `rs_type` | `"rs223"` hoặc `"rs239"` | Loại RS khi `rs_i != 0`. Mặc định nội bộ `"none"` nhưng khi RS bật, phải đặt rõ `rs223` hoặc `rs239`, nếu không module báo lỗi. `rs223` là RS(255,223), 32 byte parity; `rs239` là RS(255,239), 16 byte parity. |
| `rs_dualbasis` | boolean, mặc định `true` | Chọn biểu diễn dual basis CCSDS khi giải mã RS. Đặt `false` cho vệ tinh dùng conventional basis. |
| `rs_fill_bytes` | số nguyên, mặc định `-1` | Số “fill bytes” của shortened/punctured RS. `-1` nghĩa là codeword đủ độ dài; giá trị khác `-1` khiến decoder bổ sung byte 0 trước giải mã, rồi bỏ lại chúng sau đó. |
| `rs_usecheck` | boolean, mặc định `false` | Nếu `true`, chỉ ghi CADU khi tất cả nhánh RS giải mã thành công. Nếu `false`, frame vẫn được ghi dù RS báo không sửa được lỗi. |

Ví dụ preset CCSDS phổ biến:

```json
{
  "constellation": "bpsk",
  "cadu_size": 8192,
  "viterbi_ber_thresold": 0.300,
  "viterbi_outsync_after": 20,
  "conv_rate": "1/2",
  "derandomize": true,
  "derand_start": 4,
  "rs_i": 4,
  "rs_type": "rs223",
  "rs_dualbasis": true,
  "rs_usecheck": true
}
```

Các tham số tối thiểu để module không lỗi khi khởi tạo là: `constellation`, `cadu_size`, `viterbi_ber_thresold`, `viterbi_outsync_after`, và `rs_i`.

Phần đọc tham số và thứ tự xử lý nằm ở [module_ccsds_conv_concat_decoder.cpp](/home/nvt/Workspace/SatDump/src-core/pipeline/modules/ccsds/module_ccsds_conv_concat_decoder.cpp:17).