#include <iostream>
#define _USE_MATH_DEFINES
#include <math.h>
#include <thread>
#include <fstream>
#include <tuple>
#include <filesystem>

#include "audio.h"
#include "source.h"
#include "source_builder.h"
#include "mixer.h"
#include "note.h"
#include "oscillators.h"
#include "music.h"
#include "wave_importer.h"

#include "concurrentqueue.h"

struct ConcurrentQueueTraits : public moodycamel::ConcurrentQueueDefaultTraits {
	static constexpr size_t BLOCK_SIZE = 512;
};

void log_error(char const* msg) {
	fprintf(stderr, "[ERROR] %s\n", msg);
}

using SourcePtr = std::unique_ptr<Source>;

static std::optional<SourcePtr> create_source_from_note_event(
	NoteEvent const& event,
	Instrument const& instrument,
	double gain,
	int resolution_per_beat,
	int bpm,
	std::filesystem::path const& music_base_path
) {
	auto freq = event.note.freq();
	auto adsr = instrument.adsr();

	// Need to allow time for adsr release to have effect.
	auto duration_seconds = music::map_resolution_to_seconds(event.end - event.start, resolution_per_beat, bpm) + adsr.release;
	SourcePtr source;
	
	if (auto wave = std::get_if<InstrumentSourceWave>(&instrument.source())) {
		switch (*wave) {
			default:
			case InstrumentSourceWave::Sine:
				source = std::make_unique<SineWave>(freq);
			break;
			case InstrumentSourceWave::Triangle:
				source = std::make_unique<TriangleWave>(freq);
			break;
			case InstrumentSourceWave::Square:
				source = std::make_unique<SquareWave>(freq);
			break;
			case InstrumentSourceWave::Saw:
				source = std::make_unique<SawWave>(freq);
			break;
			case InstrumentSourceWave::Piano:
				source = std::make_unique<PianoWave>(freq);
			break;
			case InstrumentSourceWave::Violin:
				source = std::make_unique<ViolinWave>(freq);
			break;
		}

		source = SourceBuilder(std::move(source))
			.duration(std::chrono::microseconds((int64_t)(duration_seconds * 1e6)))
			.filter([adsr](double sample, FilterInfo info) {
				auto total_samples = *info.get_total_samples();
				auto release_sample_start = total_samples - (int)(adsr.release * info.sample_rate);
				
				auto is_release = info.current_sample >= release_sample_start;
				
				double value;
				if (is_release) {
					auto elapsed_release = (double)(info.current_sample - release_sample_start) / info.sample_rate;
					auto elapsed = (double)release_sample_start / info.sample_rate;
					value = adsr.evaluate(elapsed, elapsed_release);
				} else {
					auto elapsed = (double)info.current_sample / info.sample_rate;
					value = adsr.evaluate(elapsed, std::nullopt);
				}
				return sample * value;
			})
			.build();
	} else if (auto sample = std::get_if<InstrumentSourceSample>(&instrument.source())) {
		auto path = (music_base_path / sample->filename);
		auto file_opt = WaveFile::read(path.string());
		if (!file_opt) {
			fprintf(stderr, "[ERROR] Could not find sample at '%ws'\n", path.c_str());
			return std::nullopt;
		}

		source = std::make_unique<WaveFile>(std::move(*file_opt));
		source = SourceBuilder(std::move(source)).buffered(4096).build();
	}

	source = SourceBuilder(std::move(source))
		.amplify(gain)
		.build();

	return source;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		return 1;
	}

	auto music_filename = std::filesystem::path(argv[1]);
	auto music_base_path = music_filename.parent_path();

	auto res = Music::import(music_filename.string());

	if (auto e = std::get_if<MusicError>(&res)) {
		if (auto e2 = std::get_if<MusicErrorParse>(e)) {
			log_error(e2->msg.c_str());
		} else if (auto e2 = std::get_if<MusicErrorFile>(e)) {
			log_error(e2->msg.c_str());
		}
		return 1;
	}

	auto music = std::get<0>(std::move(res));
	auto gain = music.get_gain();
	auto& tracks = music.get_tracks();
	auto& instruments = music.get_instruments();
	auto& patterns = music.get_patterns();
	auto bpm = music.get_bpm();
	auto time_signature = music.get_time_signature();

	std::vector<std::tuple<int, SourcePtr>> sources;

	for (auto& track : tracks) {
		auto& instrument = instruments[track.instrument_idx()];
		for (auto& track_event : track.events()) {
			auto& pattern = patterns[track_event.pattern_idx];
			for (auto& note_event : pattern.events()) {
				auto moved_note_event = note_event.move(track_event.start);
				auto source_opt = create_source_from_note_event(
					moved_note_event,
					instrument,
					track.gain(),
					Music::get_resolution_per_beat(),
					bpm,
					music_base_path
				);

				if (!source_opt) {
					return 1;
				}
				
				sources.push_back(std::make_tuple(
					moved_note_event.start,
					std::move(*source_opt)
				));
			}
		}
	}

	// Sort in descending order of starting resolution times.
	std::sort(sources.begin(), sources.end(), [](auto& lhs, auto& rhs) {
		return std::get<0>(lhs) > std::get<0>(rhs);
	});

	auto instance = audio::Instance();
	auto device = *instance.get_default_output_device();

	auto& sample_rates = device.available_sample_rates();
	int sample_rate = 48000;
	auto sample_rate_it = std::lower_bound(sample_rates.begin(), sample_rates.end(), sample_rate);
	sample_rate = sample_rate_it == sample_rates.end() ? sample_rates.back() : *sample_rate_it;
	
	device.open(sample_rate);

	auto buffer_size = device.buffer_size();
	auto channel_count = device.channel_count();
	auto frame_count = buffer_size*channel_count;
	sample_rate = device.sample_rate();

	// We give queue double the frame count to give the sending thread a margin
	// to prevent the audio thread from dequeuing an empty queue.
	moodycamel::ConcurrentQueue<float, ConcurrentQueueTraits> queue(frame_count * 2);

	// Buffer for audio thread to deque samples in bulk.
	std::vector<float> samples_buffer(frame_count, 0.0f);

	// Holds how many samples of the next audio buffer should we zero out
	// due to an empty queue.
	int empty_overlap_count = 0;

	device.start([&](float* data, int channel_count, int sample_count) {
		auto samples_remaining = channel_count * sample_count;

		if (empty_overlap_count > 0 && empty_overlap_count <= samples_remaining) {
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

	auto [mixer, mixer_controller] = Mixer::create_mixer(channel_count, sample_rate);
	auto output = SourceBuilder(std::move(mixer)).amplify(gain).buffered(1024).build();

	double time = 0.0;
	double dt = 1.0 / (sample_rate * channel_count);

	int64_t acc_nano = 0;
	int64_t acc_count = 0;

	std::vector<double> samples;
	samples.reserve(sample_rate * 10);

	while (true) {
		auto start = std::chrono::high_resolution_clock::now();
		auto sample_opt = output->next_sample();
		auto end = std::chrono::high_resolution_clock::now();
		acc_nano += (end - start).count();
		acc_count += 1;

		auto sample = 0.0;
		if (sample_opt) {
			sample = *sample_opt;
			sample = tanh(sample);
			samples.push_back(sample);
		} else {
			if (sources.empty()) {
				break;
			}
		}

		// while(!queue.try_enqueue((float)sample));

		auto resolution_time = music::map_seconds_to_resolution(time, Music::get_resolution_per_beat(), bpm);
		while (!sources.empty() && std::get<0>(sources.back()) <= resolution_time) {
			auto source = std::get<1>(std::move(sources.back()));
			sources.pop_back();
			mixer_controller->add(std::move(source));
		}

		time += dt;
	}
	
	device.close();

	wave::export_samples_as_wave("examples/wave.wav", sample_rate, channel_count, samples.data(), samples.size());

	printf("%lf", (double)acc_nano/acc_count);

	return 0;
}