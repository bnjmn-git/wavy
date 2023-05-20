#include "audio.h"

#ifdef _WIN32
	#include "audio_wasapi.h"
#endif

namespace audio {
	Device::Device(std::unique_ptr<HostDevice> host)
		: _host(std::move(host))
	{}

	std::string_view Device::name() const {
		return _host->get_name();
	}

	std::string_view Device::id() const {
		return _host->get_id();
	}

	std::vector<int> const& Device::available_sample_rates() const {
		return _host->get_available_sample_rates();
	}

	uint32_t Device::buffer_size() const {
		return _host->get_buffer_size();
	}

	int Device::channel_count() const {
		return _host->get_channel_count();
	}

	bool Device::open(int desired_sample_rate) {
		return _host->open(desired_sample_rate);
	}

	void Device::close() {
		_host->close();
	}

	void Device::start(AudioCallback callback) {
		_host->start(std::move(callback));
	}

	void Device::stop() {
		_host->stop();
	}


	Instance::Instance()
		: _instance(new WasapiInstance())
	{
	}

	std::optional<Device> Instance::get_default_output_device() const {
		auto host = _instance->get_default_output_device();
		if (!host) {
			return {};
		}

		return Device(std::move(host));
	}
}