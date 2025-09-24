add_rules("mode.debug", "mode.release", "plugin.compile_commands.autoupdate")

add_requires(
    "nlohmann_json",
    "implot"
)

add_requires("imgui", {configs = {opengl3 = true, glfw = true}})

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
        "ui/*.cpp"
    )