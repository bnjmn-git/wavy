#pragma once

#include "audio.h"

#include <vector>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <wrl/client.h>
#include <functiondiscoverykeys.h>
#include <string>
#include <algorithm>
#include <thread>
#include <atomic>

#include <assert.h>

template<class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

[[noreturn]] static inline void handle_error_fatal(HRESULT hr) {
	fprintf(stderr, "Failed with code: %i", hr);
#ifdef _DEBUG
	__debugbreak();
#endif
	abort();
}

#define REFTIMES_PER_SEC 100000000
#define REFTIMES_PER_MILLI 10000

namespace audio {
	static inline std::string wide_to_char(wchar_t const* wstr) {
		std::string str;
		if (auto size = WideCharToMultiByte(
			CP_UTF8,
			WC_COMPOSITECHECK,
			wstr,
			-1,
			nullptr,
			0,
			NULL,
			NULL
		)) {
			str.resize(size - 1);
			size = WideCharToMultiByte(
				CP_UTF8,
				WC_COMPOSITECHECK,
				wstr,
				-1,
				str.data(),
				size,
				NULL,
				NULL
			);
			assert(size > 0);
		} else {
			fprintf(stderr, "Unable to convert wide string, defaulting to empty");
		}

		return str;
	}

	static inline std::string get_device_name(IMMDevice* device) {
		HRESULT hr;
		
		ComPtr<IPropertyStore> props;
		if (hr = device->OpenPropertyStore(STGM_READ, props.GetAddressOf()), FAILED(hr)) {
			handle_error_fatal(hr);
		}

		PROPVARIANT prop_name;
		PropVariantInit(&prop_name);
		if (hr = props->GetValue(PKEY_Device_FriendlyName, &prop_name), FAILED(hr)) {
			handle_error_fatal(hr);
		}

		std::string name;
		if (prop_name.vt != VT_EMPTY) {
			name = wide_to_char(prop_name.pwszVal);
		}

		PropVariantClear(&prop_name);

		return name;
	}

	static inline std::string get_device_id(IMMDevice* device) {
		LPWSTR device_id;
		HRESULT hr;
		if (hr = device->GetId(&device_id), FAILED(hr)) {
			handle_error_fatal(hr);
		}

		return wide_to_char(device_id);
	}

	static inline WAVEFORMATEX* memalloc_and_get_wave_format(IAudioClient* client) {
		WAVEFORMATEX* format;
		HRESULT hr = client->GetMixFormat(&format);
		if (FAILED(hr)) {
			handle_error_fatal(hr);
		}

		return format;
	}

	static inline WAVEFORMATEXTENSIBLE extract_wave_format_ext(WAVEFORMATEX const& format) {
		WAVEFORMATEXTENSIBLE ex;
		memcpy(&ex, &format, format.wFormatTag == WAVE_FORMAT_EXTENSIBLE ? sizeof(WAVEFORMATEXTENSIBLE) : sizeof(WAVEFORMATEX));
		return ex;
	}

	static inline REFERENCE_TIME map_samples_to_ref_time(int nsamples, int sample_rate) {
		return (REFERENCE_TIME)((double)(nsamples) / (double)sample_rate * REFTIMES_PER_SEC);
	}

	static std::vector<int> find_available_sample_rates(WAVEFORMATEXTENSIBLE format, IAudioClient* client) {
		std::vector<int> sample_rates;
		auto standard_sample_rates = get_standard_sample_rates();

		auto contains = [&](int rate) {
			return std::any_of(sample_rates.begin(), sample_rates.end(), [rate](int r) {
				return r == rate;
			});
		};

		auto& fmt = format.Format;
		for (int rate : standard_sample_rates) {
			if (!contains(rate)) {
				WAVEFORMATEX* closest_match = nullptr;
				fmt.nSamplesPerSec = (DWORD)rate;
				fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nChannels * (fmt.wBitsPerSample / 8);
				
				if (SUCCEEDED(client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &fmt, &closest_match))) {
					if (closest_match) {
						rate = closest_match->nSamplesPerSec;
						CoTaskMemFree(closest_match);
					}

					if (!contains(rate)) {
						sample_rates.push_back(rate);
					}
				}
			}
		}

		std::sort(sample_rates.begin(), sample_rates.end());

		return sample_rates;
	}

	class WasapiOutputDevice : public HostDevice {
	public:

		WasapiOutputDevice(ComPtr<IMMDevice> mm_device)
			: _device_name(get_device_name(mm_device.Get()))
			, _device_id(get_device_id(mm_device.Get()))
			, _device(mm_device)
		{
			ComPtr<IAudioClient> client;
			if (auto hr = mm_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.GetAddressOf())) {
				handle_error_fatal(hr);
			}

			auto wave_format = memalloc_and_get_wave_format(client.Get());
			auto wave_format_ex = extract_wave_format_ext(*wave_format);
			_sample_rates = find_available_sample_rates(wave_format_ex, client.Get());

			CoTaskMemFree(wave_format);

			// _channels = wave_format->nChannels;
			_channels = 2;
			// _channel_mask = wave_format_ex.dwChannelMask;
			_channel_mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
			assert(wave_format_ex.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
			assert(wave_format_ex.dwChannelMask & SPEAKER_FRONT_LEFT);
			assert(wave_format_ex.dwChannelMask & SPEAKER_FRONT_RIGHT);

			_client_event = CreateEvent(nullptr, false, false, nullptr);
		}

		~WasapiOutputDevice() {
			if (_is_open.load(std::memory_order_acquire)) {
				close();
			}

			CloseHandle(_client_event);
		}

		std::string_view get_name() const override {
			return _device_name;
		}

		std::string_view get_id() const override {
			return _device_id;
		}

		std::vector<int> const& get_available_sample_rates() const override {
			return _sample_rates;
		}

		int get_sample_rate() const override {
			return _sample_rate;
		}

		uint32_t get_buffer_size() const override {
			return _buffer_size;
		}

		int get_channel_count() const override {
			return _channels;
		}

		bool open(int desired_sample_rate) override {
			if (!std::any_of(_sample_rates.begin(), _sample_rates.end(), [&](int rate) {
				return rate == desired_sample_rate;
			})) {
				return false;
			}

			HRESULT hr;
			if (hr = _device->Activate(_uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)_client.GetAddressOf()), FAILED(hr)) {
				handle_error_fatal(hr);
			}

			WAVEFORMATEXTENSIBLE format;
			format.Format.cbSize = 22;	// MSDN says 22 when wFormatTag == WAVE_FORMAT_EXTENSIBLE;
			format.Format.nChannels = (WORD)_channels;
			format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
			format.Format.nSamplesPerSec = (DWORD)desired_sample_rate;
			format.Format.wBitsPerSample = sizeof(float) * 8;
			format.Format.nBlockAlign = (WORD)format.Format.nChannels * (format.Format.wBitsPerSample / 8);
			format.Format.nAvgBytesPerSec = (DWORD)format.Format.nBlockAlign * format.Format.nSamplesPerSec;
			format.dwChannelMask = _channel_mask;
			format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
			format.Samples.wValidBitsPerSample = format.Format.wBitsPerSample;

			REFERENCE_TIME default_period, min_period;
			_client->GetDevicePeriod(&default_period, &min_period);

			if (SUCCEEDED(_client->Initialize(
				AUDCLNT_SHAREMODE_SHARED,
				AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
				default_period,
				0,
				&format.Format,
				nullptr
			))) {
				_is_open = true;
				_sample_rate = format.Format.nSamplesPerSec;

				_client->GetBufferSize(&_buffer_size);

				_client->GetService(_uuidof(IAudioRenderClient), (void**)_render_client.GetAddressOf());
				_client->SetEventHandle(_client_event);

				_client_thread = std::thread(_task_client_thread, this);
			}

			return true;
		}

		void close() override {
			stop();
			_is_open.store(false, std::memory_order_release);
			SetEvent(_client_event);

			_client_thread.join();

			_render_client = nullptr;
			_client = nullptr;
		}

		void start(AudioCallback callback) override {
			assert(_is_open.load(std::memory_order_acquire));

			if (_is_running.load(std::memory_order_acquire)) {
				return;
			}

			_callback = std::move(callback);

			_client->Start();

			_is_running.store(true, std::memory_order_release);
		}

		void stop() override {
			assert(_is_open.load(std::memory_order_acquire));

			if (!_is_running.load(std::memory_order_acquire)) {
				return;
			}

			_is_running.store(false, std::memory_order_release);

			_client->Stop();
			_client->Reset();
		}

	private:

		void _process() {
			uint32_t frames_count;
			uint32_t padding_frames_count;
			uint8_t* data;

			_client->GetCurrentPadding(&padding_frames_count);
			
			assert(_buffer_size >= padding_frames_count);
			frames_count = _buffer_size - padding_frames_count;

			_render_client->GetBuffer(frames_count, &data);

			if (_callback) {
				float* dataf = (float*)data;
				_callback(dataf, _channels, frames_count);
			} else {
				memset(data, 0, _bytes_per_sample * _channels * frames_count);
			}

			_render_client->ReleaseBuffer(frames_count, 0);
		}

		bool try_process() {
			if (WaitForSingleObject(_client_event, 5000) == WAIT_OBJECT_0) {
				if (_is_running.load(std::memory_order_acquire)) {
					_process();
				}

				return true;
			}

			return false;
		}

		static void _task_client_thread(WasapiOutputDevice* device) {
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

			while (device->_is_open.load(std::memory_order_acquire)) {
				device->try_process();
			}
		}

	private:

		std::string _device_name;
		std::string _device_id;
		std::vector<int> _sample_rates;
		int _channels;
		int _bytes_per_sample;
		uint32_t _buffer_size;
		int _sample_rate;
		DWORD _channel_mask;
		HANDLE _client_event;
		
		std::atomic<bool> _is_open;
		std::atomic<bool> _is_running;

		ComPtr<IMMDevice> _device;
		ComPtr<IAudioClient> _client;

		ComPtr<IAudioRenderClient> _render_client;

		std::thread _client_thread;

		AudioCallback _callback;
	};

	class WasapiInstance : public HostInstance {
	public:

		WasapiInstance()
			: _mm_enumerator()
		{
			CoInitialize(NULL);

			if (auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), &_mm_enumerator)) {
				handle_error_fatal(hr);
			}
		}

		std::unique_ptr<HostDevice> get_default_output_device() const {
			ComPtr<IMMDevice> mm_device;
			if (_mm_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, mm_device.GetAddressOf())) {
				return nullptr;
			}

			return std::make_unique<WasapiOutputDevice>(mm_device);
		}

		~WasapiInstance() {
			CoUninitialize();
		}

	private:

		ComPtr<IMMDeviceEnumerator> _mm_enumerator;
	};
}
