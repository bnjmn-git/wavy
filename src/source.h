#pragma once

#include <stdint.h>
#include <optional>
#include <functional>

class Amplify;

static constexpr uint64_t NANO_PER_SEC = 1000000000;

class Source {
public:

	virtual ~Source() = default;

	virtual int sample_rate() const = 0;
	virtual int channel_count() const = 0;

	/*
	* @brief asdflkj
	* @return The sample value, none otherwise.
	*/
	virtual std::optional<double> next_sample() = 0;

	virtual std::optional<std::chrono::nanoseconds> total_duration() const {
		return std::nullopt;
	}
};

class Amplify : public Source {
public:

	Amplify(std::unique_ptr<Source> input, double amp)
		: _input(std::move(input))
		, _amp(amp)
	{}

	int sample_rate() const override {
		return _input->sample_rate();
	}

	int channel_count() const override {
		return _input->channel_count();
	}

	std::optional<double> next_sample() override {
		if (auto sample = _input->next_sample()) {
			return *sample * _amp;
		}
		return {};
	}

	std::optional<std::chrono::nanoseconds> total_duration() const override {
		return _input->total_duration();
	}

private:

	std::unique_ptr<Source> _input;
	double _amp;
};

class Duration : public Source {
public:

	Duration(std::unique_ptr<Source> input, uint64_t duration_ns)
		: _input(std::move(input))
		, _requested_duration_ns(duration_ns)
		, _remaining_duration_ns(duration_ns)
		, _duration_ns_per_sample(NANO_PER_SEC / (_input->sample_rate() * _input->channel_count()))
	{
	}

	int channel_count() const override {
		return _input->channel_count();
	}

	int sample_rate() const override {
		return _input->sample_rate();
	}

	std::optional<double> next_sample() override {
		if (_remaining_duration_ns <= _duration_ns_per_sample) {
			return std::nullopt;
		}
	
		auto sample = _input->next_sample();
		_remaining_duration_ns -= _duration_ns_per_sample;
		return sample;
	}

	std::optional<std::chrono::nanoseconds> total_duration() const override {
		return std::chrono::nanoseconds(_requested_duration_ns);
	}

private:

	std::unique_ptr<Source> _input;
	uint64_t _remaining_duration_ns;
	uint64_t _requested_duration_ns;
	uint64_t _duration_ns_per_sample;
};

class Delay : public Source {
public:

	Delay(std::unique_ptr<Source> input, uint64_t delay_ns)
		: _input(std::move(input))
		, _requested_delay_ns(delay_ns)
		, _remaining_delay_ns(delay_ns)
		, _duration_ns_per_sample(NANO_PER_SEC / (_input->sample_rate() * _input->channel_count()))
	{
	}

	int channel_count() const override {
		return _input->channel_count();
	}

	int sample_rate() const override {
		return _input->sample_rate();
	}

	std::optional<double> next_sample() override {
		if (_remaining_delay_ns <= _duration_ns_per_sample) {
			return _input->next_sample();
		}

		_remaining_delay_ns -= _duration_ns_per_sample;
		return 0.0;
	}

	std::optional<std::chrono::nanoseconds> total_duration() const override {
		if (auto total = _input->total_duration()) {
			return std::chrono::nanoseconds(_requested_delay_ns + total->count());
		}

		return std::nullopt;
	}

private:

	std::unique_ptr<Source> _input;
	uint64_t _remaining_delay_ns;
	uint64_t _requested_delay_ns;
	uint64_t _duration_ns_per_sample;
};

struct FilterInfo {
	int current_sample;
	int sample_rate;
	std::optional<std::chrono::nanoseconds> total_duration;

	std::optional<int> get_total_samples() {
		if (total_duration) {
			return (int)(total_duration->count() * sample_rate / NANO_PER_SEC);
		}

		return std::nullopt;
	}
};

class Filter : public Source {
public:

	Filter(std::unique_ptr<Source> input, std::function<double(double, FilterInfo)> callback)
		: _input(std::move(input))
		, _current_sample(0)
		, _callback(callback)
	{}

	int sample_rate() const override {
		return _input->sample_rate();
	}

	int channel_count() const override {
		return _input->channel_count();
	}

	std::optional<double> next_sample() override {
		FilterInfo info;
		info.current_sample = _current_sample;
		info.sample_rate = _input->sample_rate();
		info.total_duration = _input->total_duration();

		_current_sample += 1;

		if (auto sample = _input->next_sample()) {
			return _callback(*sample, info);
		}

		return std::nullopt;
	}

private:

	std::unique_ptr<Source> _input;
	std::function<double(double, FilterInfo)> _callback;
	int _current_sample;
};

