#pragma once

#include <memory>
#include <chrono>
#include <optional>

#include "audio.h"

class Source;

class OutputStream {
public:

	static std::optional<OutputStream> try_default();

	void start();
	void stop();

	void add(std::unique_ptr<Source> source);
	void add_delayed(std::unique_ptr<Source> source, int delay_milli);

	/*
	* @brief Blocks the calling thread until all 
	*/
	void wait_until_end();

private:

	OutputStream(audio::Device device);

private:

	audio::Device _device;
};