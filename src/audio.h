#pragma once

#include <memory>
#include <string_view>
#include <array>
#include <vector>
#include <functional>
#include <optional>

// There's no plan to make this cross-platform so these interfaces and abstraction
// are unnecesary. However, I don't want to pollute the global namespace with crappy,
// bloated win32 headers so yeah.

namespace audio {
	class Instance;
	class Device;
	class Stream;

	using AudioCallback = std::function<void(float*, int, int)>;

	constexpr std::array<int, 13> get_standard_sample_rates() {
		return { 8000, 9600, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 192000 };
	}
	
	class HostDevice {
	public:

		virtual ~HostDevice() {}

		virtual std::string_view get_name() const = 0;
		virtual std::string_view get_id() const = 0;
		virtual uint32_t get_buffer_size() const = 0;
		virtual std::vector<int> const& get_available_sample_rates() const = 0;
		virtual int get_sample_rate() const = 0;
		virtual int get_channel_count() const = 0;
		virtual bool open(int desired_sample_rate) = 0;
		virtual void close() = 0;
		virtual void start(AudioCallback callback) = 0;
		virtual void stop() = 0;
	};
	
	class HostInstance {
	public:

		virtual ~HostInstance() {}	
		
		virtual std::unique_ptr<HostDevice> get_default_output_device() const = 0;
	};

	class AudioDeviceCallback {
	public:
		virtual ~AudioDeviceCallback() = default;
		virtual void on_started(Device const& device) {}
		virtual void on_stopped(Device const& device) {}
		virtual void on_process(float* buffer, int channel_count, int sample_count) = 0;
	};

	class Instance {
	public:

		Instance();

		std::optional<Device> get_default_output_device() const;

	private:

		std::unique_ptr<HostInstance> _instance;
	};

	class Device {
	public:

		Device(std::unique_ptr<HostDevice> host);

		std::string_view name() const;
		std::string_view id() const;
		std::vector<int> const& available_sample_rates() const;
		int sample_rate() const;
		uint32_t buffer_size() const;
		int channel_count() const;
		bool open(int desired_sample_rate);
		void close();
		void start(AudioCallback callback);
		void stop();

	private:

		std::unique_ptr<HostDevice> _host;
	};

	class Stream {

	};
}