#include "stream.h"

#include <algorithm>

std::optional<OutputStream> OutputStream::try_default() {
	auto instance = audio::Instance();
	auto device_opt = instance.get_default_output_device();
	if (!device_opt) {
		return {};
	}
	auto device = std::move(device_opt).value();

	auto sample_rates = device.available_sample_rates();

	int desired_sample_rate = 44100;
	auto actual_sample_rate = std::lower_bound(sample_rates.begin(), sample_rates.end(), desired_sample_rate);

	if (actual_sample_rate == sample_rates.end()) {
		return std::nullopt;
	}

	if (!device.open(*actual_sample_rate)) {
		return {};
	}

	return OutputStream(std::move(device));
}

OutputStream::OutputStream(audio::Device device)
	: _device(std::move(device))
{
	
}