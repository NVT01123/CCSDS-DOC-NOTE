`BaseDemodModule` là lớp nền dùng chung cho các bộ giải điều chế. Nó xử lý chuỗi đầu vào theo thứ tự:

`IQ file/stream → DC block → frequency shift → Doppler correction → FFT/ghi intermediate → resample → AGC → demodulator cụ thể`

Các tham số của nó gồm:

| Tham số | Kiểu / đơn vị | Vai trò |
|---|---|---|
| `baseband_format` | chuỗi, ví dụ `cf32`, `cs16`, `cs8`, `cu8` | Định dạng mẫu IQ trong file đầu vào. Chỉ dùng khi đầu vào là file; phải khớp với định dạng lúc ghi. |
| `samplerate` | Hz / samples per second | Tốc độ lấy mẫu IQ đầu vào. Bắt buộc; dùng để tính số mẫu trên mỗi symbol, dịch tần, Doppler và resampling. |
| `buffer_size` | số mẫu | Kích thước khối mẫu xử lý mỗi lần. Tác động chủ yếu đến độ trễ và hiệu năng, không đổi nội dung tín hiệu. Nếu không truyền, mã tự chọn giá trị theo samplerate (tối thiểu khoảng 8193 mẫu). |
| `symbolrate` | baud | Số symbol/giây của tín hiệu. Dùng để tính `SPS = samplerate / symbolrate` và quyết định có cần resample hay không. Giá trị này thường do module demod cụ thể hoặc preset xác định. |
| `agc_rate` | số thực | Tốc độ đáp ứng của AGC. Cao hơn: AGC bám biên độ nhanh hơn nhưng có thể làm tín hiệu dao động/biến dạng hơn; thấp hơn: ổn định hơn nhưng bắt biên độ chậm. Giá trị mặc định nội bộ là `0.01`. |
| `dc_block` | boolean | Bật khử DC offset/đỉnh DC ở giữa phổ IQ. Hữu ích với SDR có DC spike, như HackRF. |
| `freq_shift` | Hz, có dấu | Dịch phổ IQ bằng lượng tần số chỉ định để đưa tín hiệu về vị trí mong muốn trước khi demod. `0` nghĩa là không dịch. |
| `iq_swap` | boolean | Đảo hai nhánh I và Q khi file/thiết bị ghi chúng theo thứ tự ngược. Sai thiết lập này thường gây đảo phổ và làm giải điều chế thất bại. |
| `min_sps` | samples/symbol | Ngưỡng SPS thấp nhất mà demodulator chấp nhận trước khi BaseDemodModule resample tín hiệu. Mặc định `1.1`. |
| `max_sps` | samples/symbol | Ngưỡng SPS cao nhất trước khi resample. Mặc định `4.0`; một số pipeline giảm xuống `3` để phù hợp demodulator. |
| `custom_samplerate` | Hz | Ép samplerate sau resample. Nếu có tham số này, nó ưu tiên hơn việc tự tính từ `min_sps`/`max_sps`. |
| `enable_doppler` | boolean | Bật hiệu chỉnh Doppler theo quỹ đạo vệ tinh. Cần các tham số quỹ đạo/vị trí bên dưới. |
| `doppler_alpha` | số thực | Hệ số làm mượt tốc độ bám hiệu chỉnh Doppler. Cao hơn bám tần số Doppler nhanh hơn; thấp hơn mượt hơn. Mặc định `0.01`. |
| `satellite_frequency` | Hz | Tần số downlink thực tế của vệ tinh; cần cho Doppler để tính độ lệch tần số. Nếu có `freq_shift`, mã cộng giá trị shift vào tần số này. |
| `satellite_norad` | số nguyên | NORAD catalog ID của vệ tinh, dùng để lấy phần tử quỹ đạo/TLE cho Doppler. |
| `qth_lon` | độ | Kinh độ trạm thu. Có thể ghi đè cấu hình QTH chung. |
| `qth_lat` | độ | Vĩ độ trạm thu. |
| `qth_alt` | m | Độ cao trạm thu. |
| `start_timestamp` | Unix timestamp, giây UTC | Thời điểm bắt đầu bản ghi. Bắt buộc khi bật Doppler trên file, vì hiệu chỉnh phải biết vị trí vệ tinh ở thời điểm mỗi mẫu được thu. Với luồng live, thời gian hiện tại được dùng. |
| `dump_intermediate` | chuỗi định dạng mẫu | Lưu IQ trung gian sau DC/frequency/Doppler correction nhưng trước resample và AGC, để kiểm tra/debug. Chuỗi này được truyền vào `set_output_sample_type`, nên phải là định dạng đầu ra hợp lệ. |

Hai lưu ý từ mã hiện tại:

- `samplerate` là bắt buộc; nếu `samplerate / symbolrate < 1`, module báo lỗi vì không đủ ít nhất một mẫu cho mỗi symbol.
- `getParams()` đặt `dump_intermediate` mặc định là `false`, nhưng constructor lại đọc nó như `std::string`. Về mặt kiểu dữ liệu đây không nhất quán; thực tế nên bỏ tham số này khi không dùng, hoặc đặt `""`, thay vì truyền boolean `false`.

Các điểm xử lý chính nằm ở [module_demod_base.cpp](/home/nvt/Workspace/SatDump/src-core/pipeline/modules/demod/module_demod_base.cpp:17) và danh sách tham số mặc định tại [module_demod_base.h](/home/nvt/Workspace/SatDump/src-core/pipeline/modules/demod/module_demod_base.h:135).