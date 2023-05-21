#pragma once

#include "source.h"

#define _USE_MATH_DEFINES
#include <math.h>

double sine_wave(double phase) {
	return sin(phase);
}

double saw_wave(double phase) {
	return M_2_PI * atan(tan(phase * 0.5));
}

double square_wave(double phase) {
	return sin(phase) < 0.0 ? -1.0 : 1.0;
}

double triangle_wave(double phase) {
	return asin(sin(phase)) * M_2_PI;
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
		auto value = sine_wave(_phase);
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
		auto value = saw_wave(_phase);
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
		auto value = triangle_wave(_phase);
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
		auto value = square_wave(_phase);
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
			sum += sine_wave(_phases[i]) * _amps[i];
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