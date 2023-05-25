#pragma once

#include <fstream>
#include <optional>
#include <string_view>
#include <chrono>

#include "source.h"

struct WaveHeader {
	char tag[4];
	int file_size;
	char type[4];
	char format_marker[4];
	int format_marker_len;
	int16_t format_type;
	int16_t channel_count;
	int sample_rate;
	int avg_bytes_per_sec;
	int16_t block_align;
	int16_t bits_per_sample;
	char data_marker[4];
	int data_size;
};

static_assert(sizeof(WaveHeader) == 44);
static_assert(alignof(WaveHeader) == 4);

class WaveFile : public Source {
public:

	static std::optional<WaveFile> read(std::string_view filename) {
		std::ifstream file(filename.data(), std::ios::binary);
		if (!file.is_open()) {
			return std::nullopt;
		}

		WaveHeader header;
		file.read((char*)&header, sizeof(WaveHeader));
		
		// Must always be "RIFF"
		if (strncmp(header.tag, "RIFF", 4) != 0) {
			return std::nullopt;
		}

		// We expect wave to be stored as PCM (0x1)
		if (header.format_type != 1) {
			return std::nullopt;
		}

		return WaveFile(header, std::move(file));
	}

	int channel_count() const override {
		return _channel_count;
	}

	int sample_rate() const override {
		return _sample_rate;
	}

	std::optional<std::chrono::nanoseconds> total_duration() const override {
		return _duration;
	}

	std::optional<double> next_sample() override {
		if (!_file.is_open()) {
			return std::nullopt;
		}

		// No way any wave file could have samples larger than 4 bytes right?
		int8_t buffer[4] = {0};
		if (!_file.read((char*)buffer, _bytes_per_sample)) {
			_file.close();
			return std::nullopt;
		}

		if (_file.gcount() != _bytes_per_sample) {
			_file.close();
			return std::nullopt;
		}

		auto sample = (double)_map_buffer_to_int(buffer) / _max_sample_value;
		return sample;
	}

private:

	WaveFile(
		WaveHeader header,
		std::ifstream file
	)
		: _file(std::move(file))
		, _sample_count(header.data_size / (header.bits_per_sample / 8))
		, _sample_rate(header.sample_rate)
		, _channel_count((int)header.channel_count)
		, _max_sample_value((int)exp2(header.bits_per_sample - 1))
		, _duration((int64_t)((double)_sample_count / (_channel_count * _sample_rate) * 1e9))
		, _bytes_per_sample(header.bits_per_sample / 8)
	{
	}

	int _map_buffer_to_int(int8_t* buffer) {
		switch (_bytes_per_sample) {
			default:
			case 1: return (int)*buffer;
			case 2: return (int)*reinterpret_cast<int16_t*>(buffer);
			case 4: return (int)*reinterpret_cast<int32_t*>(buffer);
		}
	}

	int _sample_count;
	int _sample_rate;
	int _channel_count;
	int _max_sample_value;
	int _bytes_per_sample;
	std::chrono::nanoseconds _duration;
	std::ifstream _file;
};

namespace wave {
	void export_samples_as_wave(
		std::string_view filename,
		int sample_rate,
		int channel_count,
		double* samples,
		int sample_count
	) {
		std::ofstream file(filename.data(), std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return;
		}

		WaveHeader header;
		memcpy(header.tag, "RIFF", 4);
		header.file_size = sizeof(WaveHeader) + sample_count * sizeof(int16_t) - 8;
		memcpy(header.type, "WAVE", 4);
		memcpy(header.format_marker, "fmt ", 4);
		header.format_marker_len = 16;
		header.format_type = 1;	// PCM
		header.channel_count = channel_count;
		header.sample_rate = sample_rate;
		header.avg_bytes_per_sec = (sample_rate * 2 * channel_count);
		header.block_align = 2 * channel_count;
		header.bits_per_sample = 16;
		memcpy(header.data_marker, "data", 4);
		header.data_size = sample_count * sizeof(int16_t);

		file.write((char*)&header, sizeof(WaveHeader));

		std::for_each_n(samples, sample_count, [&](double sample) {
			int16_t pcm = (int16_t)(sample * 0x8fff);
			file.write((char*)&pcm, sizeof(int16_t));
		});
	}
}