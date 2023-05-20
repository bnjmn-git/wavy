#include <iostream>
#define _USE_MATH_DEFINES
#include <math.h>
#include <thread>
#include <fstream>

#include "audio.h"
#include "source.h"
#include "source_builder.h"
#include "mixer.h"
#include "note.h"

#include "concurrentqueue.h"

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

struct ConcurrentQueueTraits : public moodycamel::ConcurrentQueueDefaultTraits {
	static constexpr size_t BLOCK_SIZE = 512;
};

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
		auto value = square_wave(_phase);
		auto dt = 1.0 / sample_rate();
		_phase += PI2 * _freq * dt;
		_phase = fmod(_phase, PI2);

		return value;
	}

private:

	double _freq;
	double _phase;
	static constexpr double PI2 = M_PI * 2.0;
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
		_phase += PI2 * _freq * dt;
		_phase = fmod(_phase, PI2);

		return value;
	}

private:

	double _freq;
	double _phase;
	static constexpr double PI2 = M_PI * 2.0;
};

int main() {
	auto instance = audio::Instance();
	auto device = *instance.get_default_output_device();
	
	device.open(48000);

	auto buffer_size = device.buffer_size();
	auto channel_count = device.channel_count();
	auto frame_count = buffer_size*channel_count;

	// We give queue double the frame count to give the sending thread a margin
	// to prevent the audio thread from dequeuing an empty queue.
	moodycamel::ConcurrentQueue<float, ConcurrentQueueTraits> queue(frame_count * 2);

	// Buffer for audio thread to deque samples in bulk.
	std::vector<float> samples_buffer(frame_count, 0.0f);

	// Holds how many samples of the next audio buffer should we zero out
	// due to an empty queue.
	int empty_overlap_count = 0;

	auto [mixer_, mixer_controller] = Mixer::create_mixer(2, 48000);
	mixer_controller->add(
		SourceBuilder(std::make_unique<SineWave>(Note(Letter::Gsh, 2).freq()))
		.duration(std::chrono::milliseconds(3000))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<SineWave>(Note(Letter::Esh, 3).freq()))
		.duration(std::chrono::milliseconds(2800))
		.delay(std::chrono::milliseconds(200))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<SineWave>(Note(Letter::G, 3).freq()))
		.duration(std::chrono::milliseconds(2800))
		.delay(std::chrono::milliseconds(200))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<SineWave>(Note(Letter::Ash, 3).freq()))
		.duration(std::chrono::milliseconds(2600))
		.delay(std::chrono::milliseconds(400))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<SineWave>(Note(Letter::C, 4).freq()))
		.duration(std::chrono::milliseconds(2400))
		.delay(std::chrono::milliseconds(600))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<SineWave>(Note(Letter::Dsh, 4).freq()))
		.duration(std::chrono::milliseconds(2200))
		.delay(std::chrono::milliseconds(800))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<SineWave>(Note(Letter::Ash, 4).freq()))
		.duration(std::chrono::milliseconds(2000))
		.delay(std::chrono::milliseconds(1000))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<SineWave>(Note(Letter::G, 4).freq()))
		.duration(std::chrono::milliseconds(1800))
		.delay(std::chrono::milliseconds(1200))
		.build()
	);

	auto mixer = std::make_unique<Mixer>(std::move(mixer_));
	auto output = SourceBuilder(std::move(mixer))
		.amplify(0.2)
		.buffered(4800)
		.build();

	device.start([&](float* data, int channel_count, int sample_count) {
		auto samples_remaining = channel_count * sample_count;

		if (empty_overlap_count > 0) {
			memset(data, 0, sizeof(float) * empty_overlap_count);
			data += empty_overlap_count;
			samples_remaining -= empty_overlap_count;
			empty_overlap_count = 0;
		}
		
		while (samples_remaining > 0) {
			auto count = (int)queue.try_dequeue_bulk(samples_buffer.data(), samples_remaining);
			if (count > 0) {
				data = std::copy_n(samples_buffer.begin(), count, data);
			} else {
				// When the queue is empty, we need to zero out channel_count samples
				// so that the next enqueue will be at a sample corresponding to the
				// correct channel. If the samples_remaining is less than channel_count,
				// then the zeroing needs to also occur in the next audio buffer, so we
				// keep track of that as well.
				if (samples_remaining < channel_count) {
					empty_overlap_count = channel_count - samples_remaining;
				}

				count = std::min(samples_remaining, channel_count);
				memset(data, 0, sizeof(float) * count);
			}

			samples_remaining -= count;
		}
	});
	
	uint64_t acc_time = 0;
	int acc_samples = 0;

	while (acc_samples < 48000*8) {
		// time += dt;

		for (int i = 0; i < channel_count; ++i) {
			auto start = std::chrono::high_resolution_clock::now();
			auto value = output->next_sample().value_or(0.0);
			value = tanh(value);
			auto end = std::chrono::high_resolution_clock::now();

			acc_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
			acc_samples++;
			while (!queue.try_enqueue((float)value));
		}
	}

	device.close();

	printf("%lf\n", (double)acc_time/acc_samples);

	return 0;
}