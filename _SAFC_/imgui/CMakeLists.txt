# Shared by both supported CMake entry points. The existing playback core is
# Windows-specific; ImGui itself uses the GLFW platform and OpenGL 3 backends.
if(NOT WIN32)
	message(FATAL_ERROR "SAFC_BUILD_IMGUI_PREVIEW currently requires Windows")
endif()

find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(SAFCImGui
	main.cpp
	folded_theme.cpp
	playback_session.cpp
	../SAFC_InnerModules/syncore_output.cpp)
target_include_directories(SAFCImGui PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/.."
	"${CMAKE_CURRENT_SOURCE_DIR}/../../utility/include")
target_compile_features(SAFCImGui PRIVATE cxx_std_23)
target_compile_definitions(SAFCImGui PRIVATE UNICODE _UNICODE)
target_link_libraries(SAFCImGui PRIVATE
	imgui::imgui glfw OpenGL::GL
	Boost::headers GLUT::GLUT zstd::libzstd
	comdlg32 shell32 ole32 winmm)

# GLUT is only a transitive dependency of the existing MIDI core's utility
# headers. No SAFGUIF rendering or legacy app translation units are built here.
if(MSVC)
	target_compile_options(SAFCImGui PRIVATE /permissive /EHsc)
endif()

if(TARGET safsyn-windows)
	target_link_libraries(SAFCImGui PRIVATE safsyn-windows)
	target_compile_definitions(SAFCImGui PRIVATE SAFC_WITH_SYNCORE=1)
endif()

if(BUILD_TESTING)
	add_test(NAME safc-imgui-smoke COMMAND SAFCImGui --smoke
		"${CMAKE_CURRENT_BINARY_DIR}/imgui-smoke.bmp")
	set_tests_properties(safc-imgui-smoke PROPERTIES TIMEOUT 45)
endif()
