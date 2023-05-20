#pragma once

#include <optional>
#include <vector>
#include <algorithm>
#include <assert.h>
#include <memory>

#include "source.h"

class ChannelConverter : public Source {
public:

	ChannelConverter(std::unique_ptr<Source> input, int to_channels)
		: _input(std::move(input))
		, _from(_input->channel_count())
		, _to(to_channels)
		, _repeat_sample(std::nullopt)
		, _next_output_sample_pos(0)
	{
	}

	int channel_count() const override {
		return _to;
	}

	int sample_rate() const override {
		return _input->sample_rate();
	}

	std::optional<double> next_sample() override {
		// Essentially a no-op if there's no conversion.
		if (_from == _to) {
			return _input->next_sample();
		}

		std::optional<double> result;
		
		// When converting from less channels to more, the last channel of the
		// input is copied to the extra channels of the output.
		
		// Copy the last input channel here
		if (_next_output_sample_pos == _from - 1) {
			auto sample = _input->next_sample();
			_repeat_sample = sample;
			result = sample;
		}
		else if (_next_output_sample_pos < _from) {
			result = _input->next_sample();
		} else {
			// We are now outputting to a channel that doesn't exist in the input,
			// so return the repeat sample.
			result = _repeat_sample;
		}

		++_next_output_sample_pos;

		if (_next_output_sample_pos == _to) {
			// Wrap around back to 0
			_next_output_sample_pos = 0;

			// Discard any remaining samples if we are converting to a lower channel count.
			if (_from > _to) {
				for (int i = _to; i < _from; ++i) {
					_input->next_sample();
				}
			}
		}

		return result;
	}

	std::optional<std::chrono::nanoseconds> total_duration() const override {
		return _input->total_duration();
	}

private:

	std::unique_ptr<Source> _input;
	int _from;
	int _to;
	std::optional<double> _repeat_sample;
	int _next_output_sample_pos;
};

template<class T>
T gcd(T a, T b) {
	return b == 0 ? a : gcd(b, a % b);
}

class SampleRateConverter : public Source {
public:

	SampleRateConverter(std::unique_ptr<Source> input, int to_sample_rate)
		: _input(std::move(input))
		, _channels(_input->channel_count())
		, _current_frame_idx(0)
		, _next_frame_idx(0)
	{
		auto from = _input->sample_rate();
		auto to = to_sample_rate;
		auto g = gcd(from, to);

		std::vector<double> first_samples;
		std::vector<double> next_samples;

		if (from != to) {
			first_samples.reserve(_channels);
			next_samples.reserve(_channels);

			for (int i = 0; i < _channels; ++i) {
				if (auto sample = _input->next_sample()) {
					first_samples.push_back(*sample);
				}
			}

			for (int i = 0; i < _channels; ++i) {
				if (auto sample = _input->next_sample()) {
					next_samples.push_back(*sample);
				}
			}
		}

		_current_frame = std::move(first_samples);
		_next_frame = std::move(next_samples);
		_output_buffer.reserve(_channels);
		_from = from / g;
		_to = to / g;
	}

	int channel_count() const override {
		return _input->channel_count();
	}

	int sample_rate() const override {
		return _to;
	}

	std::optional<double> next_sample() override {
		if (_from == _to) {
			return _input->next_sample();
		}

		if (!_output_buffer.empty()) {
			auto sample = _output_buffer[0];
			_output_buffer.erase(_output_buffer.begin());
			return sample;
		}

		if (_next_frame_idx == _to) {
			_next_frame_idx = 0;

			do {
				_next_input_frame();
			} while (_current_frame_idx != _from);

			_current_frame_idx = 0;
		} else {
			auto left_sample_idx = (_from * _next_frame_idx / _to) % _from;
			while (_current_frame_idx != left_sample_idx) {
				_next_input_frame();
			}
		}

		std::optional<double> result;
		auto lerp_factor = (double)((_from * _next_frame_idx) % _to) / _to;
		for (int i = 0; i < std::min(_current_frame.size(), _next_frame.size()); ++i) {
			auto cur_sample = _current_frame[i];
			auto next_sample = _next_frame[i];
			auto sample = cur_sample * (1.0f - lerp_factor) + next_sample * lerp_factor;

			if (i == 0) {
				result = sample;
			} else {
				_output_buffer.push_back(sample);
			}
		}

		_next_frame_idx += 1;

		if (result) {
			return result;
		} else {
			if (!_current_frame.empty()) {
				auto r = _current_frame[0];
				_current_frame.erase(_current_frame.begin());
				std::swap(_output_buffer, _current_frame);
				_current_frame.clear();
				return r;
			}
		}

		return {};
	}

	std::optional<std::chrono::nanoseconds> total_duration() const override {
		return _input->total_duration();
	}

private:

	void _next_input_frame() {
		_current_frame_idx += 1;
		std::swap(_current_frame, _next_frame);
		_next_frame.clear();

		for (int i = 0; i < _channels; ++i) {
			if (auto sample = _input->next_sample()) {
				_next_frame.push_back(*sample);
			} else {
				break;
			}
		}
	}

private:

	std::unique_ptr<Source> _input;
	int _from;
	int _to;
	int _channels;
	std::vector<double> _current_frame;
	std::vector<double> _next_frame;
	std::vector<double> _output_buffer;

	int _current_frame_idx;
	int _next_frame_idx;
};

class Converter : public Source {
public:

	Converter(std::unique_ptr<Source> input, int channels, int sample_rate)
		: _input(std::make_unique<SampleRateConverter>(std::move(input), sample_rate), channels)
	{}

	int channel_count() const override {
		return _input.channel_count();
	}

	int sample_rate() const override {
		return _input.sample_rate();
	}

	std::optional<double> next_sample() override {
		return _input.next_sample();
	}

	std::optional<std::chrono::nanoseconds> total_duration() const override {
		return _input.total_duration();
	}

private:

	ChannelConverter _input;
};