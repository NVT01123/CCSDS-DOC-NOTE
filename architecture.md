Ba khái niệm có quan hệ bao-hàm rõ ràng:

```text
Pipeline  = điều phối các bước nghiệp vụ cấp cao
Module    = một bước có input/output, file hoặc stream
Block     = toán tử DSP nhỏ, xử lý mẫu liên tục trong module
```

## 1. Pipeline: “kế hoạch xử lý end-to-end”

`Pipeline` là tầng orchestration. Nó không xử lý mẫu IQ hay frame trực tiếp, mà quyết định:

- Chuỗi bước nào được chạy.
- Input/output của từng bước nằm ở file nào.
- Dừng ở level nào (`soft`, `cadu`, `products`).
- Chạy batch hay ghép streaming.
- Cấu hình profile protocol.

Interface ở [pipeline.h](/home/nvt/Workspace/station/src/core/pipeline/pipeline.h:8) chỉ có bốn trách nhiệm: `configure`, `run`, `live`, `generateProducts`.

Trong hệ thống hiện tại, `MeteorPipeline` là implementation duy nhất và có graph logic cố định:

```text
baseband
  │
  ├── PSKDemodModule ───────────────→ soft
  ├── CCSDSConvConcatDecoderModule ─→ cadu
  └── METEORMSUMRLRPTDecoderModule ─→ products
```

Tuy nhiên đây chưa phải graph engine tổng quát: danh sách `steps` là metadata, còn việc khởi tạo module vẫn là `if (step.module == "...")` trong `MeteorPipeline::run()` ([meteor.cpp](/home/nvt/Workspace/station/src/core/pipeline/pipelines/meteor.cpp:220)). Nghĩa là pipeline có tính khai báo một phần, nhưng execution vẫn hard-code.

## 2. Module: “đơn vị nghiệp vụ có vòng đời”

`ProcessingModule` là abstraction trung tâm ở [module.h](/home/nvt/Workspace/station/src/core/pipeline/module.h:21). Mỗi module có:

- `d_input_file`, `d_output_file_hint`, `d_output_file`.
- `d_parameters`.
- Kiểu I/O: `DATA_FILE`, `DATA_STREAM`, hoặc `DATA_DSP_STREAM`.
- Vòng đời: `setInputType` → `setOutputType` → `init` → `process` → `getOutput`.
- FIFO byte-level và DSP stream complex-level cho các kết nối streaming.

Nói cách khác, module là adapter từ graph cấp pipeline xuống implementation xử lý thực tế. Có ba loại chính:

| Module | Input | Output | Vai trò |
|---|---|---|---|
| `PSKDemodModule` | IQ file hoặc complex DSP stream | `.soft` hoặc byte FIFO | Demodulate OQPSK |
| `CCSDSConvConcatDecoderModule` | `.soft` hoặc byte FIFO | `.cadu` hoặc FIFO | Viterbi, NRZ-M, ASM, RS |
| `METEORMSUMRLRPTDecoderModule` | `.cadu` | PNG + JSON | Decode sản phẩm Meteor |

`FileStreamToFileStreamModule` là base module tái sử dụng cho các chuyển đổi file/byte stream. Nó che giấu open/read/write/close và hỗ trợ cùng lúc file batch lẫn FIFO streaming ([filestream_to_filestream.cpp](/home/nvt/Workspace/station/src/core/pipeline/modules/base/filestream_to_filestream.cpp:20)).

### Module vs pipeline

Một pipeline biết **thứ tự và mục đích** của các bước.
Một module biết **cách hoàn thành một bước**.

Ví dụ pipeline biết “soft symbols phải được FEC decode để tạo CADU”; module CCSDS biết cụ thể cách đọc buffer, chạy Viterbi, tìm ASM, derandomize, Reed–Solomon và xuất CADU ([module_ccsds_conv_concat_decoder.cpp](/home/nvt/Workspace/station/src/core/pipeline/modules/ccsds/module_ccsds_conv_concat_decoder.cpp:106)).

## 3. Block: “toán tử DSP streaming nhỏ”

`dsp::Block<IN_T, OUT_T>` là abstraction thấp hơn module, đặt tại [block.h](/home/nvt/Workspace/station/src/common/dsp/block.h:17).

Mỗi block:

- Có `input_stream` và tự tạo `output_stream`.
- Có một hàm `work()` virtual thực hiện một chunk biến đổi.
- Chạy vòng lặp `work()` trên thread riêng khi gọi `start()`.
- Dừng bằng atomic flag và signal cho stream khi gọi `stop()`.

Đây là mô hình **push/pull bằng double-buffer**:

```text
producer writes writeBuf
  → swap(size)
  → consumer reads readBuf
  → flush()
  → producer được phép ghi chunk tiếp theo
```

`dsp::stream<T>` không phải FIFO nhiều chunk; nó là double-buffer đồng bộ, nên mỗi cạnh DSP chỉ giữ tối đa một block dữ liệu đang chờ xử lý. Điều này đơn giản và tránh cấp phát liên tục, nhưng throughput phụ thuộc vào block chậm nhất.

Các block tiêu biểu:

```text
FileSourceBlock
 → CorrectIQBlock / FreqShiftBlock / SmartResamplerBlock
 → AGCBlock
 → FIR/RRC block
 → CostasLoopBlock
 → DelayOneImagBlock
 → ClockRecoveryMMBlock
```

`FileSourceBlock` là block nguồn: đọc IQ với format đã chọn và chuẩn hoá sang `complex_t` ([file_source.cpp](/home/nvt/Workspace/station/src/common/dsp/io/file_source.cpp:18)). Các block còn lại nhận và phát `stream<complex_t>`.

## 4. Một module có thể là graph của nhiều block

`PSKDemodModule` là ví dụ rõ nhất. Bản thân module là một graph DSP nội bộ:

```text
FileSource
  → optional DC blocker
  → optional frequency shift
  → optional resampler
  → AGC
  → RRC filter
  → Costas PLL
  → optional OQPSK I/Q delay
  → clock recovery
  → soft-symbol serialization
```

Graph được nối trong `init()` ([module_psk_demod.cpp](/home/nvt/Workspace/station/src/core/pipeline/modules/demod/module_psk_demod.cpp:85)). Khi `process()` chạy, module start tất cả DSP blocks rồi đọc output của clock-recovery để chuyển complex samples thành soft bytes và ghi `.soft` hoặc đẩy vào FIFO ([module_psk_demod.cpp](/home/nvt/Workspace/station/src/core/pipeline/modules/demod/module_psk_demod.cpp:124)).

Vì vậy:

```text
Pipeline
 └─ PSK demod module
     └─ N DSP blocks, mỗi block có worker thread riêng
```

## 5. Hai loại streaming khác nhau

Hệ thống có hai tầng streaming độc lập.

### Bên trong module: DSP stream

Dùng `dsp::stream<complex_t>` và double-buffer.

- Mục tiêu: truyền sample IQ/complex giữa các block.
- Granularity: block mẫu DSP.
- Đồng thời: mỗi DSP block chạy thread riêng.
- Dùng trong demodulator.

### Giữa module: RingBuffer byte stream

Dùng `RingBuffer<uint8_t>`.

- Mục tiêu: truyền byte soft symbols hoặc generic bytes.
- Granularity: byte/chunk file-level.
- Có back-pressure theo `maxLatency`.
- Dùng khi `--live`: demodulator producer, CCSDS decoder consumer.

Luồng live thực tế:

```text
PSKDemodModule
  └─ graph DSP nội bộ nhiều thread
       └─ soft bytes
            └─ RingBuffer<uint8_t>
                 └─ CCSDS decoder thread
                      └─ .cadu
```

`closeWriter()` là tín hiệu EOF “bình thường”: consumer đọc hết dữ liệu còn lại rồi nhận 0 byte; khác với `stopReader()` vốn là huỷ ngay consumer ([buffer.h](/home/nvt/Workspace/station/src/common/dsp/buffer.h:280)).

## 6. Đánh giá thiết kế

Điểm mạnh:

- Phân tầng tốt: thuật toán DSP không biết Meteor; module không cần biết CLI; pipeline không trực tiếp xử lý mẫu.
- `PSKDemodModule` tái sử dụng được cho BPSK/QPSK/OQPSK/8PSK về mặt cấu trúc.
- FEC decoder hỗ trợ cả file và byte stream, nên batch/live dùng chung thuật toán.
- Thiết kế block bằng type template giữ kiểu sample rõ ràng ở cấp DSP.

Giới hạn:

- Module API dùng chung `unordered_map<string, double>`, làm contract tham số yếu kiểu.
- Pipeline chưa tự nối module theo metadata; thêm module mới phải sửa `MeteorPipeline`.
- Block tự quản thread; module phải stop theo đúng thứ tự. Không có scheduler hay cancellation token thống nhất xuyên pipeline.
- `dsp::stream` dùng double-buffer, không phải queue; pipeline DSP dài có thể bị chặn dây chuyền bởi block chậm.
- `DATA_DSP_STREAM` đã được thiết kế nhưng chưa được pipeline dùng để nối module-to-module; hiện chỉ tồn tại bên trong demod module.
- Product module chỉ đọc file CADU, nên chưa thể nối streaming end-to-end đến PNG.

Tóm lại: kiến trúc hiện tại phù hợp với receiver tối giản hiệu năng tốt cho một protocol cố định. Để trở thành framework mở rộng, bước lớn nhất không phải thêm block DSP, mà là biến `PipelineStep` thành execution graph thật sự: module factory, typed port, typed config, ownership RAII và lifecycle/cancellation thống nhất.