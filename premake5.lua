workspace "Wavy"
	kind "ConsoleApp"
	configurations { "debug", "release" }
	architecture "x86_64"
	language "C++"
	cppdialect "C++17"
	location "build/"
	targetdir "bin/%{cfg.buildcfg}"

	project "Wavy"

		files {
			"src/**.h",
			"src/**.cpp"
		}

		filter "configurations:debug*"
			defines { "DEBUG" }
			symbols "On"
			optimize "Off"

		filter "configurations:release*"
			symbols "Off"
			optimize "Full"