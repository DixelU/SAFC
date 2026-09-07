# Shared by both supported CMake entry points. The playback and processing core is
# Windows-specific; ImGui itself uses the GLFW platform and OpenGL 3 backends.
if(NOT WIN32)
	message(FATAL_ERROR "SAFC_BUILD_IMGUI currently requires Windows")
endif()

find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_library(safc_imgui_core STATIC
	folded_theme.cpp
	playback_session.cpp
	video_export_panel.cpp
	editor_panel.cpp
	analysis_panel.cpp
	mapping_panel.cpp
	preferences.cpp
	project_session.cpp
	project_panel.cpp
	cli.cpp
	../JSON/JSON.cpp
	../JSON/JSONValue.cpp
	../app/project_model.cpp
	../SAFC_InnerModules/midi_editor_tools.cpp
	../SAFC_InnerModules/compressed_midi_event_source.cpp
	../SAFC_InnerModules/simple_player_video_export.cpp
	../SAFC_InnerModules/syncore_output.cpp)
target_include_directories(safc_imgui_core PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/.."
	"${CMAKE_CURRENT_SOURCE_DIR}/../../utility/include")
target_compile_features(safc_imgui_core PUBLIC cxx_std_23)
target_compile_definitions(safc_imgui_core PUBLIC UNICODE _UNICODE)
target_link_libraries(safc_imgui_core PUBLIC
	imgui::imgui glfw OpenGL::GL
	Boost::headers zstd::libzstd
	LibArchive::LibArchive comdlg32 shell32 ole32 winmm advapi32
	mfplat mfreadwrite mfuuid mf propsys)
# Compile the frontend entry point and existing icon/version resource once.
# SAFC is the default application; SAFCImGui retains the preview target and path
# for existing users, scripts, and captures. Both execute the identical frontend.
add_library(safc_imgui_entry OBJECT main.cpp ../_SAFC_.rc)
target_link_libraries(safc_imgui_entry PRIVATE safc_imgui_core)
add_executable(SAFC $<TARGET_OBJECTS:safc_imgui_entry>)
target_link_libraries(SAFC PRIVATE safc_imgui_core)
set_target_properties(SAFC PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(SAFCImGui $<TARGET_OBJECTS:safc_imgui_entry>)
target_link_libraries(SAFCImGui PRIVATE safc_imgui_core)

# No SAFGUIF widget headers, GLUT event loop, or GLUT link dependency are part
# of the application core. Legacy GUI code is isolated in the optional target.
if(MSVC)
	target_compile_options(safc_imgui_core PUBLIC /permissive /EHsc)
endif()

if(TARGET safsyn-windows)
	target_link_libraries(safc_imgui_core PUBLIC safsyn-windows)
	target_compile_definitions(safc_imgui_core PUBLIC SAFC_WITH_SYNCORE=1)
endif()

if(BUILD_TESTING)
	add_executable(safc-imgui-cli-tests cli_regression.cpp)
	target_link_libraries(safc-imgui-cli-tests PRIVATE safc_imgui_core)
	add_test(NAME safc-imgui-cli COMMAND safc-imgui-cli-tests
		"${CMAKE_CURRENT_BINARY_DIR}/cli-regression-output")
	set_tests_properties(safc-imgui-cli PROPERTIES TIMEOUT 60)
	add_executable(safc-imgui-editor-tests ../tests/imgui_editor_regression.cpp)
	target_link_libraries(safc-imgui-editor-tests PRIVATE safc_imgui_core)
	add_test(NAME safc-imgui-editor COMMAND safc-imgui-editor-tests)
	set_tests_properties(safc-imgui-editor PROPERTIES TIMEOUT 60)
	add_executable(safc-imgui-analysis-tests analysis_mapping_regression.cpp)
	target_link_libraries(safc-imgui-analysis-tests PRIVATE safc_imgui_core)
	add_test(NAME safc-imgui-analysis COMMAND safc-imgui-analysis-tests
		"${CMAKE_CURRENT_BINARY_DIR}/analysis-regression-output")
	set_tests_properties(safc-imgui-analysis PROPERTIES TIMEOUT 45)
	add_executable(safc-imgui-playback-tests ../tests/imgui_playback_tests.cpp)
	target_link_libraries(safc-imgui-playback-tests PRIVATE safc_imgui_core)
	add_test(NAME safc-imgui-playback COMMAND safc-imgui-playback-tests)
	set_tests_properties(safc-imgui-playback PROPERTIES TIMEOUT 60)
	add_test(NAME safc-imgui-smoke COMMAND SAFCImGui --smoke
		"${CMAKE_CURRENT_BINARY_DIR}/imgui-smoke.bmp")
	set_tests_properties(safc-imgui-smoke PROPERTIES TIMEOUT 45)
	add_test(NAME safc-imgui-workflows COMMAND SAFCImGui --workflow-smoke
		"${CMAKE_CURRENT_BINARY_DIR}/workflows.bmp")
	set_tests_properties(safc-imgui-workflows PROPERTIES TIMEOUT 60)
endif()
