#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>

#include "video_export_panel.h"
#include "playback_session.h"
#include "folded_theme.h"
#include "../SAFC_InnerModules/playback_event_source.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace safc::imgui_ui
{
namespace
{
std::string display_path(const std::wstring& path)
{
	if (path.empty())
		return "Unsaved editor snapshot";
	const int size =
		WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), result.data(), size, nullptr, nullptr);
	return result;
}

float fraction(std::uint64_t value, std::uint64_t total)
{
	return total ? static_cast<float>(std::clamp(static_cast<double>(value) / total, 0.0, 1.0)) : 0.f;
}

bool midi_extension(const std::wstring& path)
{
	auto extension = std::filesystem::path(path).extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
	return extension == L".mid" || extension == L".midi";
}

// The exporter's own check, so invalid settings are reported before the
// save dialog instead of after it.
std::string settings_problem(const simple_player_video_settings& settings, const syncore_preferences& preferences)
{
	return simple_player_video_settings_problem(settings, preferences);
}

using video_codec = simple_player_video_codec;
using audio_codec = simple_player_video_audio_codec;
using encoder_list = std::vector<simple_player_video_encoder_info>;

bool asf_codec(video_codec codec)
{
	return codec == video_codec::wmv9 || codec == video_codec::vc1;
}

bool usable(const simple_player_video_encoder_info& encoder, video_codec codec, bool hardware)
{
	return encoder.codec == codec && encoder.writable && (hardware || !encoder.hardware);
}

bool codec_available(const encoder_list& encoders, video_codec codec, bool hardware)
{
	return std::any_of(encoders.begin(), encoders.end(),
		[&](const auto& encoder) { return usable(encoder, codec, hardware); });
}

bool input_available(const encoder_list& encoders, const simple_player_video_settings& settings,
	simple_player_video_chroma chroma, std::uint32_t bit_depth)
{
	return std::any_of(encoders.begin(), encoders.end(), [&](const auto& encoder)
	{
		return usable(encoder, settings.codec, settings.hardware_encoder) &&
			encoder.inputs[static_cast<int>(chroma)][bit_depth > 8 ? 1 : 0];
	});
}

std::uint32_t nearest_of(std::uint32_t value, std::initializer_list<std::uint32_t> options)
{
	return *std::min_element(options.begin(), options.end(), [&](std::uint32_t left, std::uint32_t right)
	{
		return (std::max)(left, value) - (std::min)(left, value) < (std::max)(right, value) - (std::min)(right, value);
	});
}

// Keeps dependent choices valid after the codec or audio codec changes, so a
// combo never shows a value the new codec cannot take.
void fit_to_codec(simple_player_video_settings& settings)
{
	if (settings.codec != video_codec::hevc)
		settings.chroma = simple_player_video_chroma::yuv420;
	if (settings.codec != video_codec::hevc && settings.codec != video_codec::av1)
		settings.bit_depth = 8;
	if (!simple_player_video_audio_supported(settings.codec, settings.audio_codec))
	{
		const bool lossy = settings.audio_codec == audio_codec::aac || settings.audio_codec == audio_codec::wma ||
			settings.audio_codec == audio_codec::wma_pro;
		settings.audio_codec = asf_codec(settings.codec) ? (lossy ? audio_codec::wma : audio_codec::pcm)
			: (lossy ? audio_codec::aac : audio_codec::flac);
	}
	const std::uint32_t qp_limit = settings.codec == video_codec::av1 ? 255 : 51;
	settings.qp = (std::min)(settings.qp, qp_limit);
	for (auto* qp : {&settings.min_qp, &settings.max_qp})
		if (*qp != simple_player_video_encoder_default)
			*qp = (std::min)(*qp, static_cast<std::int32_t>(qp_limit));
	auto& rate = settings.audio_sample_rate;
	switch (settings.audio_codec)
	{
	case audio_codec::aac:
		settings.audio_bitrate_kbps = nearest_of(settings.audio_bitrate_kbps, {96, 128, 160, 192});
		if (rate != 0 && rate != 44100 && rate != 48000)
			rate = 48000;
		break;
	case audio_codec::wma:
		settings.audio_bitrate_kbps = std::clamp(settings.audio_bitrate_kbps, 32U, 320U);
		if (rate != 0 && rate != 44100 && rate != 48000)
			rate = 48000;
		break;
	case audio_codec::wma_pro:
		settings.audio_bitrate_kbps = std::clamp(settings.audio_bitrate_kbps, 32U, 768U);
		if (rate != 0 && rate != 44100 && rate != 48000 && rate != 88200 && rate != 96000)
			rate = 48000;
		settings.audio_bits = settings.audio_bits > 16 ? 24 : 16;
		break;
	default:
		if (settings.audio_codec != audio_codec::pcm || settings.audio_bits < 32)
			settings.audio_bits = settings.audio_bits > 16 ? 24 : 16;
		break;
	}
}

// Indexed by simple_player_video_rate_control.
constexpr const char* rate_control_names[] = {"CBR", "VBR, peak constrained", "VBR, unconstrained", "Quality",
	"Low-delay VBR", "Global VBR", "Global low-delay VBR", "Constant QP"};
}

struct video_export_panel::impl
{
	playback_session& playback;
	native_dialogs dialogs;
	simple_player_video_settings settings;
	std::jthread worker;
	std::atomic_bool running{false};
	std::atomic_bool cancel_requested{false};
	bool closed = false;
	mutable std::mutex mutex;
	video_export_snapshot state;
	std::vector<std::uint8_t> pixels;
	std::uint32_t frame_width = 0;
	std::uint32_t frame_height = 0;
	std::uint64_t frame_serial = 0;
	GLuint texture = 0;
	std::uint64_t uploaded_serial = 0;
	std::uint32_t texture_width = 0;
	std::uint32_t texture_height = 0;
	// Installed encoders, enumerated once in the background when the panel
	// first draws; until then every codec is offered.
	encoder_list encoders;
	bool encoders_ready = false;
	std::jthread encoder_probe;
	bool reveal_advanced = false;

	impl(playback_session& service, native_dialogs native) : playback(service), dialogs(std::move(native))
	{
		state.status = "Ready";
	}

	void probe_encoders()
	{
		if (encoder_probe.joinable() || !simple_player_video_export_available())
			return;
		encoder_probe = std::jthread([this]()
		{
			auto found = simple_player_video_encoders();
			std::lock_guard lock(mutex);
			encoders = std::move(found);
			encoders_ready = true;
		});
	}

	// The probe result, or null while it runs.
	std::optional<encoder_list> known_encoders() const
	{
		std::lock_guard lock(mutex);
		return encoders_ready ? std::optional<encoder_list>(encoders) : std::nullopt;
	}

	// The settings check plus what the installed encoders cannot do; the
	// render would otherwise report it only after the save dialog.
	std::string problem(const syncore_preferences& synth) const
	{
		auto result = settings_problem(settings, synth);
		const auto found = known_encoders();
		if (!result.empty() || !found)
			return result;
		const std::string codec = simple_player_video_codec_name(settings.codec);
		if (!codec_available(*found, settings.codec, settings.hardware_encoder))
			return "No usable " + codec + " encoder is installed" +
				(settings.hardware_encoder ? std::string() : "; GPU encoders are off");
		if (!input_available(*found, settings, settings.chroma, settings.bit_depth))
			return "No installed " + codec + " encoder takes this chroma format and bit depth";
		return {};
	}

	static bool progress_callback(const simple_player_video_progress& progress, void* user) noexcept
	{
		auto& self = *static_cast<impl*>(user);
		try
		{
			std::lock_guard lock(self.mutex);
			self.state.progress = progress;
			self.state.progress.preview_bgra = nullptr;
			self.state.status = progress.stage;
			if (progress.preview_bgra && progress.preview_width && progress.preview_height &&
				progress.preview_stride >= static_cast<std::size_t>(progress.preview_width) * 4)
			{
				const auto row_bytes = static_cast<std::size_t>(progress.preview_width) * 4;
				self.pixels.resize(row_bytes * progress.preview_height);
				for (std::uint32_t y = 0; y < progress.preview_height; ++y)
					std::memcpy(self.pixels.data() + row_bytes * y,
						progress.preview_bgra + static_cast<std::size_t>(progress.preview_stride) * y, row_bytes);
				self.frame_width = progress.preview_width;
				self.frame_height = progress.preview_height;
				++self.frame_serial;
			}
			return !self.cancel_requested.load(std::memory_order_acquire);
		}
		catch (...)
		{
			self.cancel_requested.store(true, std::memory_order_release);
			return false;
		}
	}

	void upload_preview()
	{
		std::vector<std::uint8_t> frame;
		std::uint32_t width{}, height{};
		std::uint64_t serial{};
		{
			std::lock_guard lock(mutex);
			if (frame_serial == uploaded_serial)
				return;
			frame = pixels;
			width = frame_width;
			height = frame_height;
			serial = frame_serial;
		}
		if (frame.empty())
		{
			if (texture)
				glDeleteTextures(1, &texture);
			texture = 0;
			texture_width = texture_height = 0;
			uploaded_serial = serial;
			return;
		}
		GLint previous_texture{}, alignment{}, row_length{};
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
		if (!texture)
			glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
		glBindTexture(GL_TEXTURE_2D, previous_texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
		texture_width = width;
		texture_height = height;
		uploaded_serial = serial;
	}
	void draw_preview(const video_export_snapshot& current)
	{
		upload_preview();
		const auto path = current.busy ? current.source_path : playback.current_path();
		const bool has_source = current.busy || !path.empty() || playback.current_source();
		ImGui::TextWrapped("Source: %s", has_source ? display_path(path).c_str() : "none");
		if (texture && texture_width && texture_height)
		{
			// The preview is at most 320 px wide; never upscale it past the UI scale.
			const float scale = std::max(0.f, std::min(ImGui::GetContentRegionAvail().x / texture_width, ui_scale()));
			ImGui::Image(static_cast<ImTextureID>(texture), {texture_width * scale, texture_height * scale});
		}
		else
		{
			ImGui::BeginChild("Preview", {0, scaled(180)}, ImGuiChildFlags_Borders);
			ImGui::TextDisabled("Rendered frames appear here");
			ImGui::EndChild();
		}
	}

	void draw_progress(const video_export_snapshot& current)
	{
		ImGui::TextWrapped("%s", current.cancelling ? "Cancelling..." : current.status.c_str());
		ImGui::ProgressBar(
			fraction(current.progress.completed_frames, current.progress.total_frames), {-1, 0}, "Video");
		ImGui::ProgressBar(
			fraction(current.progress.completed_audio_frames, current.progress.total_audio_frames), {-1, 0}, "Audio");
		if (current.progress.total_frames)
			ImGui::Text("Frames %llu / %llu  |  Events %llu  |  Voices %llu", current.progress.completed_frames,
				current.progress.total_frames, current.progress.completed_events, current.progress.active_voices);
		if (!current.result.video_encoder.empty())
			ImGui::TextWrapped("Encoder: %s", current.result.video_encoder.c_str());
		if (!current.result.error.empty())
			ImGui::TextWrapped("%s", current.result.error.c_str());
		if (!current.result.warning.empty())
			ImGui::TextWrapped("%s", current.result.warning.c_str());
		if (!current.output_path.empty())
			ImGui::TextWrapped("Output: %s", display_path(current.output_path).c_str());
	}

	// Why a render cannot start from the current player source, or empty.
	static std::string source_problem(const std::shared_ptr<playback_event_source>& source,
		const playback_session::source_factory& factory, const std::wstring& path, const playback_snapshot& playback)
	{
		if (!simple_player_video_export_available())
			return "This build does not include SYNCore video export";
		if (source && !factory)
			return "This event source cannot create independent export readers; save it as MIDI first";
		if (!source && path.empty())
			return "No source - open a MIDI in the player or play from the editor";
		if (playback.busy && !playback.playing && !factory)
			return "Wait for source preparation before exporting";
		// A failed or cancelled archive leaves only its path; it is not a MIDI.
		if (!source && !factory && !midi_extension(path))
			return "Prepare the archive in the player first.";
		return {};
	}

	void draw_settings()
	{
		probe_encoders();
		const auto found = known_encoders();
		// Labels get rows of their own. A table carries the text baseline of a
		// cell's last line into the next cell of the row, so a right-hand label
		// would drop to its left neighbour's field text and shift the column down.
		const auto labels = [](const char* left, const char* right)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(left);
			if (right)
			{
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(right);
			}
			ImGui::TableNextRow();
		};
		const auto integer = [](const char* id, std::uint32_t& value)
		{
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			ImGui::InputScalar(id, ImGuiDataType_U32, &value);
		};
		// Only these values, with an optional caption for a special one.
		const auto choice = [](const char* id, std::uint32_t& value, std::initializer_list<std::uint32_t> options,
			std::uint32_t special = UINT32_MAX, const char* special_text = nullptr)
		{
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			const auto text = [&](std::uint32_t option)
			{
				return option == special && special_text ? std::string(special_text) : std::to_string(option);
			};
			if (ImGui::BeginCombo(id, text(value).c_str()))
			{
				for (const auto option : options)
				{
					if (ImGui::Selectable(text(option).c_str(), option == value))
						value = option;
					if (option == value)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		};
		// A combo over an enum's values; `enabled` greys out what cannot work.
		const auto options = [](const char* id, auto& value, std::initializer_list<const char*> names,
			const auto& enabled)
		{
			using value_type = std::remove_reference_t<decltype(value)>;
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			const auto current = static_cast<std::size_t>(value);
			const auto* preview = current < names.size() ? names.begin()[current] : "?";
			bool changed = false;
			if (ImGui::BeginCombo(id, preview))
			{
				for (std::size_t index = 0; index < names.size(); ++index)
				{
					const bool usable = enabled(index);
					if (ImGui::Selectable(names.begin()[index], index == current,
						usable ? 0 : ImGuiSelectableFlags_Disabled))
					{
						value = static_cast<value_type>(index);
						changed = true;
					}
					if (index == current)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			return changed;
		};
		const auto always = [](std::size_t) { return true; };
		// Clamp only once editing ends, so typing is not rewritten mid-value.
		const auto seconds = [](const char* id, double& value, double step, double fast, double minimum)
		{
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			ImGui::InputDouble(id, &value, step, fast, "%.3f");
			if (!ImGui::IsItemActive())
				value = std::isfinite(value) ? std::clamp(value, minimum, 60.0) : minimum;
		};
		// An encoder property: unchecked leaves the encoder's default.
		const auto optional = [](const char* id, std::int32_t& value, int minimum, int maximum, int fallback)
		{
			ImGui::TableNextColumn();
			ImGui::PushID(id);
			bool set = value != simple_player_video_encoder_default;
			if (ImGui::Checkbox("##set", &set))
				value = set ? fallback : simple_player_video_encoder_default;
			ImGui::SetItemTooltip("Unchecked keeps the encoder's default");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			ImGui::BeginDisabled(!set);
			int shown = set ? value : fallback;
			if (ImGui::InputInt("##value", &shown) && set)
				value = shown;
			if (set && !ImGui::IsItemActive())
				value = std::clamp(value, minimum, maximum);
			ImGui::EndDisabled();
			ImGui::PopID();
		};
		const auto toggle = [&](const char* id, std::int32_t& value)
		{
			// -1 (encoder default), 0 (off), 1 (on) shown as Default, Off, On.
			int index = value + 1;
			if (options(id, index, {"Default", "Off", "On"}, always))
				value = index - 1;
		};

		auto& s = settings;
		const bool asf = asf_codec(s.codec);
		const bool h264 = s.codec == video_codec::h264;
		const std::uint32_t qp_limit = s.codec == video_codec::av1 ? 255 : 51;
		const auto mode = s.rate_control;
		using rate_control = simple_player_video_rate_control;
		const bool bitrate_mode = mode != rate_control::quality && mode != rate_control::constant_qp;

		if (ImGui::BeginTable("Render settings", 2, ImGuiTableFlags_SizingStretchSame))
		{
			labels("Width", "Height");
			integer("##width", s.width);
			integer("##height", s.height);
			labels("FPS", "Codec");
			integer("##fps", s.fps);
			{
				std::vector<std::string> names;
				for (const auto codec : {video_codec::h264, video_codec::hevc, video_codec::av1,
					video_codec::wmv9, video_codec::vc1})
				{
					std::string name = std::string(simple_player_video_codec_name(codec)) +
						(asf_codec(codec) ? " (WMV)" : " (MP4)");
					if (found && !codec_available(*found, codec, s.hardware_encoder))
					{
						const bool unwritable = std::any_of(found->begin(), found->end(), [&](const auto& encoder)
							{ return encoder.codec == codec && !encoder.writable; });
						name += unwritable ? " - Windows cannot store it" : " - no encoder";
					}
					names.push_back(std::move(name));
				}
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				if (ImGui::BeginCombo("##codec", names[static_cast<std::size_t>(s.codec)].c_str()))
				{
					for (std::size_t index = 0; index < names.size(); ++index)
					{
						const auto codec = static_cast<video_codec>(index);
						const bool usable = !found || codec_available(*found, codec, s.hardware_encoder);
						if (ImGui::Selectable(names[index].c_str(), codec == s.codec,
							usable ? 0 : ImGuiSelectableFlags_Disabled))
						{
							s.codec = codec;
							fit_to_codec(s);
						}
					}
					ImGui::EndCombo();
				}
			}
			labels("Rate control", bitrate_mode ? "Video kbps" : mode == rate_control::quality ? "Quality (0-100)" : "QP");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			if (ImGui::BeginCombo("##rate_control", rate_control_names[static_cast<std::size_t>(mode)]))
			{
				for (std::size_t index = 0; index < std::size(rate_control_names); ++index)
					if (ImGui::Selectable(rate_control_names[index], index == static_cast<std::size_t>(mode)))
						s.rate_control = static_cast<rate_control>(index);
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("CBR and VBR aim at a bitrate. Quality and constant QP aim at a picture quality and\n"
				"let the file size follow; they keep flat backgrounds clean at a far lower average rate.");
			if (bitrate_mode)
				integer("##video_kbps", s.video_bitrate_kbps);
			else if (mode == rate_control::quality)
			{
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				int quality = static_cast<int>(s.quality);
				if (ImGui::SliderInt("##quality", &quality, 0, 100))
					s.quality = static_cast<std::uint32_t>(quality);
			}
			else
			{
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1);
				int qp = static_cast<int>(s.qp);
				if (ImGui::SliderInt("##qp", &qp, 0, static_cast<int>(qp_limit)))
					s.qp = static_cast<std::uint32_t>(qp);
				ImGui::SetItemTooltip("Lower is better and larger");
			}
			if (mode == rate_control::peak_constrained_vbr)
			{
				labels("Peak kbps (0 = encoder default)", nullptr);
				integer("##peak_kbps", s.peak_bitrate_kbps);
			}

			const bool lossy_audio = s.audio_codec == audio_codec::aac || s.audio_codec == audio_codec::wma ||
				s.audio_codec == audio_codec::wma_pro;
			labels("Audio codec", lossy_audio ? "Audio kbps" : "Audio bits");
			if (options("##audio_codec", s.audio_codec, {"AAC", "ALAC (lossless)", "FLAC (lossless)", "WMA",
				"WMA Pro", "PCM (uncompressed)"}, [&](std::size_t index)
				{
					return simple_player_video_audio_supported(s.codec, static_cast<audio_codec>(index));
				}))
				fit_to_codec(s);
			if (s.audio_codec == audio_codec::aac)
				choice("##audio_kbps", s.audio_bitrate_kbps, {96, 128, 160, 192});
			else if (lossy_audio)
			{
				integer("##audio_kbps", s.audio_bitrate_kbps);
				ImGui::SetItemTooltip("The encoder's closest supported rate is used");
			}
			else if (s.audio_codec == audio_codec::pcm)
				choice("##audio_bits", s.audio_bits, {16, 24, 32}, 32, "32 (float)");
			else
				choice("##audio_bits", s.audio_bits, {16, 24});
			labels("Audio Hz", "Background");
			{
				const auto synth_rate = playback.synth_preferences().sample_rate;
				const auto same = "Same as SYNCore (" + std::to_string(synth_rate) + ")";
				if (s.audio_codec == audio_codec::aac || s.audio_codec == audio_codec::wma)
					choice("##audio_hz", s.audio_sample_rate, {0, 44100, 48000}, 0, same.c_str());
				else if (s.audio_codec == audio_codec::wma_pro)
					choice("##audio_hz", s.audio_sample_rate, {0, 44100, 48000, 88200, 96000}, 0, same.c_str());
				else
					choice("##audio_hz", s.audio_sample_rate, {0, 32000, 44100, 48000, 88200, 96000, 176400, 192000},
						0, same.c_str());
				ImGui::SetItemTooltip("A rate other than SYNCore's is resampled by Windows");
			}
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			float background[3] = {static_cast<float>((s.background_rgb >> 16) & 0xFF) / 255.f,
				static_cast<float>((s.background_rgb >> 8) & 0xFF) / 255.f,
				static_cast<float>(s.background_rgb & 0xFF) / 255.f};
			if (ImGui::ColorEdit3("##background", background, ImGuiColorEditFlags_DisplayHex))
			{
				const auto channel = [](float value)
				{
					return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
				};
				s.background_rgb = channel(background[0]) << 16 | channel(background[1]) << 8 | channel(background[2]);
			}
			ImGui::SetItemTooltip("A flat colour the notes never use is easy to key out");
			labels("Visible seconds", "Tail seconds");
			seconds("##visible_seconds", s.visible_seconds, .01, .1, .01);
			seconds("##tail_seconds", s.tail_seconds, .5, 1, 0);
			labels("Overlaps", nullptr);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			int overlap = s.remove_overlaps <= 1 ? s.remove_overlaps : 2;
			if (ImGui::Combo("##overlaps", &overlap, "Naive removal\0Realtime removal\0Draw all\0"))
				s.remove_overlaps = overlap == 2 ? 0xff : static_cast<std::uint8_t>(overlap);
			ImGui::EndTable();
		}

		if (reveal_advanced)
			ImGui::SetNextItemOpen(true);
		const bool advanced = ImGui::CollapsingHeader("Advanced encoder options");
		if (reveal_advanced)
			ImGui::SetScrollHereY(0.f);
		reveal_advanced = false;
		if (!advanced)
			return;
		if (found)
		{
			std::string installed;
			for (const auto& encoder : *found)
				if (encoder.codec == s.codec)
					installed += (installed.empty() ? "" : ", ") + encoder.name + (encoder.hardware ? " (GPU)" : "");
			ImGui::TextWrapped("%s encoders: %s", simple_player_video_codec_name(s.codec),
				installed.empty() ? "none" : installed.c_str());
		}
		else
			ImGui::TextDisabled("Looking for installed encoders...");
		ImGui::TextWrapped("Unchecked values keep the encoder's default. After a render, the status names the "
			"encoder and any option it ignored.");
		if (!ImGui::BeginTable("Advanced settings", 2, ImGuiTableFlags_SizingStretchSame))
			return;
		labels("GPU encoders", h264 ? "H.264 profile" : nullptr);
		ImGui::TableNextColumn();
		if (ImGui::Checkbox("Use when available##hardware", &s.hardware_encoder))
			fit_to_codec(s);
		ImGui::SetItemTooltip("Off uses the software encoders only; HEVC and AV1 usually have none");
		if (h264)
			options("##h264_profile", s.h264_profile, {"Encoder default", "Baseline", "Main", "High"}, always);

		labels("Chroma", "Bit depth");
		const auto input_ok = [&](simple_player_video_chroma chroma, std::uint32_t depth)
		{
			const bool codec_ok = (s.codec == video_codec::hevc || chroma == simple_player_video_chroma::yuv420) &&
				(depth == 8 || s.codec == video_codec::hevc || s.codec == video_codec::av1);
			return codec_ok && (!found || input_available(*found, s, chroma, depth));
		};
		options("##chroma", s.chroma, {"4:2:0", "4:2:2", "4:4:4 (no chroma loss)"}, [&](std::size_t index)
			{
				return input_ok(static_cast<simple_player_video_chroma>(index), s.bit_depth);
			});
		ImGui::SetItemTooltip("4:2:0 stores colour at half resolution, which blurs colour at note edges.\n"
			"4:4:4 keeps full colour resolution; HEVC on a capable GPU encodes it.");
		{
			int depth = s.bit_depth > 8 ? 1 : 0;
			if (options("##bit_depth", depth, {"8-bit", "10-bit"}, [&](std::size_t index)
				{
					return input_ok(s.chroma, index ? 10U : 8U);
				}))
				s.bit_depth = depth ? 10 : 8;
		}
		labels("Colour matrix", "Range");
		options("##matrix", s.matrix, {"BT.601", "BT.709", "BT.2020"}, always);
		{
			int full = s.full_range ? 1 : 0;
			if (options("##range", full, {"Limited (16-235)", "Full (0-255)"}, always))
				s.full_range = full != 0;
			ImGui::SetItemTooltip("Full range is refused before rendering when the encoder cannot mark it");
		}
		labels("Chroma filter", "Speed/quality preset");
		ImGui::BeginDisabled(s.chroma == simple_player_video_chroma::yuv444);
		options("##chroma_filter", s.chroma_filter, {"Box average", "Co-sited [1 2 1]", "Nearest"}, always);
		ImGui::EndDisabled();
		optional("##speed", s.quality_vs_speed, 0, 100, 100);
		ImGui::SetItemTooltip("0 is fastest, 100 is best");
		labels("Keyframe interval (frames)", "B-frames");
		optional("##gop", s.gop_size, 1, 1000000, static_cast<int>(s.fps) * 2);
		optional("##b_frames", s.b_frames, 0, 16, 2);
		labels("Reference frames", "Encoder threads (software)");
		optional("##references", s.max_reference_frames, 1, 16, 4);
		optional("##threads", s.worker_threads, 1, 64, 4);
		labels("Minimum QP", "Maximum QP");
		ImGui::BeginDisabled(!bitrate_mode);
		optional("##min_qp", s.min_qp, 0, static_cast<int>(qp_limit), 10);
		optional("##max_qp", s.max_qp, 0, static_cast<int>(qp_limit), 40);
		ImGui::EndDisabled();
		labels("VBV buffer kbits (0 = default)", mode == rate_control::constant_qp ? "P / B-frame QP offset" : nullptr);
		integer("##buffer", s.buffer_kbits);
		if (mode == rate_control::constant_qp)
		{
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1);
			int offsets[2] = {s.qp_p_offset, s.qp_b_offset};
			if (ImGui::InputInt2("##qp_offsets", offsets))
			{
				s.qp_p_offset = std::clamp(offsets[0], -static_cast<int>(qp_limit), static_cast<int>(qp_limit));
				s.qp_b_offset = std::clamp(offsets[1], -static_cast<int>(qp_limit), static_cast<int>(qp_limit));
			}
		}
		labels(h264 ? "CABAC" : "Low latency", h264 ? "Low latency" : nullptr);
		if (h264)
			toggle("##cabac", s.cabac);
		toggle("##low_latency", s.low_latency);
		labels("MP4 fast start", s.audio_codec == audio_codec::wma_pro ? "WMA Pro bits" : "Audio dither");
		ImGui::TableNextColumn();
		ImGui::BeginDisabled(asf);
		ImGui::Checkbox("Index first##fast_start", &s.fast_start);
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Puts the MP4 index before the media, so playback can start while downloading");
		if (s.audio_codec == audio_codec::wma_pro)
			choice("##wma_pro_bits", s.audio_bits, {16, 24});
		else
		{
			ImGui::TableNextColumn();
			ImGui::Checkbox("TPDF to 16-bit##dither", &s.audio_dither);
			ImGui::SetItemTooltip("Dither when SAFC reduces SYNCore's float output to 16-bit samples");
		}
		ImGui::EndTable();
	}
};

video_export_panel::video_export_panel(playback_session& playback, native_dialogs dialogs)
	: impl_(std::make_unique<impl>(playback, std::move(dialogs)))
{
}
video_export_panel::~video_export_panel()
{
	shutdown();
}

simple_player_video_settings video_export_panel::settings() const
{
	return impl_->settings;
}
void video_export_panel::set_settings(const simple_player_video_settings& value)
{
	if (!impl_->running.load(std::memory_order_acquire))
		impl_->settings = value;
}

void video_export_panel::reveal_advanced_options()
{
	impl_->reveal_advanced = true;
}

video_export_snapshot video_export_panel::snapshot() const
{
	std::lock_guard lock(impl_->mutex);
	auto result = impl_->state;
	result.busy = impl_->running.load(std::memory_order_acquire);
	result.cancelling = result.busy && impl_->cancel_requested.load(std::memory_order_acquire);
	return result;
}

bool video_export_panel::start_export(std::wstring output_path)
{
	if (impl_->closed || output_path.empty() || impl_->running.load(std::memory_order_acquire))
		return false;
	if (impl_->worker.joinable())
		impl_->worker.join();
	const auto source = impl_->playback.current_source();
	auto factory = impl_->playback.export_source_factory();
	auto source_path = impl_->playback.current_path();
	const auto playback = impl_->playback.snapshot();
	auto unavailable = impl_->source_problem(source, factory, source_path, playback);
	if (unavailable.empty())
		unavailable = impl_->problem(impl_->playback.synth_preferences());
	if (!unavailable.empty())
	{
		std::lock_guard lock(impl_->mutex);
		impl_->state.status = std::move(unavailable);
		return false;
	}
	// Prepared editor readers are virtual; the renderer uses this descriptor
	// only for source/output collision checks and never reads it from disk.
	if (source_path.empty())
		source_path = L"Unsaved editor.mid";
	const auto bank = impl_->playback.bank_path();
	const auto synth = impl_->playback.synth_preferences();
	const auto settings = impl_->settings;
	{
		std::lock_guard lock(impl_->mutex);
		impl_->state = {};
		impl_->state.status = "Starting render...";
		impl_->state.source_path = source_path;
		impl_->state.output_path = output_path;
		impl_->pixels.clear();
		++impl_->frame_serial;
	}
	impl_->cancel_requested.store(false, std::memory_order_release);
	impl_->running.store(true, std::memory_order_release);
	try
	{
		impl_->worker = std::jthread(
			[self = impl_.get(), output_path = std::move(output_path), source_path = std::move(source_path),
				factory = std::move(factory), bank, synth, settings, total_events = playback.source_events]() mutable
		{
			simple_player_video_result result;
			try
			{
				if (factory)
				{
					auto audio = factory();
					auto video = factory();
					if (!audio || !video || audio.get() == video.get())
						throw std::runtime_error("Export requires two independent event readers");
					result = render_simple_player_video_events(source_path, *audio, *video, total_events, output_path,
						bank, synth, settings, &self->cancel_requested, &impl::progress_callback, self);
				}
				else
					result = render_simple_player_video(source_path, output_path, bank, synth, settings,
						&self->cancel_requested, &impl::progress_callback, self);
			}
			catch (const std::exception& error)
			{
				result.error = error.what();
			}
			catch (...)
			{
				result.error = "Unexpected export failure";
			}
			{
				std::lock_guard lock(self->mutex);
				self->state.result = result;
				if (result.ok)
					self->state.status = "Video saved";
				else if (result.cancelled)
					self->state.status = "Render cancelled";
				else
					self->state.status = "Render failed";
			}
			self->running.store(false, std::memory_order_release);
		});
	}
	catch (const std::exception& error)
	{
		impl_->running.store(false, std::memory_order_release);
		std::lock_guard lock(impl_->mutex);
		impl_->state.status = "Could not start render";
		impl_->state.result.error = error.what();
		return false;
	}
	return true;
}

void video_export_panel::cancel()
{
	impl_->cancel_requested.store(true, std::memory_order_release);
}

void video_export_panel::shutdown()
{
	if (!impl_ || impl_->closed)
		return;
	impl_->closed = true;
	cancel();
	if (impl_->worker.joinable())
		impl_->worker.join();
	if (impl_->texture)
		glDeleteTextures(1, &impl_->texture);
	impl_->texture = 0;
}

void video_export_panel::draw(bool* open)
{
	if (impl_->closed || (open && !*open))
		return;
	ImGui::SetNextWindowPos({scaled(90), scaled(86)}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(
		{scaled(600), std::max(scaled(420), std::min(scaled(730), ImGui::GetIO().DisplaySize.y - scaled(116)))},
		ImGuiCond_FirstUseEver);
	if (begin_folded_window("Player video render", open))
	{
		const auto current = snapshot();

		impl_->draw_preview(current);
		impl_->draw_progress(current);
		ImGui::Separator();
		ImGui::BeginDisabled(current.busy);
		impl_->draw_settings();
		ImGui::TextWrapped("Uses applied SYNCore settings. Audio and video render independently from playback.");
		// Validate before the save dialog, so it is never shown for a render that cannot start.
		std::string problem;
		if (!current.busy)
		{
			problem = impl_->source_problem(impl_->playback.current_source(),
				impl_->playback.export_source_factory(), impl_->playback.current_path(), impl_->playback.snapshot());
			if (problem.empty())
				problem = impl_->problem(impl_->playback.synth_preferences());
		}
		ImGui::BeginDisabled(!problem.empty());
		if (ImGui::Button("Render video", {scaled(150), 0}) && impl_->dialogs.save_video)
		{
			auto suggestion = std::filesystem::path(impl_->playback.current_path());
			if (suggestion.empty())
				suggestion = "Editor.mp4";
			suggestion.replace_extension(simple_player_video_extension(impl_->settings.codec));
			try
			{
				auto destination = impl_->dialogs.save_video(suggestion.wstring());
				if (!destination.empty())
					start_export(std::move(destination));
			}
			catch (const std::exception& error)
			{
				std::lock_guard lock(impl_->mutex);
				impl_->state.status = error.what();
			}
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!current.busy || current.cancelling);
		if (ImGui::Button("Cancel render"))
			cancel();
		ImGui::EndDisabled();
		if (!problem.empty())
		{
			ImGui::PushTextWrapPos(0);
			ImGui::TextDisabled("%s", problem.c_str());
			ImGui::PopTextWrapPos();
		}
	}
	end_folded_window();
}
}
