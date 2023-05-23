#pragma once

#include "source.h"

#define _USE_MATH_DEFINES
#include <math.h>

namespace detail {
	inline double sine_wave(double phase) {
		return sin(phase);
	}

	inline double saw_wave(double phase) {
		return M_2_PI * atan(tan(phase * 0.5));
	}

	inline double square_wave(double phase) {
		return sin(phase) < 0.0 ? -1.0 : 1.0;
	}

	inline double triangle_wave(double phase) {
		return asin(sin(phase)) * M_2_PI;
	}

	constexpr int NUM_WAVE_SAMPLES = 128;

	template<int N>
	class WaveTable {
	public:

		constexpr WaveTable(double(*fn)(int, int)) {
			for (int i = 0; i < N; ++i) {
				_table[i] = fn(i, N);
			}
		}

		constexpr double evaluate(double phase) const {
			phase /= 2.0 * M_PI;
			phase *= N;
			auto lerp = phase - floor(phase);
			int left_sample = (int)floor(phase) % N;
			int right_sample = (int)ceil(phase) % N;

			return _table[left_sample] * (1.0 - lerp) + _table[right_sample] * lerp;
		}

	private:

		double _table[N];
	};

	WaveTable<NUM_WAVE_SAMPLES> const SINE_TABLE([](int current_sample, int num_samples) {
		auto phase = (double)current_sample / num_samples;
		phase *= 2.0 * M_PI;
		return sine_wave(phase);
	});

	WaveTable<NUM_WAVE_SAMPLES> const SAW_TABLE([](int current_sample, int num_samples) {
		auto phase = (double)current_sample / num_samples;
		phase *= 2.0 * M_PI;
		return saw_wave(phase);
	});
	
	WaveTable<NUM_WAVE_SAMPLES> const TRIANGLE_WAVE([](int current_sample, int num_samples) {
		auto phase = (double)current_sample / num_samples;
		phase *= 2.0 * M_PI;
		return triangle_wave(phase);
	});

	WaveTable<NUM_WAVE_SAMPLES> const SQUARE_WAVE([](int current_sample, int num_samples) {
		auto phase = (double)current_sample / num_samples;
		phase *= 2.0 * M_PI;
		return square_wave(phase);
	});
}

double mod(double n, double d) {
	n = fmod(n, d);
	if (n < 0.0) {
		n += d;
	}

	return n;
}

double saturate(double input) {
	return tanh(input);
}

class SineWave : public Source {
public:

	SineWave(double freq)
		: _freq(freq)
		, _phase(0.0)
	{}

	int sample_rate() const {
		return 48000;
	}

	int channel_count() const {
		return 1;
	}

	std::optional<double> next_sample() {
		auto value = detail::SINE_TABLE.evaluate(_phase);
		auto dt = 1.0 / sample_rate();
		_phase += 2.0*M_PI * _freq * dt;
		_phase = fmod(_phase, 2.0*M_PI);

		return value;
	}

private:

	double _freq;
	double _phase;
};

class SawWave : public Source {
public:

	SawWave(double freq)
		: _freq(freq)
		, _phase(0.0)
	{}

	int sample_rate() const {
		return 48000;
	}

	int channel_count() const {
		return 1;
	}

	std::optional<double> next_sample() {
		auto value = detail::SAW_TABLE.evaluate(_phase);
		auto dt = 1.0 / sample_rate();
		_phase += 2.0*M_PI * _freq * dt;
		_phase = fmod(_phase, 2.0*M_PI);

		return value;
	}

private:

	double _freq;
	double _phase;
};

class TriangleWave : public Source {
public:

	TriangleWave(double freq)
		: _freq(freq)
		, _phase(0.0)
	{}

	int sample_rate() const {
		return 48000;
	}

	int channel_count() const {
		return 1;
	}

	std::optional<double> next_sample() {
		auto value = detail::TRIANGLE_WAVE.evaluate(_phase);
		auto dt = 1.0 / sample_rate();
		_phase += 2.0*M_PI * _freq * dt;
		_phase = fmod(_phase, 2.0*M_PI);

		return value;
	}

private:

	double _freq;
	double _phase;
};

class SquareWave : public Source {
public:

	SquareWave(double freq)
		: _freq(freq)
		, _phase(0.0)
	{}

	int sample_rate() const {
		return 48000;
	}

	int channel_count() const {
		return 1;
	}

	std::optional<double> next_sample() {
		auto value = detail::SQUARE_WAVE.evaluate(_phase);
		auto dt = 1.0 / sample_rate();
		_phase += 2.0*M_PI * _freq * dt;
		_phase = fmod(_phase, 2.0*M_PI);

		return value;
	}

private:

	double _freq;
	double _phase;
};

class PianoWave : public Source {
public:

	PianoWave(double freq)
		: _freqs{ freq, 0.0 }
		, _phases{ 0.0 }
		, _amps{
			1.0,
			0.15,
			0.17,
			0.155,
			0.075,
			0.0675,
			0.01,
			0.067,
			0.05
		}
	{
		for (int i = 1; i < 9; ++i) {
			_freqs[i] = freq * (i + 1);
		}
	}

	int sample_rate() const {
		return 48000;
	}

	int channel_count() const {
		return 1;
	}

	std::optional<double> next_sample() {
		auto sum = 0.0;
		auto dt = 1.0 / sample_rate();
		for (int i = 0; i < 9; ++i) {
			sum += detail::SINE_TABLE.evaluate(_phases[i]) * _amps[i];
			_phases[i] += 2.0*M_PI * _freqs[i] * dt;
			_phases[i] = fmod(_phases[i], 2.0*M_PI);
		}

		return sum;
	}

private:

	double _freqs[9];
	double _amps[9];
	double _phases[9];
};

class ViolinWave : public Source {
public:

	ViolinWave(double freq)
		: _freqs{ freq, 0.0 }
		, _phases{ 0.0 }
		, _amps{
			0.447,
			1.0,
			0.794,
			0.282,
			0.316,
			0.224,
			0.2,
			0.2,
			0.251,
			0.0794,
			0.178
		}
	{
		for (int i = 1; i < 9; ++i) {
			_freqs[i] = _freqs[0] * (i + 1);
		}
	}

	int sample_rate() const {
		return 48000;
	}

	int channel_count() const {
		return 1;
	}

	std::optional<double> next_sample() {
		auto sum = 0.0;
		auto dt = 1.0 / sample_rate();
		for (int i = 0; i < 9; ++i) {
			sum += detail::SINE_TABLE.evaluate(_phases[i]) * _amps[i];
			_phases[i] += 2.0*M_PI * _freqs[i] * dt;
			_phases[i] = fmod(_phases[i], 2.0*M_PI);
		}

		return sum;
	}

private:

	double _freqs[11];
	double _amps[11];
	double _phases[11];
};