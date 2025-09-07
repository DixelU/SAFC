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

    --if is_plat("windows") then
    --    add_includedirs("../externals/imgui_backend/win32")
    --end

    --if is_plat("linux") then
    --    add_includedirs("../externals/imgui_backend/linux")
    --end

    add_files(
        "../externals/sfd/*.c",
        "../externals/log_c/*.c"
    )

    --if is_plat("windows") then
    --    add_files("../externals/imgui_backend/win32/*.cpp")
    --end

    --if is_plat("linux") then
    --    add_files("../externals/imgui_backend/linux/*.cpp")
    --end

    add_files(
        "*.cpp",
        "ui/*.cpp"
    )
--add_includedirs("include")
--add_links("nlohmann_json", "imgui", "implot")