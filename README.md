# Wavy
A fun little music maker.

## Features
- Resusable patterns
- Basic oscillators such as sine, triangle, square and saw
- Sample from .wav files
- Export .wav file

## Build Instructions
Building only works with Visual Studio supporting C++17 or later. If you have MSBuild in your paths,
simply run build.bat in a command line as follows:<br>
`build GENERATOR CONFIGURATION`
- `GENERATOR` is the version of Visual Studio on your system to use. Possible values are:
  - vs2017
  - vs2019
  - vs2022
- `CONFIGURATION` is the build configuration. Possible values are `debug` or `release`.

If MSBuild is not installed or isn't in your path, then you must use [premake](https://premake.github.io)
which comes with the source. Run the premake5.exe under the premake folder in a command line as follows:<br>
- `premake5 GENERATOR ROOT_DIR`
  - `GENERATOR` is the same as above.
  - `ROOT_DIR` is the directory holding the top-level premake5.lua script.

This will generate a .sln file in the build directory (created if not already there). You can then run this
solution in Visual Studio and build.

## How To Use
Once you have the Wavy.exe file, you can run it through the command line as such:<br>
`wavy FILE [-e EXPORT_FILE]`
- `FILE` is the path to the YAML file containing your song.
- `EXPORT_FILE` is the file you wish to export your song in .wav format.

Your music file needs to be a YAML file. You can refer to basic_example.yml in the examples folder.
In the top level, you can define:
- `bpm` with an integer to determine the beats per minute. Defaults to 120.
- `time-signature` with an array of size 2, with the first element being the number of beats per bar and the second is the note that takes a beat. Defaults to [4,4].
- `gain` with a decimal to adjust overall volume. Defaults to 1.0.

Required data falls under three sections
- `patterns` is where you define your notes to play.
- `instruments` for the sounds your notes should make.
- `tracks` is to sequence and reuse your patterns easily.

`patterns` is an array that contains maps of two key-values, `name` and [`commands`](#commands). `name` must be a unique string that will identify your pattern when used in tracks. `commands` are an array of commands to execute.
All commands are encoded as arrays, where the first element identifies the command to use and the rest are
the arguments for the command.

`instruments` is an array that contains maps of three key-values: `name`, `source`, and `adsr`. `name` must be a unique string that will identify the instrument when used in tracks. `source` is the source of sound
that will be played. It can have a builtin string value of either `sine`, `triangle`, `square`, `saw`, `piano`,
or `violin`. Otherwise, a sample can be used by specifying a key-value called `sample` with a WAV file relative to your YAML to sample from. `adsr` is optional, and has values `attack`, `decay`, `sustain`, and `release`, where all
must hold decimal values.

`tracks` is an array that contains maps of four key-values: `name`, `instrument`, `gain`, and
[`commands`](#commands). `name` uniquely identifies a tracks. `instrument` holds the name of the instrument
you wish to use. `gain` is optional and holds a decimal value controlling the volume for only this track.

### Commands
The following is a list of possible commands for patterns and tracks.

- `[repeat, COUNT]` repeats a section between this command and an end-repeat command. `COUNT`
	is the number of times to repeat.
- `[end-repeat]` marks the end of a repeat command.
- `[delay, DURATION]` moves forward the cursor position where the next play command will execute. `DURATION` is an array of size 2, where the elements form a numerator and denominator. For example, [1,4]
  means that the command lasts for a 1/4 note, or quartet in music terminology. [1,8] means a semi-quaver and so on.
- `[play, NOTE, DURATION]` is used in the context of a pattern. It plays `NOTE` for a certain duration. This
command does not play in sequence, which means successive play commands will not move the cursor position 
by `DURATION`. `NOTE` must be in the format `{LETTER}[MODIFIER]{Octave}`. `LETTER` is the letter of the note
in standard music, i.e. A, B, C, D, E, F, and G. `MODIFIER` is optional, and can be either `b` or `#` for flat
and sharp respectively. `OCTAVE` is a positive integer less than 10. Possible `NOTE` values are C4, Ab3, G#9, etc.
- `[play, PATTERN]` is used in the context of a track, and plays `PATTERN`. `PATTERN` must refer to the name
of one of the patterns under the `patterns` map. This command is played sequentially, so successive play
commands will play one after the other.