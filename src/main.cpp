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
#include "oscillators.h"
#include "music.h"

#include "concurrentqueue.h"

struct ConcurrentQueueTraits : public moodycamel::ConcurrentQueueDefaultTraits {
	static constexpr size_t BLOCK_SIZE = 512;
};

void log_error(char const* msg) {
	fprintf(stderr, "[ERROR] %s\n", msg);
}

int main() {
	auto res = Music::import("examples/abc.yaml");
	if (auto e = std::get_if<MusicError>(&res)) {
		if (auto e2 = std::get_if<MusicErrorParse>(e)) {
			log_error(e2->msg.c_str());
		} else if (auto e2 = std::get_if<MusicErrorFile>(e)) {
			log_error(e2->msg.c_str());
		}
		return 1;
	}

	auto notea = Note::from_str("C#4");
	auto noteb = Note::from_str("Ab9");
	auto notec = Note::from_str("Ab9u");
	auto noted = Note::from_str("Ab10");
	auto notee = Note::from_str("A10");
	auto notef = Note::from_str("A4");
	auto noteg = Note::from_str("a4");

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

	auto filter = [](double sample, FilterInfo info) {
		auto max_samples = info.sample_rate * 3;
		auto x = (double)info.current_sample / max_samples;
		auto k = log(0.005);
		auto fade_out = exp(k*x);
		fade_out = std::max(0.0, std::min(1.0, fade_out));
		return sample * fade_out * fade_out;
	};

	auto [mixer_, mixer_controller] = Mixer::create_mixer(2, 48000);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::Ab, 2).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::Eb, 3).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(200))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::G, 3).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(200))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::Bb, 3).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(400))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::C, 4).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(600))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::Eb, 4).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(800))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::Bb, 4).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(1000))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::G, 4).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(1200))
		.build()
	);

	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::G, 2).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(4000))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::G, 3).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(4000))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::Bb, 3).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(4000))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::D, 4).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(4000))
		.build()
	);
	mixer_controller->add(
		SourceBuilder(std::make_unique<PianoWave>(Note(Letter::F, 4).freq()))
		.duration(std::chrono::milliseconds(3000))
		.filter(filter)
		.delay(std::chrono::milliseconds(4000))
		.build()
	);


	auto mixer = std::make_unique<Mixer>(std::move(mixer_));
	auto output = SourceBuilder(std::move(mixer))
		.amplify(0.5)
		.buffered(1200)
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

	auto start = std::chrono::high_resolution_clock::now();

	while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() < 10000) {
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