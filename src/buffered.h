#pragma once

#include <optional>
#include <chrono>
#include "source.h"

class Buffered : public Source {
public:

	Buffered(std::unique_ptr<Source> input, int buffer_size = 1024)
		: _input(std::move(input))
		, _buffer_size(buffer_size * _input->channel_count())
		, _buffer(_buffer_size)
		, _buffer_idx(0)
	{
		_advance_buffer();
	}

	int channel_count() const override {
		return _input->channel_count();
	}

	int sample_rate() const override {
		return _input->sample_rate();
	}

	std::optional<double> next_sample() override {
		if (_buffer.empty()) {
			return std::nullopt;
		}

		auto sample = _buffer[_buffer_idx];

		_buffer_idx += 1;

		if (_buffer_idx >= _buffer.size()) {
			_advance_buffer();
		}

		return sample;
	}

	std::optional<std::chrono::nanoseconds> total_duration() const override {
		return _input->total_duration();
	}

private:

	void _advance_buffer() {
		_buffer.clear();
		_buffer_idx = 0;

		for (int i = 0; i < _buffer_size; ++i) {
			if (auto sample = _input->next_sample()) {
				_buffer.push_back(*sample);
			} else {
				break;
			}
		}
	}

private:

	std::unique_ptr<Source> _input;
	int _buffer_size;
	int _buffer_idx;
	std::vector<double> _buffer;
};