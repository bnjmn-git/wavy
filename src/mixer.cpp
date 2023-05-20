#include "mixer.h"
#include "conversions.h"

std::tuple<Mixer, std::shared_ptr<MixerController>> Mixer::create_mixer(int channels, int sample_rate) {
	auto controller = std::make_shared<MixerController>(channels, sample_rate);
	auto mixer = Mixer(controller);

	return { std::move(mixer), std::move(controller) };
}

MixerController::MixerController(int channels, int sample_rate)
	: _channel_count(channels)
	, _sample_rate(sample_rate)
	, _has_pending(false)
	, _pending_sources()
	, _pending_sources_mtx()
{}

void MixerController::add(std::unique_ptr<Source> source) {
	auto converted_source = std::make_unique<Converter>(std::move(source), _channel_count, _sample_rate);
	
	std::scoped_lock lk(_pending_sources_mtx);
	_pending_sources.push_back(std::move(converted_source));
	_has_pending.store(true, std::memory_order::memory_order_release);
}

Mixer::Mixer(std::shared_ptr<MixerController> input)
	: _input(std::move(input))
	, _sample_count(0)
	, _current_sources()
	, _still_pending()
	, _still_current()
{}

int Mixer::channel_count() const {
	return _input->_channel_count;
}

int Mixer::sample_rate() const {
	return _input->_sample_rate;
}

std::optional<double> Mixer::next_sample() {
	if (_input->_has_pending.load(std::memory_order_acquire)) {
		_start_pending_sources();
	}

	_sample_count += 1;

	auto sum = _sum_current_sources();

	if (_current_sources.empty()) {
		return std::nullopt;
	}

	return sum;
}

void Mixer::_start_pending_sources() {
	_still_pending.clear();
	
	std::scoped_lock lk(_input->_pending_sources_mtx);

	bool in_step = _sample_count % _input->_channel_count == 0;
	for (auto& source : _input->_pending_sources) {
		if (in_step) {
			_current_sources.push_back(std::move(source));
		} else {
			_still_pending.push_back(std::move(source));
		}
	}

	std::swap(_still_pending, _input->_pending_sources);

	bool has_pending = !_input->_pending_sources.empty();
	_input->_has_pending.store(has_pending, std::memory_order_release);
}

double Mixer::_sum_current_sources() {
	_still_current.clear();
	
	auto sum = 0.0;

	for (auto& source : _current_sources) {
		if (auto sample = source->next_sample()) {
			sum += *sample;
			_still_current.push_back(std::move(source));
		}
	}

	std::swap(_still_current, _current_sources);

	return sum;
}