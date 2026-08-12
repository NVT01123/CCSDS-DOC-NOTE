`stlsdr` không xuất hiện trong mã; backend tương ứng là `rtlsdr` (`RtlSdrSource`). Luồng live thực tế là:

```text
RTL-SDR / librtlsdr
  → _rx_callback(U8 IQ)
  → source_ptr->output_stream : stream<complex_t>
  → [tuỳ chọn] SmartResampler decimation
  → SplitterBlock
  → output "live"
  → LivePipeline, module demod đầu tiên
  → RingBuffer<byte> giữa các module tiếp theo
  → file / module cuối
```

## 1. Đăng ký và tạo SDR source

Plugin RTL-SDR đăng ký factory với ID `"rtlsdr"` qua sự kiện `RegisterDSPSampleSourcesEvent`; từ đó UI/CLI có thể liệt kê thiết bị và tạo `RtlSdrSource`. Xem [main.cpp](/home/nvt/Workspace/Sat/SatDump/plugins/sdr_sources/rtlsdr_sdr_support/main.cpp:14).

`getAvailableSources()` gọi:

- `rtlsdr_get_device_count()`
- `rtlsdr_get_device_name()`
- `rtlsdr_get_device_usb_strings()`

và tạo `SourceDescriptor` mang serial làm `unique_id`; serial này được giữ trong `d_sdr_id`. Xem [rtlsdr_sdr.cpp](/home/nvt/Workspace/Sat/SatDump/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp:302).

Trong Recorder, `dsp::getSourceFromDescriptor()` dùng factory đã đăng ký để tạo `source_ptr`, rồi gọi `open()`. `open()` chưa mở USB/device; nó chỉ đánh dấu source đã mở và nạp danh sách sample rate hợp lệ (250 kS/s đến 3.2 MS/s). Xem [rtlsdr_sdr.cpp](/home/nvt/Workspace/Sat/SatDump/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp:147).

## 2. Nạp cấu hình SDR

Cấu hình được đưa vào `RtlSdrSource::set_settings()`:

- `gain`: gain tuner hiển thị theo dB.
- `lna_agc`: AGC phía RTL-SDR.
- `tuner_agc`: chọn automatic/manual tuner gain.
- `bias`: bật Bias-Tee.
- `ppm_correction`: hiệu chỉnh tần số.
- Cấu hình cũ `agc` được chuyển thành cả `lna_agc` và `tuner_agc`.

Nếu SDR đã chạy, thay đổi được áp ngay bằng `set_bias()`, `set_gains()`, `set_ppm()`. Các lời gọi đến librtlsdr được retry tối đa 20 lần. Xem [rtlsdr_sdr.cpp](/home/nvt/Workspace/Sat/SatDump/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp:110).

Tần số và sample rate được set trước khi thu:

- `set_samplerate()` chỉ chấp nhận giá trị trong danh sách source hỗ trợ.
- `set_frequency()` lưu tần số trong base class; khi source đã chạy nó gọi `rtlsdr_set_center_freq()`.
- Với tần số >1 GHz, code tune xuống `frequency - 1 GHz` rồi tune lại để ép PLL lock — workaround cho librtlsdr. Xem [rtlsdr_sdr.cpp](/home/nvt/Workspace/Sat/SatDump/plugins/sdr_sources/rtlsdr_sdr.cpp:227).

## 3. `RtlSdrSource::start()`: mở hardware và chuẩn bị callback

Khi người dùng bấm Start, Recorder gọi `source_ptr->start()` trước. Xem [recorder_proc.cpp](/home/nvt/Workspace/Sat/SatDump/src-interface/recorder/recorder_proc.cpp:82).

Bên trong `RtlSdrSource::start()`:

1. Gọi `DSPSampleSource::start()`, tạo `output_stream = std::make_shared<dsp::stream<complex_t>>()`. Đây là stream IQ nguồn. Xem [dsp_sample_source.h](/home/nvt/Workspace/Sat/SatDump/src-core/common/dsp_source_sink/dsp_sample_source.h:40).
2. Đổi `d_sdr_id` (serial) thành index qua `rtlsdr_get_index_by_serial()`.
3. Gọi `rtlsdr_open()` để mở device.
4. Đọc các mức gain hardware bằng `rtlsdr_get_tuner_gains()`, sắp xếp và lưu vào `available_gains`.
5. Gọi `rtlsdr_set_sample_rate()`.
6. Đặt `is_started = true`, sau đó cấu hình:
   - center frequency;
   - Bias-Tee;
   - mode AGC/manual gain và gain gần nhất hardware hỗ trợ;
   - ppm correction.
7. `rtlsdr_reset_buffer()` để xóa phần dữ liệu USB còn tồn.
8. Tạo `work_thread`, chạy `RtlSdrSource::mainThread()`.

Xem [rtlsdr_sdr.cpp](/home/nvt/Workspace/Sat/SatDump/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp:168).

## 4. Đăng ký callback và kích thước block

`mainThread()` tính `buffer_size`:

```cpp
ceil(sample_rate / (60 * 512)) * 512
```

sau đó giới hạn tối đa bằng `dsp::STREAM_BUFFER_SIZE`. Ý nghĩa là xấp xỉ 60 callback/s và size luôn bội của 512. Nó gọi:

```cpp
rtlsdr_read_async(rtlsdr_dev_obj, _rx_callback, &output_stream, 0, buffer_size);
```

Tham số `ctx` là địa chỉ của `shared_ptr<stream<complex_t>> output_stream`, không phải trực tiếp `this`. `0` ở số USB buffers là để librtlsdr dùng mặc định. Xem [rtlsdr_sdr.h](/home/nvt/Workspace/Sat/SatDump/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.h:42).

`rtlsdr_read_async()` là lời gọi blocking: librtlsdr nhận block USB và gọi `_rx_callback()` nhiều lần. Nó chỉ return khi bị hủy/error; vòng `while (thread_should_run)` sẽ chỉ gọi lại nếu vẫn cần chạy.

## 5. Callback: đổi U8 interleaved IQ thành complex float

Callback:

```cpp
void RtlSdrSource::_rx_callback(unsigned char *buf, uint32_t len, void *ctx)
```

Xem [rtlsdr_sdr.cpp](/home/nvt/Workspace/Sat/SatDump/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp:3).

Nó thực hiện:

1. Cast `ctx` về con trỏ đến `shared_ptr<dsp::stream<complex_t>>`, rồi dereference để lấy stream đích.
2. Vì payload RTL-SDR là IQ 8-bit unsigned xen kẽ, một complex sample dùng hai byte:
   - `buf[2*i]` là I/real;
   - `buf[2*i+1]` là Q/imag.
3. Với mỗi sample:
   ```cpp
   I = (byte_I - 127.4f) / 128.0f;
   Q = (byte_Q - 127.4f) / 128.0f;
   ```
   rồi ghi vào `stream->writeBuf[i]`.

Vậy `len` là số byte, còn số IQ sample là `len / 2`. Giá trị raw khoảng `0…255` được đổi gần đúng thành float quanh `[-1, 1]`. Lưu ý code dùng offset `127.4f`, không phải `127.5f`/`128`; đây là lựa chọn hiệu chỉnh DC nhỏ của dự án, không có bước lọc DC riêng ngay trong callback.

4. Sau khi lấp đầy `writeBuf`, callback gọi:

```cpp
stream->swap(len / 2);
```

Đây là điểm dữ liệu được “publish” cho downstream, không phải `memcpy` sang module trực tiếp.

## 6. Cơ chế `stream<complex_t>` và backpressure

`dsp::stream` dùng double-buffer:

- producer/callback ghi vào `writeBuf`;
- consumer đọc từ `readBuf`;
- `swap(size)` hoán đổi hai con trỏ và đặt `dataSize=size`;
- `read()` chờ block mới và trả số sample;
- consumer phải gọi `flush()` sau khi dùng xong để cấp lại buffer cho writer.

Xem [buffer.h](/home/nvt/Workspace/Sat/SatDump/src-core/common/dsp/buffer.h:28).

Điểm quan trọng là chỉ có một block đang “in flight” cho mỗi stream:

- Callback sẽ block trong `swap()` nếu consumer chưa `flush()` block trước.
- Vì callback block, librtlsdr không được cấp buffer mới; đây là cơ chế backpressure tự nhiên.
- Đổi lại, xử lý downstream chậm có thể làm USB overrun/drop sample ở tầng driver/hardware. Callback vì thế chỉ làm chuyển đổi U8→CF32 tối giản, không chứa DSP nặng.

## 7. Từ source stream đến stream của module live

Recorder tạo một `SplitterBlock` có input là output stream của SDR. Khi start, nó gắn input hiện thời — có thể là stream đã decimate — rồi start splitter. Xem [recorder_proc.cpp](/home/nvt/Workspace/Sat/SatDump/src-interface/recorder/recorder_proc.cpp:95).

Nếu `current_decimation > 1`:

```text
SDR output_stream → SmartResamplerBlock → splitter input
```

Nếu không, source stream đi thẳng vào splitter.

Splitter có các nhánh độc lập:

- `main` → FFT/waterfall;
- `"record"` → file sink khi ghi IQ;
- `"live"` → pipeline đang decode;
- VFO outputs → có thể copy IQ hoặc frequency-shift bằng rotator.

Khi bật live pipeline, Recorder:

1. reset output `"live"` thành stream mới;
2. tạo và start `LivePipeline` với `splitter->get_output("live")`;
3. bật `splitter->set_enabled("live", true)`.

Xem [recorder_proc.cpp](/home/nvt/Workspace/Sat/SatDump/src-interface/recorder/recorder_proc.cpp:175).

Mỗi lần splitter nhận IQ từ SDR, nó:

1. `input_stream->read()`;
2. copy IQ từ `input_stream->readBuf` đến `live.output_stream->writeBuf`;
3. gọi `input_stream->flush()` để giải phóng source buffer và unblock callback;
4. gọi `live.output_stream->swap(nsamples)` để đánh thức module live đầu tiên.

Xem [splitter.cpp](/home/nvt/Workspace/Sat/SatDump/src-core/common/dsp/path/splitter.cpp:109).

Do đó callback không “ghi trực tiếp vào module”. Nó ghi vào source stream; splitter là cầu nối copy/publish sang stream mà module đầu tiên đọc.

## 8. LivePipeline gắn stream vào module đầu tiên

`LivePipeline::start()`:

1. Đọc `normal_live` trong định nghĩa pipeline và tạo các module.
2. Gán stream `"live"` vào:
   ```cpp
   modules[0]->input_stream = stream;
   modules[0]->setInputType(DATA_DSP_STREAM);
   ```
3. Gán output:
   - nếu còn module sau: `DATA_STREAM` và tạo `RingBuffer<uint8_t>(1,000,000)`;
   - nếu là module cuối: `DATA_FILE`.
4. Gọi `init()`, đặt `input_active = true`, rồi đẩy `process()` của module sang thread pool.
5. Với module kế tiếp, `input_fifo` trỏ tới `output_fifo` của module trước; mỗi module chạy trong thread riêng.

Xem [live_pipeline.cpp](/home/nvt/Workspace/Sat/SatDump/src-core/pipeline/live_pipeline.cpp:52).

Phân biệt hai loại stream:

```text
RTL-SDR → splitter → demod đầu tiên
  dsp::stream<complex_t>      : double-buffer, IQ float

demod → decoder → ...
  dsp::RingBuffer<uint8_t>    : FIFO byte cho soft symbol/frame/data
```

## 9. Ví dụ module đầu tiên: FSK demod

Với pipeline có `fsk_demod` đầu tiên, module nhận `input_stream` là stream `"live"` và dựng chuỗi DSP:

```text
IQ CF32
→ [optional] DC correction / frequency shift / Doppler / resample
→ AGC
→ Quadrature demod
→ DC correction
→ AGC
→ RRC FIR
→ Mueller–Müller clock recovery
→ soft symbols int8
→ output_fifo hoặc file .soft
```

`BaseDemodModule::initb()` lấy `samplerate`, `buffer_size`, `dc_block`, `freq_shift`, `iq_swap`, Doppler… từ tham số pipeline và tạo các DSP block. Xem [module_demod_base.cpp](/home/nvt/Workspace/Sat/SatDump/src-core/pipeline/modules/demod/module_demod_base.cpp:13).

`FSKDemodModule::process()` start toàn bộ blocks, đọc output của clock recovery, chuyển symbol sang `int8_t`, rồi:

- `output_fifo->write(...)` nếu còn module sau;
- `data_out.write(...)` nếu đây là module cuối.

Xem [module_fsk_demod.cpp](/home/nvt/Workspace/Sat/SatDump/src-core/pipeline/modules/demod/module_fsk_demod.cpp:88).

## 10. Dừng luồng theo chiều ngược

Khi stop:

1. Recorder tắt nhánh `"live"` của splitter.
2. `LivePipeline::stop()` đặt `input_active=false`, stop reader/writer của input stream hoặc FIFO, gọi `module->stop()` và chờ future của từng module hoàn tất.
3. Recorder stop splitter, sau đó stop decimator nếu có.
4. `RtlSdrSource::stop()` gọi `rtlsdr_cancel_async()`, đặt cờ thread false, gọi `output_stream->stopWriter()` để giải phóng callback nếu đang kẹt ở `swap()`, `join()` worker thread, tắt Bias-Tee và đóng device.

Xem [live_pipeline.cpp](/home/nvt/Workspace/Sat/SatDump/src-core/pipeline/live_pipeline.cpp:152) và [rtlsdr_sdr.cpp](/home/nvt/Workspace/Sat/SatDump/plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.cpp:207).