#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <tuple>

#include "source.h"

/*
 * Allows adding sources to the corresponding Mixer
*/
class MixerController {
public:

	MixerController(int channels, int sample_rate);

	void add(std::unique_ptr<Source> source);

private:

	friend class Mixer;

private:

	// Needed so we don't have to lock the mutex to see if pending_sources is empty.
	std::atomic<bool> _has_pending;

	std::vector<std::unique_ptr<Source>> _pending_sources;
	std::mutex _pending_sources_mtx;

	int _channel_count;
	int _sample_rate;
};

class Mixer : public Source {
public:

	static std::tuple<Mixer, std::shared_ptr<MixerController>> create_mixer(int channels, int sample_rate);

	int channel_count() const override;
	int sample_rate() const override;
	std::optional<double> next_sample() override;

private:

	Mixer(std::shared_ptr<MixerController> input);

	void _start_pending_sources();
	double _sum_current_sources();

private:

	// Pending sounds.
	std::shared_ptr<MixerController> _input;

	// Current sources producing samples.
	std::vector<std::unique_ptr<Source>> _current_sources;

	// The number of samples produced so far.
	uint32_t _sample_count;

	std::vector<std::unique_ptr<Source>> _still_pending;
	std::vector<std::unique_ptr<Source>> _still_current;
};