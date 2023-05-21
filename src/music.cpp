#include "music.h"
#include "rapidyaml.h"

#include <fstream>

struct RymlError {
	std::string msg;
};

struct InternalErrorUnexpectedNumberOfArgs {
	int actual;
	int expected;
	InternalErrorUnexpectedNumberOfArgs(int actual, int expected)
		: actual(actual)
		, expected(expected)
	{}
};

struct InternalErrorOther {};

using InternalError = std::variant<
	InternalErrorOther,
	InternalErrorUnexpectedNumberOfArgs
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
		assert(node.is_seq());
		if (node.num_children() != 2) {
			return InternalError(InternalErrorUnexpectedNumberOfArgs(node.num_children(), 2));
		}
		
		TimeSignature sig;
		node[0] >> sig.beats_per_bar;
		node[1] >> sig.beat_value;

		return sig;
	} else {
		return std::nullopt;
	}
}

static auto parse_bpm(ryml::ConstNodeRef root)
	-> std::variant<std::optional<int>, InternalError>
{
	constexpr c4::csubstr BPM_PROP_NAME("bpm");

	if (root.has_child(BPM_PROP_NAME)) {
		auto node = root[BPM_PROP_NAME];
		// assert(node.has_val());
		// assert(node.val().is_integer());
		if (!node.is_keyval() || !node.val().is_integer()) {
			return InternalErrorOther{};
		}

		int bpm;
		node >> bpm;
		return bpm;
	} else {
		return std::nullopt;
	}
}

struct CommandDelay {
	static constexpr char* NAME = "delay";
	int delay;
};

struct CommandRepeat {
	static constexpr char* NAME = "repeat";
	int count;
};

struct CommandEndRepeat {
	static constexpr char* NAME = "end-repeat";
};

struct CommandPlay {
	static constexpr char* NAME = "play";

	Note note;
	int duration;

	CommandPlay(Note note, int duration)
		: note(note)
		, duration(duration)
	{}
};

using PatternCommand = std::variant<
	CommandDelay,
	CommandRepeat,
	CommandEndRepeat,
	CommandPlay
>;

int csubstr_compare(c4::csubstr const& ss, char const* str) {
	return strncmp(ss.data(), str, ss.len);
}

static auto parse_command_delay(ryml::ConstNodeRef node)
	-> std::variant<CommandDelay, InternalError>
{
	if (node.num_children() != 2 || !node[1].val().is_integer()) {
		return InternalErrorOther{};
	}

	CommandDelay delay;
	node[1] >> delay.delay;
	return delay;
}

static auto parse_command_repeat(ryml::ConstNodeRef node)
	-> std::variant<CommandRepeat, InternalError>
{
	if (node.num_children() != 2 || !node[1].val().is_integer()) {
		return InternalErrorOther{};
	}

	CommandRepeat repeat;
	node[1] >> repeat.count;
	return repeat;
}

static auto parse_command_end_repeat(ryml::ConstNodeRef node)
	-> std::variant<CommandEndRepeat, InternalError>
{
	if (node.num_children() != 1) {
		return InternalErrorOther{};
	}

	return CommandEndRepeat{};
}

static auto parse_note(ryml::ConstNodeRef node)
	-> std::variant<Note, InternalError>
{
	if (auto note = Note::from_str(std::string_view(node.val().data(), node.val().len))) {
		return *note;
	}

	return InternalErrorOther{};
}

static auto parse_command_play(ryml::ConstNodeRef node)
	-> std::variant<CommandPlay, InternalError>
{
	if (
		node.num_children() != 3 ||
		!node[1].val().has_str() ||
		!node[2].val().is_integer()
	) {
		return InternalErrorOther{};
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

	int duration;
	node[2] >> duration;

	return CommandPlay(note, duration);
}

static auto parse_pattern_command(ryml::ConstNodeRef node)
	-> std::variant<PatternCommand, InternalError>
{
	if (!node.is_seq() || !node[0].val().has_str()) {
		return InternalErrorOther{};
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
	if (csubstr_compare(name, CommandPlay::NAME) == 0) {
		return pass_through(parse_command_play(node));
	}

	return InternalErrorOther{};
}

static void process_commands(
	std::vector<PatternCommand> const& commands,
	int resolution_per_beat,
	int bpm,
	Pattern& pattern
) {

}

static auto parse_pattern(
	ryml::ConstNodeRef node,
	int resolution_per_beat,
	int bpm
)
	-> std::variant<Pattern, InternalError>
{
	if (!node.is_map()) {
		return InternalError{};
	}

	constexpr c4::csubstr NAME_PROP_NAME("name");
	constexpr c4::csubstr COMMAND_PROP_NAME("commands");

	if (!node.has_child(NAME_PROP_NAME) || !node.has_child(COMMAND_PROP_NAME)) {
		return InternalErrorOther{};
	}

	auto name_node = node[NAME_PROP_NAME];
	if (!name_node.is_keyval() || !name_node.val().has_str()) {
		return InternalErrorOther{};
	}

	auto commands_node = node[COMMAND_PROP_NAME];
	if (!commands_node.is_seq()) {
		return InternalErrorOther{};
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

	process_commands(commands, resolution_per_beat, bpm, pattern);
	
	return pattern;
}

static auto parse_patterns(
	ryml::ConstNodeRef root,
	int resolution_per_beat,
	int bpm
)
	-> std::variant<std::vector<Pattern>, InternalError>
{
	constexpr c4::csubstr PATTERNS_PROP_NAME("patterns");
	if (!root.has_child(PATTERNS_PROP_NAME)) {
		return InternalErrorOther{};
	}

	auto patterns_node = root[PATTERNS_PROP_NAME];
	if (!patterns_node.is_seq()) {
		return InternalErrorOther{};
	}

	std::vector<Pattern> patterns;
	patterns.reserve(patterns_node.num_children());
	for (auto pattern_node : patterns_node.children()) {
		auto res = parse_pattern(pattern_node, resolution_per_beat, bpm);
		if (auto err = std::get_if<1>(&res)) {
			return std::move(*err);
		}

		patterns.push_back(std::get<0>(std::move(res)));
	}

	return std::vector<Pattern>{};
}

MusicError map_internal_to_music_error(InternalError e) {
	return MusicErrorParse{};
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

	std::vector<Pattern> patterns;
	{
		auto res = parse_patterns(root);
		if (auto ok = std::get_if<0>(&res)) {
			patterns = std::move(*ok);
		} else {
			return map_internal_to_music_error(std::get<1>(std::move(res)));
		}
	}

	Music music;
	music._time_signature = time_signature;
	music._bpm = bpm;

	// Don't remember if return conversion will implicitly remove
	return std::move(music);
}