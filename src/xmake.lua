add_rules("mode.debug", "mode.release", "plugin.compile_commands.autoupdate")

set_languages("c++23")

add_requires(
	"nlohmann_json",
	"implot"
)

add_requires("imgui", {configs = {opengl3 = true, glfw = true}})

if is_plat("windows") then
	add_cxflags("/utf-8")

	add_defines(
		"UNICODE",
		"_UNICODE",
		"_CRT_SECURE_NO_WARNINGS"
	)

	-- Optional: Set for specific configurations
	add_syslinks("user32", "shell32", "comdlg32")
end

if is_plat("linux") then
	set_symbols("debug")

	add_syslinks("pthread", "GL")
end

target("SAFC")
	set_kind("binary")

	add_defines(
		"LOGC__USER_SETTINGS"
	)

	add_packages(
		"imgui"
	)

	add_includedirs(
		"../externals/",
		"../externals/btree"
	)

	add_files(
		"../externals/sfd/*.c",
		"../externals/log_c/*.c"
	)

	add_files(
		"*.cpp",
		"ui/*.cpp",
		"core/*.cpp"
	)