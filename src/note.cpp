#include "note.h"

#include <cstdlib>

std::optional<Note> Note::from_str(std::string_view str) {
	if (str.length() < 2 || str.length() > 3) {
		return std::nullopt;
	}

	char copy[4] = {0};
	strncpy(copy, str.data(), str.length());

	char letter_c;
	char modifier_c = 0;
	uint32_t octave;

	if (str.length() == 2) {
		if (sscanf(copy, "%c%u", &letter_c, &octave) != 2) {
			return std::nullopt;
		}
	} else if (str.length() == 3) {
		if (sscanf(copy, "%c%c%u", &letter_c, &modifier_c, &octave) != 3) {
			return std::nullopt;
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
		default: return std::nullopt;
	}

	if (modifier_c == '#') {
		base += 1;
	} else if (modifier_c == 'b') {
		base -= 1;
	} else if (modifier_c != 0) {
		return std::nullopt;
	}

	if (base < 0) {
		base += detail::NUM_NOTES;
	}

	auto letter = (Letter)base;

	return Note(letter, (int8_t)octave);
}