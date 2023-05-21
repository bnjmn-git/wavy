workspace "Wavy"
	kind "ConsoleApp"
	configurations { "debug", "release" }
	architecture "x86_64"
	language "C++"
	cppdialect "C++17"
	location "build/"
	targetdir "bin/%{cfg.buildcfg}"

	defines { "_CRT_SECURE_NO_WARNINGS" }

	project "Wavy"

		files {
			"src/**.h",
			"src/**.cpp"
		}

		filter "configurations:debug*"
			symbols "On"
			optimize "Off"

		filter "configurations:release*"
			symbols "Off"
			optimize "Full"