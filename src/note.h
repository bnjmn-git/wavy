#pragma once

#include <stdint.h>
#include <math.h>
#include <string_view>
#include <variant>

enum class Letter : uint8_t {
	// C is the first variant as standard music notation has octaves
	// begin at C.
	C = 0,
	Csh,
	D,
	Dsh,
	E,
	F,
	Fsh,
	G,
	Gsh,
	A,
	Ash,
	B,

	Cb = B,
	Db = Csh,
	Eb = Dsh,
	Fb = E,
	Gb = Fsh,
	Ab = Gsh,
	Bb = Ash,

	Bsh = C,
	Esh = F,
};

namespace detail {
	constexpr int NUM_NOTES = 12;
	
	inline constexpr int get_note_idx(Letter letter, uint8_t octave) {
		auto offset = (int)letter;
		auto base = NUM_NOTES * octave;
		return base + offset;
	}

	constexpr int A4_NOTE_IDX = get_note_idx(Letter::A, 4);
	constexpr double A4_FREQ = 440.0;
}

struct NoteParseErrorUnexpectedLength { uint32_t length; };
struct NoteParseErrorInvalidOctave { uint32_t octave; };
enum struct NoteParseErrorInvalidLetter {
	LowerCase,
	DoesNotExist
};
struct NoteParseErrorInvalidModifier {};
struct NoteParseErrorInvalidFormat {};

using NoteParseError = std::variant<
	NoteParseErrorUnexpectedLength,
	NoteParseErrorInvalidOctave,
	NoteParseErrorInvalidLetter,
	NoteParseErrorInvalidModifier,
	NoteParseErrorInvalidFormat
>;

class Note {
public:

	Letter letter;
	uint8_t octave;

	constexpr Note(Letter letter, uint8_t octave)
		: letter(letter)
		, octave(octave)
	{}

	double freq() const {
		auto note_idx = _get_note_idx();
		auto offset_from_a4 = (double)(note_idx - detail::A4_NOTE_IDX);
		return detail::A4_FREQ * exp2(offset_from_a4 / detail::NUM_NOTES);
	}

	/*
	 * @brief Makes a note from a string.
	 * @param str Must be in the format "Letter [Modifier] Octave". For example,
	 * "C#4", "Ab1", "G3". The letter must be capitalized, and octaves no greater than 9.
	*/
	static std::variant<Note, NoteParseError> from_str(std::string_view str);

private:

	int _get_note_idx() const {
		return detail::get_note_idx(letter, octave);
	}
};