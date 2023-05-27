#include "music.h"
#include "rapidyaml.h"

#include <fstream>

template<class...T>
std::string format_string(char const* fmt, T&&...args) {
	char buffer[512] = {0};
	int size = snprintf(buffer, 512, fmt, std::forward<T>(args)...);
	return std::string(buffer, size);
}

struct RymlError {
	std::string msg;
};

struct InternalErrorUnexpectedNumberOfArgs {
	std::string name;
	int actual;
	int expected;
	InternalErrorUnexpectedNumberOfArgs(std::string name, int expected, int actual)
		: actual(actual)
		, expected(expected)
		, name(std::move(name))
	{}
};

struct InternalErrorMissingField {
	std::string name;

	InternalErrorMissingField(std::string name)
		: name(std::move(name))
	{}
};

struct InternalErrorArgumentUnexpectedType {
	std::string name;
	int arg_pos;
	std::string expected;
	std::string actual;

	InternalErrorArgumentUnexpectedType(std::string name, int arg_pos, std::string expected, std::string actual)
		: expected(std::move(expected))
		, actual(std::move(actual))
		, name(std::move(name))
		, arg_pos(arg_pos)
	{}
};

struct InternalErrorFieldUnexpectedType {
	std::string name;
	std::string expected;
	std::string actual;

	InternalErrorFieldUnexpectedType(std::string name, std::string expected, std::string actual)
		: expected(std::move(expected))
		, actual(std::move(actual))
		, name(std::move(name))
	{}
};

struct InternalErrorNoteParseError {
	NoteParseError error;
};

struct InternalErrorOther {
	std::string msg;
};

using InternalError = std::variant<
	InternalErrorOther,
	InternalErrorUnexpectedNumberOfArgs,
	InternalErrorMissingField,
	InternalErrorArgumentUnexpectedType,
	InternalErrorFieldUnexpectedType,
	InternalErrorNoteParseError
>;

void ryml_error_callback(char const* msg, size_t msg_len, ryml::Location location, void* user_data) {
	char message[1024] = {0};
	auto copy_len = std::min(msg_len, (size_t)1023);
	strncpy(message, msg, copy_len);

	// Has an annoying end of line that should not be there so that the user can
	// format it the way they want to.
	if (message[copy_len - 1] == '\n') {
		message[copy_len - 1] = '\0';
	}

	auto size = snprintf(nullptr, 0, "%s(%llu,%llu): %s", location.name.data(), location.line, location.col, message);
	std::string error_msg(size, '\0');
	sprintf(error_msg.data(), "%s(%llu,%llu): %s", location.name.data(), location.line, location.col, message);
	
	// Rapidyaml requires us to throw or abort when this callback is called. A small flaw in this library.
	throw RymlError { error_msg };
}

static auto parse_time_signature(ryml::ConstNodeRef root)
	-> std::variant<std::optional<TimeSignature>, InternalError>
{
	constexpr c4::csubstr TIME_SIGNATURE_PROP_NAME("time-signature");

	// Time signature is optional, so not an error if not present
	if (root.has_child(TIME_SIGNATURE_PROP_NAME)) {
		auto node = root[TIME_SIGNATURE_PROP_NAME];
		if (!node.is_seq()) {
			return InternalErrorFieldUnexpectedType(TIME_SIGNATURE_PROP_NAME.data(), ryml::NodeType(ryml::NodeType_e::SEQ).type_str(), node.type_str());
		}
		if (node.num_children() != 2) {
			return InternalErrorUnexpectedNumberOfArgs(TIME_SIGNATURE_PROP_NAME.data(), 2, (int)node.num_children());
		}
		
		TimeSignature sig;
		node[0] >> sig.beats_per_bar;
		node[1] >> sig.beat_value;

		return sig;
	} else {
		return std::nullopt;
	}
}

char const* get_val_type_name(ryml::csubstr val) {
	if (val.is_integer()) {
		return "Integer";
	}
	if (val.is_real()) {
		return "Decimal";
	}
	if (val.has_str()) {
		return "String";
	}

	return "Don't know";
}

static auto parse_bpm(ryml::ConstNodeRef root)
	-> std::variant<std::optional<int>, InternalError>
{
	constexpr c4::csubstr BPM_PROP_NAME("bpm");

	if (root.has_child(BPM_PROP_NAME)) {
		auto node = root[BPM_PROP_NAME];
		if (!node.is_keyval()) {
			return InternalErrorFieldUnexpectedType(BPM_PROP_NAME.data(), ryml::NodeType(ryml::NodeType_e::KEYVAL).type_str(), node.type_str());
		}
		if (!node.val().is_integer()) {
			return InternalErrorArgumentUnexpectedType(
				BPM_PROP_NAME.data(),
				0,
				"Integer",
				get_val_type_name(node.val())
			);
		}

		int bpm;
		node >> bpm;
		return bpm;
	} else {
		return std::nullopt;
	}
}

static auto parse_gain(ryml::ConstNodeRef root)
	-> std::variant<std::optional<double>, InternalError>
{
	constexpr c4::csubstr GAIN_PROP_NAME("gain");
	if (root.has_child(GAIN_PROP_NAME)) {
		auto node = root[GAIN_PROP_NAME];
		if (!node.is_keyval()) {
			return InternalErrorFieldUnexpectedType(
				GAIN_PROP_NAME.data(),
				ryml::NodeType(ryml::NodeType_e::KEYVAL).type_str(),
				node.type_str()
			);
		}
		if (!node.val().is_real()) {
			return InternalErrorArgumentUnexpectedType(
				GAIN_PROP_NAME.data(),
				0,
				"Decimal",
				get_val_type_name(node.val())
			);
		}

		double gain;
		node >> gain;
		return gain;
	} else {
		return std::nullopt;
	}
}

struct Duration {
	int count;	// The number of dividends.
	int dividend;	// Duration of the note (quarter note = 4, eigth = 8)

	double note_value() const {
		return (double)count / dividend;
	}
};

struct CommandDelay {
	static constexpr char* NAME = "delay";
	Duration duration;
};

struct CommandRepeat {
	static constexpr char* NAME = "repeat";
	int count;
};

struct CommandEndRepeat {
	static constexpr char* NAME = "end-repeat";
};

struct CommandPlayNote {
	static constexpr char* NAME = "play";

	Note note;
	Duration duration;

	CommandPlayNote(Note note, Duration duration)
		: note(note)
		, duration(duration)
	{}
};

using PatternCommand = std::variant<
	CommandDelay,
	CommandRepeat,
	CommandEndRepeat,
	CommandPlayNote
>;

struct CommandPlayPattern {
	static constexpr char* NAME = "play";

	std::string pattern_name;
};

using TrackCommand = std::variant<
	CommandDelay,
	CommandRepeat,
	CommandEndRepeat,
	CommandPlayPattern
>;

int csubstr_compare(c4::csubstr const& ss, char const* str) {
	return strncmp(ss.data(), str, ss.len);
}

static auto parse_duration_value(ryml::ConstNodeRef node)
	-> std::variant<Duration, InternalError>
{
	if (!node.is_seq()) {
		return InternalErrorFieldUnexpectedType(
			"duration",
			ryml::NodeType(ryml::NodeType_e::SEQ).type_str(),
			node.type_str()
		);
	}
	if (node.num_children() != 2) {
		return InternalErrorUnexpectedNumberOfArgs(
			"duration",
			2,
			(int)node.num_children()
		);
	}

	for (int i = 0; i < 2; ++i) {
		if (!node[i].val().is_integer()) {
			return InternalErrorArgumentUnexpectedType(
				"duration",
				i,
				"Integer",
				get_val_type_name(node[0].val())
			);
		}
	}

	Duration duration;
	node[0] >> duration.count;
	node[1] >> duration.dividend;

	return duration;
}

static auto parse_command_delay(ryml::ConstNodeRef node)
	-> std::variant<CommandDelay, InternalError>
{
	if (node.num_children() != 2) {
		return InternalErrorUnexpectedNumberOfArgs(
			CommandDelay::NAME,
			1,
			(int)node.num_children() - 1
		);
	}

	CommandDelay delay;
	{
		auto res = parse_duration_value(node[1]);
		if (auto err = std::get_if<1>(&res)) {
			return std::move(*err);
		}

		delay.duration = std::get<0>(std::move(res));
	}
	
	return delay;
}

static auto parse_command_repeat(ryml::ConstNodeRef node)
	-> std::variant<CommandRepeat, InternalError>
{
	if (node.num_children() != 2) {
		return InternalErrorUnexpectedNumberOfArgs(
			CommandDelay::NAME,
			1,
			(int)node.num_children() - 1
		);
	}
	if (!node[1].val().is_integer()) {
		return InternalErrorArgumentUnexpectedType(
			CommandRepeat::NAME,
			0,
			"Integer",
			get_val_type_name(node[1].val())
		);
	}

	CommandRepeat repeat;
	node[1] >> repeat.count;
	return repeat;
}

static auto parse_command_end_repeat(ryml::ConstNodeRef node)
	-> std::variant<CommandEndRepeat, InternalError>
{
	if (node.num_children() != 1) {
		return InternalErrorUnexpectedNumberOfArgs(
			CommandEndRepeat::NAME,
			0,
			(int)node.num_children() - 1
		);
	}

	return CommandEndRepeat{};
}

static auto parse_note(ryml::ConstNodeRef node)
	-> std::variant<Note, InternalError>
{
	auto res = Note::from_str(std::string_view(node.val().data(), node.val().len));
	if (auto e = std::get_if<1>(&res)) {
		return InternalErrorNoteParseError{ std::move(*e) };
	}

	return std::get<0>(std::move(res));
}

static auto parse_command_play_note(ryml::ConstNodeRef node)
	-> std::variant<CommandPlayNote, InternalError>
{
	if (node.num_children() != 3) {
		return InternalErrorUnexpectedNumberOfArgs(
			CommandPlayNote::NAME,
			2,
			(int)node.num_children() - 1
		);
	}
	if (!node[1].val().has_str()) {
		return InternalErrorArgumentUnexpectedType(
			CommandPlayNote::NAME,
			0,
			"String",
			get_val_type_name(node[1].val())
		);
	}

	std::optional<Note> note_opt;
	{
		auto res = parse_note(node[1]);
		if (auto ok = std::get_if<0>(&res)) {
			note_opt = *ok;
		} else {
			return std::get<1>(std::move(res));
		}
	}

	auto note = *note_opt;

	Duration duration;
	{
		auto res = parse_duration_value(node[2]);
		if (auto err = std::get_if<1>(&res)) {
			return std::move(*err);
		}

		duration = std::get<0>(std::move(res));
	}

	return CommandPlayNote(note, duration);
}

static auto parse_pattern_command(ryml::ConstNodeRef node)
	-> std::variant<PatternCommand, InternalError>
{
	if (!node.is_seq()) {
		return InternalErrorFieldUnexpectedType(
			"command",
			ryml::NodeType(ryml::NodeType_e::SEQ).type_str(),
			node.type_str()
		);
	}
	if (!node[0].val().has_str()) {
		return InternalErrorOther{ "Command name must be a string" };
	}

	auto name = node[0].val();

	auto pass_through = [](auto res) -> std::variant<PatternCommand, InternalError> {
		if (auto ok = std::get_if<0>(&res)) {
			return std::move(*ok);
		} else {
			return std::get<1>(std::move(res));
		}
	};

	if (csubstr_compare(name, CommandDelay::NAME) == 0) {
		return pass_through(parse_command_delay(node));
	}
	if (csubstr_compare(name, CommandRepeat::NAME) == 0) {
		return pass_through(parse_command_repeat(node));
	}
	if (csubstr_compare(name, CommandEndRepeat::NAME) == 0) {
		return pass_through(parse_command_end_repeat(node));
	}
	if (csubstr_compare(name, CommandPlayNote::NAME) == 0) {
		return pass_through(parse_command_play_note(node));
	}

	return InternalErrorOther{ format_string("Command '%s' does not exist", std::string(name.data(), name.len)) };
}

static std::variant<std::monostate, InternalError> process_pattern_commands(
	std::vector<PatternCommand> commands,
	int resolution_per_beat,
	int bpm,
	TimeSignature time_signature,
	Pattern& pattern
) {
	// We need to translate the raw commands into ones that will actually move
	// the pattern forward, i.e. delay/play. This will involve expanding repeat
	// commands.

	std::vector<PatternCommand> final_commands;
	final_commands.reserve(commands.size());	// Probably gonna need at least num commands.

	std::vector<std::vector<PatternCommand>> repeat_stack;

	std::vector<PatternCommand>* cur_target = &final_commands;

	for (auto& command : commands) {
		if (auto c = std::get_if<CommandRepeat>(&command)) {
			// Start every stack with the repeat command that caused it, so we can track repeat count.
			repeat_stack.push_back({ std::move(*c) });
			cur_target = &repeat_stack.back();
		} else if (auto c = std::get_if<CommandEndRepeat>(&command)) {
			if (repeat_stack.empty()) {
				return InternalErrorOther{ "Extra 'end-repeat' called" };
			}

			// We now need to pop the top of the stack and repeatedly fill the next array in the stack
			// with the contents repeat_count times.

			auto repeat_commands = std::move(repeat_stack.back());
			repeat_stack.pop_back();
			if (repeat_stack.empty()) {
				cur_target = &final_commands;
			} else {
				cur_target = &repeat_stack.back();
			}

			// A repeat stack vector should always have its first element be a CommandRepeat.
			int repeat_count = std::get<CommandRepeat>(repeat_commands[0]).count;

			// We do not count the first CommandRepeat element.
			if (repeat_commands.size() > 1) {
				while (repeat_count > 0) {
					--repeat_count;

					cur_target->insert(cur_target->end(), repeat_commands.begin() + 1, repeat_commands.end());
				}
			}
		} else {
			cur_target->push_back(std::move(command));
		}
	}

	// Missing end-repeat command
	if (!repeat_stack.empty()) {
		return InternalErrorOther{ format_string("Missing %lu 'end-repeat' commands", repeat_stack.size() )};
	}

	auto beat_value = (double)time_signature.beat_value;
	auto elapsed = 0.0;	// Elapsed time in resolution time, stored as double to allow greater precision.

	// Now we have a list of final commands that can be used to generate note events.
	for (auto& command : final_commands) {
		if (auto c = std::get_if<CommandDelay>(&command)) {
			auto beats = beat_value * c->duration.note_value();
			elapsed += beats * resolution_per_beat;
		} else if (auto c = std::get_if<CommandPlayNote>(&command)) {
			auto beats = beat_value * c->duration.note_value();
			auto duration = beats * resolution_per_beat;
			pattern.add_note(NoteEvent(
				c->note,
				(int)floor(elapsed),
				(int)floor(elapsed + duration)
			));
		} else {
			return InternalErrorOther{ "Problem with the software itself" };
		}
	}

	return {};
}

static auto parse_pattern(
	ryml::ConstNodeRef node,
	int resolution_per_beat,
	int bpm,
	TimeSignature time_signature
)
	-> std::variant<Pattern, InternalError>
{
	if (!node.is_map()) {
		return InternalError{};
	}

	constexpr c4::csubstr NAME_PROP_NAME("name");
	constexpr c4::csubstr COMMAND_PROP_NAME("commands");

	if (!node.has_child(NAME_PROP_NAME)) {
		return InternalErrorMissingField(NAME_PROP_NAME.data());
	}
	if (!node.has_child(COMMAND_PROP_NAME)) {
		return InternalErrorMissingField(COMMAND_PROP_NAME.data());
	}

	auto name_node = node[NAME_PROP_NAME];
	if (!name_node.is_keyval()) {
		return InternalErrorMissingField(NAME_PROP_NAME.data());
	}
	if (!name_node.val().has_str()) {
		return InternalErrorArgumentUnexpectedType(
			NAME_PROP_NAME.data(),
			0,
			"String",
			get_val_type_name(name_node.val())
		);
	}

	auto commands_node = node[COMMAND_PROP_NAME];
	if (!commands_node.is_seq()) {
		return InternalErrorFieldUnexpectedType(
			COMMAND_PROP_NAME.data(),
			"String",
			get_val_type_name(commands_node.val())
		);
	}

	Pattern pattern(std::string(name_node.val().data(), name_node.val().len));
	std::vector<PatternCommand> commands;
	commands.reserve(commands_node.num_children());

	for (auto command_node : commands_node.children()) {
		auto res = parse_pattern_command(command_node);
		if (auto err = std::get_if<1>(&res)) {
			return std::move(*err);
		}

		commands.push_back(std::get<0>(std::move(res)));
	}

	if (auto err = std::get_if<1>(&process_pattern_commands(
		std::move(commands),
		resolution_per_beat,
		bpm,
		time_signature,
		pattern
	))) {
		return std::move(*err);
	}
	
	return pattern;
}

static auto parse_patterns(
	ryml::ConstNodeRef root,
	int resolution_per_beat,
	int bpm,
	TimeSignature time_signature
)
	-> std::variant<std::vector<Pattern>, InternalError>
{
	constexpr c4::csubstr PATTERNS_PROP_NAME("patterns");
	if (!root.has_child(PATTERNS_PROP_NAME)) {
		return InternalErrorMissingField(PATTERNS_PROP_NAME.data());
	}

	auto patterns_node = root[PATTERNS_PROP_NAME];
	if (!patterns_node.is_seq()) {
		return InternalErrorFieldUnexpectedType(
			PATTERNS_PROP_NAME.data(),
			ryml::NodeType(ryml::NodeType_e::SEQ).type_str(),
			patterns_node.type_str()
		);
	}

	std::vector<Pattern> patterns;
	patterns.reserve(patterns_node.num_children());
	for (auto pattern_node : patterns_node.children()) {
		auto res = parse_pattern(pattern_node, resolution_per_beat, bpm, time_signature);
		if (auto err = std::get_if<1>(&res)) {
			return std::move(*err);
		}

		patterns.push_back(std::get<0>(std::move(res)));
	}

	return patterns;
}

static auto parse_adsr(ryml::ConstNodeRef node, Adsr default_adsr)
	-> std::variant<Adsr, InternalError>
{
	if (!node.is_map()) {
		return InternalErrorFieldUnexpectedType(
			"adsr",
			ryml::NodeType(ryml::NodeType_e::MAP).type_str(),
			node.type_str()
		);
	}

	auto adsr = default_adsr;

	constexpr c4::csubstr ATTACK_PROP_NAME("attack");
	constexpr c4::csubstr DECAY_PROP_NAME("decay");
	constexpr c4::csubstr SUSTAIN_PROP_NAME("sustain");
	constexpr c4::csubstr RELEASE_PROP_NAME("release");

	auto deserialize_double_if_exists = [&](c4::csubstr const& name, double& out_value) {
		if (node.has_child(name)) {
			node[name] >> out_value;
		}
	};

	deserialize_double_if_exists(ATTACK_PROP_NAME, adsr.attack);
	deserialize_double_if_exists(DECAY_PROP_NAME, adsr.decay);
	deserialize_double_if_exists(SUSTAIN_PROP_NAME, adsr.sustain);
	deserialize_double_if_exists(RELEASE_PROP_NAME, adsr.release);

	return adsr;
}

static auto parse_instrument(
	ryml::ConstNodeRef node
) -> std::variant<Instrument, InternalError>
{
	constexpr c4::csubstr NAME_PROP_NAME("name");
	constexpr c4::csubstr SOURCE_PROP_NAME("source");
	constexpr c4::csubstr ADSR_PROP_NAME("adsr");

	if (!node.is_map()) {
		return InternalErrorFieldUnexpectedType(
			"Instrument",
			ryml::NodeType(ryml::NodeType_e::MAP).type_str(),
			node.type_str()
		);
	}

	if (!node.has_child(NAME_PROP_NAME)) {
		return InternalErrorMissingField(NAME_PROP_NAME.data());
	}

	if (!node.has_child(SOURCE_PROP_NAME)) {
		return InternalErrorMissingField(SOURCE_PROP_NAME.data());
	}

	auto name_node = node[NAME_PROP_NAME];
	if (!name_node.val().has_str()) {
		return InternalErrorMissingField(NAME_PROP_NAME.data());
	}

	std::string name;
	name_node >> name;

	auto source_node = node[SOURCE_PROP_NAME];

	InstrumentSource source_type = InstrumentSourceWave::Sine;
	// Using builtin source
	if (source_node.is_keyval() && source_node.val().has_str()) {
		auto source_node_ss = source_node.val();
		if (csubstr_compare(source_node_ss, "sine") == 0) {
			source_type = InstrumentSourceWave::Sine;
		} else if (csubstr_compare(source_node_ss, "triangle") == 0) {
			source_type = InstrumentSourceWave::Triangle;
		} else if (csubstr_compare(source_node_ss, "square") == 0) {
			source_type = InstrumentSourceWave::Square;
		} else if (csubstr_compare(source_node_ss, "saw") == 0) {
			source_type = InstrumentSourceWave::Saw;
		} else if (csubstr_compare(source_node_ss, "piano") == 0) {
			source_type = InstrumentSourceWave::Piano;
		} else if (csubstr_compare(source_node_ss, "violin") == 0) {
			source_type = InstrumentSourceWave::Violin;
		} else {
			return InternalErrorOther{};
		}
	} else if (source_node.is_map() && source_node.has_child("sample")) {
		InstrumentSourceSample sample;
		source_node["sample"] >> sample.filename;
		source_type = std::move(sample);
	}

	// Optional value
	constexpr Adsr default_adsr = Adsr(0.03, 0.0, 1.0, 0.03);
	Adsr adsr = default_adsr;
	if (node.has_child(ADSR_PROP_NAME)) {
		auto res = parse_adsr(node[ADSR_PROP_NAME], default_adsr);
		if (auto err = std::get_if<1>(&res)) {
			return std::move(*err);
		}

		adsr = std::get<0>(res);
	}

	return Instrument(std::move(name), std::move(source_type), adsr);
}

static auto parse_instruments(
	ryml::ConstNodeRef root
) -> std::variant<std::vector<Instrument>, InternalError>
{
	constexpr c4::csubstr INSTRUMENTS_PROP_NAME("instruments");
	if (!root.has_child(INSTRUMENTS_PROP_NAME)) {
		return InternalErrorMissingField(INSTRUMENTS_PROP_NAME.data());
	}

	auto instruments_node = root[INSTRUMENTS_PROP_NAME];
	if (!instruments_node.is_seq()) {
		return InternalErrorFieldUnexpectedType(
			INSTRUMENTS_PROP_NAME.data(),
			ryml::NodeType(ryml::NodeType_e::SEQ).type_str(),
			instruments_node.type_str()
		);
	}

	std::vector<Instrument> instruments;
	instruments.reserve(instruments_node.num_children());
	for (auto instrument_node : instruments_node) {
		auto res = parse_instrument(instrument_node);
		if (auto ok = std::get_if<0>(&res)) {
			instruments.push_back(std::move(*ok));
		} else {
			return std::get<1>(std::move(res));
		}
	}

	return instruments;
}

static auto parse_command_play_pattern(ryml::ConstNodeRef node)
	-> std::variant<CommandPlayPattern, InternalError>
{
	if (node.num_children() != 2) {
		return InternalErrorUnexpectedNumberOfArgs(
			CommandPlayPattern::NAME,
			1,
			(int)node.num_children() - 1
		);
	}
	if (!node[1].val().has_str()) {
		return InternalErrorArgumentUnexpectedType(
			CommandPlayPattern::NAME,
			0,
			"String",
			get_val_type_name(node[1].val())
		);
	}

	std::string pattern_name;
	node[1] >> pattern_name;

	return CommandPlayPattern{ std::move(pattern_name) };
}

static auto parse_track_command(ryml::ConstNodeRef node)
	-> std::variant<TrackCommand, InternalError>
{
	if (!node.is_seq()) {
		return InternalErrorFieldUnexpectedType(
			"track command",
			ryml::NodeType(ryml::NodeType_e::SEQ).type_str(),
			node.type_str()
		);
	}
	if (!node[0].val().has_str()) {
		return InternalErrorArgumentUnexpectedType(
			"track command",
			0,
			"String",
			get_val_type_name(node[0].val())
		);
	}

	auto name = node[0].val();

	auto pass_through = [](auto res) -> std::variant<TrackCommand, InternalError> {
		if (auto ok = std::get_if<0>(&res)) {
			return std::move(*ok);
		} else {
			return std::get<1>(std::move(res));
		}
	};

	if (csubstr_compare(name, CommandDelay::NAME) == 0) {
		return pass_through(parse_command_delay(node));
	}
	if (csubstr_compare(name, CommandRepeat::NAME) == 0) {
		return pass_through(parse_command_repeat(node));
	}
	if (csubstr_compare(name, CommandEndRepeat::NAME) == 0) {
		return pass_through(parse_command_end_repeat(node));
	}
	if (csubstr_compare(name, CommandPlayPattern::NAME) == 0) {
		return pass_through(parse_command_play_pattern(node));
	}

	return InternalErrorOther{ format_string("Command '%s' does not exist", std::string(name.data(), name.len).c_str()) };
}

static std::variant<std::monostate, InternalError> process_track_commands(
	std::vector<TrackCommand> commands,
	int resolution_per_beat,
	int bpm,
	TimeSignature time_signature,
	std::vector<Pattern> const& patterns,
	Track& track
) {
	// We need to translate the raw commands into ones that will actually move
	// the pattern forward, i.e. delay/play. This will involve expanding repeat
	// commands.

	std::vector<TrackCommand> final_commands;
	final_commands.reserve(commands.size());	// Probably gonna need at least num commands.

	std::vector<std::vector<TrackCommand>> repeat_stack;

	std::vector<TrackCommand>* cur_target = &final_commands;

	for (auto& command : commands) {
		if (auto c = std::get_if<CommandRepeat>(&command)) {
			// Start every stack with the repeat command that caused it, so we can track repeat count.
			repeat_stack.push_back({ std::move(*c) });
			cur_target = &repeat_stack.back();
		} else if (auto c = std::get_if<CommandEndRepeat>(&command)) {
			if (repeat_stack.empty()) {
				return InternalErrorOther{ "Extra 'end-repeat' called" };
			}

			// We now need to pop the top of the stack and repeatedly fill the next array in the stack
			// with the contents repeat_count times.

			auto repeat_commands = std::move(repeat_stack.back());
			repeat_stack.pop_back();
			if (repeat_stack.empty()) {
				cur_target = &final_commands;
			} else {
				cur_target = &repeat_stack.back();
			}

			// A repeat stack vector should always have its first element be a CommandRepeat.
			int repeat_count = std::get<CommandRepeat>(repeat_commands[0]).count;

			// We do not count the first CommandRepeat element.
			if (repeat_commands.size() > 1) {
				while (repeat_count > 0) {
					--repeat_count;

					cur_target->insert(cur_target->end(), repeat_commands.begin() + 1, repeat_commands.end());
				}
			}
		} else {
			cur_target->push_back(std::move(command));
		}
	}

	auto beat_value = (double)time_signature.beat_value;
	auto elapsed = 0.0;	// Elapsed time in resolution time, stored as double to allow greater precision.

	// Now we have a list of final commands that can be used to generate pattern events.
	for (auto& command : final_commands) {
		if (auto c = std::get_if<CommandDelay>(&command)) {
			auto beats = beat_value * c->duration.note_value();
			elapsed += beats * resolution_per_beat;
		} else if (auto c = std::get_if<CommandPlayPattern>(&command)) {
			auto pattern = std::find_if(patterns.begin(), patterns.end(), [&](Pattern const& pattern) {
				return pattern.name().compare(c->pattern_name) == 0;
			});
			if (pattern == patterns.end()) {
				return InternalErrorOther{ format_string("Missing %lu 'end-repeat' commands", repeat_stack.size() )};
			}

			auto pattern_idx = pattern - patterns.begin();
			auto duration = pattern->duration();

			track.add_pattern(PatternEvent(
				(int)pattern_idx,
				(int)floor(elapsed),
				(int)floor(elapsed + duration)
			));

			// Every play command will increase elapsed, as patterns cannot overlap and must be played
			// sequentially in the order they appear.
			elapsed += (double)duration;
		} else {
			return InternalErrorOther{ "Problem with software itself" };
		}
	}

	return {};
}

static auto parse_track(
	ryml::ConstNodeRef node,
	std::vector<Pattern> const& patterns,
	std::vector<Instrument> const& instruments,
	int resolution_per_beat,
	int bpm,
	TimeSignature time_signature
) -> std::variant<Track, InternalError>
{
	if (!node.is_map()) {
		return InternalErrorFieldUnexpectedType(
			"track",
			ryml::NodeType(ryml::NodeType_e::MAP).type_str(),
			node.type_str()
		);
	}

	constexpr c4::csubstr NAME_PROP_NAME("name");
	constexpr c4::csubstr INSTRUMENT_PROP_NAME("instrument");
	constexpr c4::csubstr COMMANDS_PROP_NAME("commands");

	if (!node.has_child(NAME_PROP_NAME)) {
		return InternalErrorMissingField(NAME_PROP_NAME.data());
	}
	if (!node.has_child(INSTRUMENT_PROP_NAME)) {
		return InternalErrorMissingField(INSTRUMENT_PROP_NAME.data());
	}
	if (!node.has_child(COMMANDS_PROP_NAME)) {
		return InternalErrorMissingField(COMMANDS_PROP_NAME.data());
	}

	auto name_node = node[NAME_PROP_NAME];
	auto instrument_node = node[INSTRUMENT_PROP_NAME];
	auto commands_node = node[COMMANDS_PROP_NAME];

	if (!name_node.is_keyval()) {
		return InternalErrorFieldUnexpectedType(
			"track name",
			ryml::NodeType(ryml::NodeType_e::KEYVAL).type_str(),
			name_node.type_str()
		);
	}
	if (!name_node.val().has_str()) {
		return InternalErrorArgumentUnexpectedType(
			"track name",
			0,
			"String",
			get_val_type_name(name_node.val())
		);
	}

	if (!instrument_node.is_keyval()) {
		return InternalErrorFieldUnexpectedType(
			"track instrument",
			ryml::NodeType(ryml::NodeType_e::KEYVAL).type_str(),
			name_node.type_str()
		);
	}
	if (!instrument_node.val().has_str()) {
		return InternalErrorArgumentUnexpectedType(
			"track instrument",
			0,
			"String",
			get_val_type_name(instrument_node.val())
		);
	}

	if (!commands_node.is_seq()) {
		return InternalErrorFieldUnexpectedType(
			"track commands",
			ryml::NodeType(ryml::NodeType_e::SEQ).type_str(),
			commands_node.type_str()
		);
	}

	std::string_view instrument_name(instrument_node.val().data(), instrument_node.val().len);
	auto instrument = std::find_if(instruments.begin(), instruments.end(), [&](Instrument const& instrument) {
		return instrument.name().compare(instrument_name) == 0;
	});
	if (instrument == instruments.end()) {
		return InternalErrorOther{ format_string("Instrument '%s' does not exist", std::string(instrument_name.data(), instrument_name.length())) };
	}

	auto instrument_idx = instrument - instruments.begin();

	double gain = 1.0;
	if (node.has_child("gain")) {
		auto gain_node = node["gain"];
		if (!gain_node.is_keyval()) {
			return InternalErrorFieldUnexpectedType(
				"gain",
				ryml::NodeType(ryml::NodeType_e::KEYVAL).type_str(),
				gain_node.type_str()
			);
		}
		if (!gain_node.val().is_real()) {
			return InternalErrorArgumentUnexpectedType(
				"gain",
				0,
				"Decimal",
				get_val_type_name(gain_node.val())
			);
		}
		gain_node >> gain;
	}

	Track track(std::string(name_node.val().data(), name_node.val().len), (int)instrument_idx, gain);
	std::vector<TrackCommand> commands;
	commands.reserve(commands_node.num_children());

	for (auto command_node : commands_node.children()) {
		auto res = parse_track_command(command_node);
		if (auto ok = std::get_if<0>(&res)) {
			commands.push_back(std::move(*ok));
		} else {
			return std::get<1>(std::move(res));
		}
	}

	if (auto err = std::get_if<1>(&process_track_commands(
		std::move(commands),
		resolution_per_beat,
		bpm,
		time_signature,
		patterns,
		track
	))) {
		return std::move(*err);
	}

	return track;
}

static auto parse_tracks(
	ryml::ConstNodeRef root,
	std::vector<Pattern> const& patterns,
	std::vector<Instrument> const& instruments,
	int resolution_per_beat,
	int bpm,
	TimeSignature time_signature
) -> std::variant<std::vector<Track>, InternalError>
{
	constexpr c4::csubstr TRACKS_PROP_NAME("tracks");
	if (!root.has_child(TRACKS_PROP_NAME)) {
		return InternalErrorMissingField(TRACKS_PROP_NAME.data());
	}

	auto tracks_node = root[TRACKS_PROP_NAME];
	if (!tracks_node.is_seq()) {
		return InternalErrorFieldUnexpectedType(
			TRACKS_PROP_NAME.data(),
			ryml::NodeType(ryml::NodeType_e::SEQ).type_str(),
			tracks_node.type_str()
		);
	}

	std::vector<Track> tracks;
	tracks.reserve(tracks_node.num_children());
	for (auto track_node : tracks_node.children()) {
		auto res = parse_track(
			track_node,
			patterns,
			instruments,
			resolution_per_beat,
			bpm,
			time_signature
		);

		if (auto ok = std::get_if<0>(&res)) {
			tracks.push_back(std::move(*ok));
		} else {
			return std::get<1>(std::move(res));
		}
	}

	return tracks;
}

MusicError map_internal_to_music_error(InternalError err) {
	if (auto e = std::get_if<InternalErrorUnexpectedNumberOfArgs>(&err)) {
		return MusicErrorParse(format_string(
			"Unexpected number of arguments to '%s' (expected: %i, actual: %i)",
			e->name.c_str(), e->expected, e->actual
		));
	}
	if (auto e = std::get_if<InternalErrorMissingField>(&err)) {
		return MusicErrorParse(format_string(
			"Missing field '%s'",
			e->name.c_str()
		));
	}
	if (auto e = std::get_if<InternalErrorArgumentUnexpectedType>(&err)) {
		return MusicErrorParse(format_string(
			"Argument '%i' has unexpected type for '%s' (expected: %s, actual: %s)",
			e->arg_pos, e->name.c_str(), e->expected.c_str(), e->actual.c_str()
		));
	}
	if (auto e = std::get_if<InternalErrorFieldUnexpectedType>(&err)) {
		return MusicErrorParse(format_string(
			"Field '%s' has unexpected type (expected: %s, actual: %s)",
			e->name.c_str(), e->expected.c_str(), e->actual.c_str()
		));
	}
	if (auto e = std::get_if<InternalErrorNoteParseError>(&err)) {
		auto& error = e->error;
		if (auto ne = std::get_if<NoteParseErrorInvalidFormat>(&error)) {
			return MusicErrorParse("A note had invalid format. A correct format is of the form C4, A#3, Gb5. Letter must be capitalized, and octave less than 10");
		}
		if (auto ne = std::get_if<NoteParseErrorInvalidLetter>(&error)) {
			switch (*ne) {
				case NoteParseErrorInvalidLetter::DoesNotExist:
					return MusicErrorParse("A note had a letter that does not exist in standard western notation");
				case NoteParseErrorInvalidLetter::LowerCase:
					return MusicErrorParse("A note had a lower case letter. Must be capitalized");
				default: return MusicErrorParse("Something wrong with the software");
			}
		}
		if (auto ne = std::get_if<NoteParseErrorInvalidModifier>(&error)) {
			return MusicErrorParse("A note had an invalid modifier. Must be # or b or left empty");
		}
		if (auto ne = std::get_if<NoteParseErrorInvalidOctave>(&error)) {
			return MusicErrorParse(format_string("A note had an octave of %u, must be less than 10", ne->octave));
		}
		if (auto ne = std::get_if<NoteParseErrorUnexpectedLength>(&error)) {
			return MusicErrorParse(format_string("A note had %u characters, must be 2 or 3", ne->length));
		}
	}

	return MusicErrorParse("Something went wrong");
}

std::variant<Music, MusicError> Music::import(std::string_view filename) {
	auto parser = ryml::Parser(
		c4::yml::Callbacks(nullptr, nullptr, nullptr, ryml_error_callback),
		c4::yml::ParserOptions().locations(true)
	);

	std::ifstream file(filename.data(), std::ios::ate);
	if (!file.is_open()) {
		return MusicErrorFile { strerror(errno) };
	}

	auto file_size = file.tellg();
	std::string source(file_size, '\0');
	file.seekg(0);
	file.read(source.data(), file_size);

	file.close();

	c4::yml::Tree tree;
	
	try {
		tree = parser.parse_in_arena(
			c4::csubstr(filename.data(), filename.length()),
			c4::csubstr(source.data(), source.length())
		);
	} catch (RymlError e) {
		return MusicErrorParse { std::move(e.msg) };
	}

	auto root = tree.rootref();

	TimeSignature time_signature;
	// I'm too in love with rust's error handling.
	{
		auto res = parse_time_signature(root);
		if (auto ok = std::get_if<0>(&res)) {
			// Default to common time signature
			time_signature = ok->value_or(TimeSignature{ 4, 4 });
		} else {
			return map_internal_to_music_error(std::get<1>(std::move(res)));
		}
	}
	
	int bpm;
	{
		auto res = parse_bpm(root);
		if (auto ok = std::get_if<0>(&res)) {
			// Default to 120
			bpm = ok->value_or(120);
		} else {
			return map_internal_to_music_error(std::get<1>(std::move(res)));
		}
	}

	double gain;
	{
		auto res = parse_gain(root);
		if (auto ok = std::get_if<0>(&res)) {
			gain = ok->value_or(1.0);
		} else {
			return map_internal_to_music_error(std::get<1>(std::move(res)));
		}
	}

	std::vector<Pattern> patterns;
	{
		auto res = parse_patterns(root, get_resolution_per_beat(), bpm, time_signature);
		if (auto ok = std::get_if<0>(&res)) {
			patterns = std::move(*ok);
		} else {
			return map_internal_to_music_error(std::get<1>(std::move(res)));
		}
	}

	std::vector<Instrument> instruments;
	{
		auto res = parse_instruments(root);
		if (auto ok = std::get_if<0>(&res)) {
			instruments = std::move(*ok);
		} else {
			return map_internal_to_music_error(std::get<1>(std::move(res)));
		}
	}

	std::vector<Track> tracks;
	{
		auto res = parse_tracks(root, patterns, instruments, get_resolution_per_beat(), bpm, time_signature);
		if (auto ok = std::get_if<0>(&res)) {
			tracks = std::move(*ok);
		} else {
			return map_internal_to_music_error(std::get<1>(std::move(res)));
		}
	}

	Music music;
	music._time_signature = time_signature;
	music._bpm = bpm;
	music._gain = gain;
	music._patterns = std::move(patterns);
	music._instruments = std::move(instruments);
	music._tracks = std::move(tracks);

	return music;
}