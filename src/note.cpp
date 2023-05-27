#include "note.h"

#include <cstdlib>

std::variant<Note, NoteParseError> Note::from_str(std::string_view str) {
	if (str.length() < 2 || str.length() > 3) {
		return NoteParseErrorUnexpectedLength { (uint32_t)str.length() };
	}

	char copy[4] = {0};
	strncpy(copy, str.data(), str.length());

	char letter_c;
	char modifier_c = 0;
	uint32_t octave;

	if (str.length() == 2) {
		if (sscanf(copy, "%c%u", &letter_c, &octave) != 2) {
			return NoteParseErrorInvalidFormat{};
		}
	} else if (str.length() == 3) {
		if (sscanf(copy, "%c%c%u", &letter_c, &modifier_c, &octave) != 3) {
			return NoteParseErrorInvalidFormat{};
		}
	}

	int base;
	switch (letter_c) {
		case 'C': base = (int)Letter::C; break;
		case 'D': base = (int)Letter::D; break;
		case 'E': base = (int)Letter::E; break;
		case 'F': base = (int)Letter::F; break;
		case 'G': base = (int)Letter::G; break;
		case 'A': base = (int)Letter::A; break;
		case 'B': base = (int)Letter::B; break;

		case 'c':
		case 'd':
		case 'e':
		case 'f':
		case 'g':
		case 'a':
		case 'b':
			return NoteParseErrorInvalidLetter::LowerCase;
		default: return NoteParseErrorInvalidLetter::DoesNotExist;
	}

	if (modifier_c == '#') {
		base += 1;
	} else if (modifier_c == 'b') {
		base -= 1;
	} else if (modifier_c != 0) {
		return NoteParseErrorInvalidModifier{};
	}

	if (base < 0) {
		base += detail::NUM_NOTES;
	}

	auto letter = (Letter)base;

	if (octave > 9) {
		return NoteParseErrorInvalidOctave { octave };
	}

	return Note(letter, (int8_t)octave);
}