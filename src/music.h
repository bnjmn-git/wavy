#pragma once

#include <variant>
#include <optional>
#include <string_view>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>

#include "note.h"

struct TimeSignature {
	int beats_per_bar;
	int beat_value;
};

struct MusicErrorParse {
	std::string msg;
};

struct MusicErrorFile {
	std::string msg;
};

using MusicError = std::variant<
	MusicErrorParse,
	MusicErrorFile
>;

class Adsr {
public:

	// The seconds until reaching peak value after 'press'
	double attack;
	
	// The seconds of decay from peak to sustain after attack
	double decay;

	// Level of amplitude in the range [0.0, 1.0] to maintain after decay
	double sustain;

	// The seconds until amplitude reaches 0 after 'release'
	double release;

public:

	constexpr Adsr(double a, double d, double s, double r)
		: attack(a)
		, decay(d)
		, sustain(s)
		, release(r)
	{}

	/*
	 * @brief Evaluates the amplitude of the ADSR given the parameters.
	 * @param elapsed_press The elapsed seconds since press
	 * @param elapsed_release Optional elapsed seconds since release. If none, then release is not calculated.
	 * @return The amplitude to multiply with a sample
	*/
	double evaluate(double elapsed_press, std::optional<double> elapsed_release) const {
		// assert(elapsed >= 0.0);

		double value = 0.0;
		if (elapsed_press < attack) {
			value = elapsed_press / attack;
		} else if (elapsed_press < attack + decay) {
			auto t = elapsed_press / (attack + decay);
			value = 1.0 + (sustain - 1.0) * t;
		} else {
			value = sustain;
		}

		if (!elapsed_release) {
			return value;
		}

		auto t = *elapsed_release / release;
		return value * (1.0 - t);
	}
};

enum class InstrumentSourceType {
	Sine,
	Triangle,
	Square,
	Saw,
	Piano,
	Violin
};

class Instrument {
public:

	Instrument(std::string name, InstrumentSourceType type, Adsr adsr)
		: _name(std::move(name))
		, _type(type)
		, _adsr(adsr)
	{}

	std::string_view name() const {
		return _name;
	}

	InstrumentSourceType type() const {
		return _type;
	}

	Adsr adsr() const {
		return _adsr;
	}

private:

	std::string _name;
	InstrumentSourceType _type;
	Adsr _adsr;
};

class NoteEvent {
public:

	// The start of this event in resolution time.
	// See the comments on Music::get_resolution_per_beat() for more info.
	int start;
	int end;
	Note note;

public:

	NoteEvent(Note note, int start, int end)
		: note(note)
		, start(start)
		, end(end)
	{}

	NoteEvent move(int offset) const {
		return NoteEvent(
			note,
			start + offset,
			end + offset
		);
	}
};

class Pattern {
public:

	Pattern(std::string name)
		: _name(std::move(name))
		, _events()
		, _duration(0)
	{}

	std::string_view name() const {
		return _name;
	}

	void add_note(NoteEvent note) {
		_events.push_back(note);
		_duration = std::max(_duration, note.end);
	}

	int duration() const { return _duration; }

	std::vector<NoteEvent> const& events() const { return _events; }

private:

	std::string _name;
	std::vector<NoteEvent> _events;
	int _duration;	// In resolution time
};

class PatternEvent {
public:

	int start;
	int end;
	int pattern_idx;

	PatternEvent(int pattern_idx, int start, int end)
		: pattern_idx(pattern_idx)
		, start(start)
		, end(end)
	{}
};

class Track {
public:

	Track(std::string name, int instrument_idx, double gain)
		: _name(name)
		, _instrument_idx(instrument_idx)
		, _gain(gain)
	{}

	std::string_view name() const {
		return _name;
	}

	int instrument_idx() const { return _instrument_idx; }
	std::vector<PatternEvent> const& events() const { return _events; }

	void add_pattern(PatternEvent pattern) {
		_events.push_back(std::move(pattern));
	}

	double gain() const { return _gain; }

private:

	std::string _name;
	int _instrument_idx;
	double _gain;
	std::vector<PatternEvent> _events;
};

namespace music {
	/*
	 * Utility function that maps a value in resolution time to seconds.
	*/
	constexpr double map_resolution_to_seconds(int value, int resolution, int bpm) {
		auto seconds_per_beat = 60.0 / bpm;
		auto beats = (double)value / resolution;
		return beats * seconds_per_beat;
	}

	/**
	 * Utility function that maps seconds to a resolution value.
	*/
	constexpr int map_seconds_to_resolution(double seconds, int resolution, int bpm) {
		auto beats_per_second = (double)bpm / 60.0;
		auto beats = seconds * beats_per_second;
		return (int)(beats * resolution);
	}
}

class Music {
public:

	// using Instruments = std::unordered_map<std::string, Instrument>;
	// using Patterns = std::unordered_map<std::string, Pattern>;

	static std::variant<Music, MusicError> import(std::string_view filename);

	int get_bpm() const { return _bpm; }
	double get_gain() const { return _gain; }

	/*
	 * @brief Gets the resolution of a beat. The resolution subdivides a single beat into discrete
	 * values. So 96 would divide a beat into 96 discrete values. This is useful to allow exact
	 * placement of notes and avoid floating point comparisons, or to guarantee a sorted order.
	*/
	static constexpr int get_resolution_per_beat() { return 96; }
	TimeSignature get_time_signature() const { return _time_signature; }

	std::vector<Pattern> const& get_patterns() const { return _patterns; }
	std::vector<Instrument> const& get_instruments() const { return _instruments; }
	std::vector<Track> const& get_tracks() const { return _tracks; }
	
private:

	int _bpm;
	double _gain;
	TimeSignature _time_signature;
	std::vector<Instrument> _instruments;
	std::vector<Pattern> _patterns;
	std::vector<Track> _tracks;
};