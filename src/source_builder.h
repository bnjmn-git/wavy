#pragma once

#include <memory>
#include <functional>
#include <chrono>
#include <stdint.h>
#include "source.h"
#include "buffered.h"

class SourceBuilder {
public:

	SourceBuilder(std::unique_ptr<Source> source)
		: _source(std::move(source))
	{}

	SourceBuilder& amplify(double amp) {
		_source = std::make_unique<Amplify>(std::move(_source), amp);
		return *this;
	}

	template<class Rep, class Period>
	SourceBuilder& duration(std::chrono::duration<Rep, Period> duration) {
		_source = std::make_unique<Duration>(std::move(_source), std::chrono::duration<uint64_t, std::nano>(duration).count());
		return *this;
	}

	template<class Rep, class Period>
	SourceBuilder& delay(std::chrono::duration<Rep, Period> delay) {
		_source = std::make_unique<Delay>(std::move(_source), std::chrono::duration<uint64_t, std::nano>(delay).count());
		return *this;
	}

	SourceBuilder& filter(std::function<double(double, FilterInfo)> callback) {
		_source = std::make_unique<Filter>(std::move(_source), callback);
		return *this;
	}

	SourceBuilder& buffered(int buffer_size = 1024) {
		_source = std::make_unique<Buffered>(std::move(_source), buffer_size);
		return *this;
	}

	std::unique_ptr<Source> build() {
		return std::move(_source);
	}

private:

	std::unique_ptr<Source> _source;
};