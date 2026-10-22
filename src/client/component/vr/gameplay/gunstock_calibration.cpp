#include <std_include.hpp>
#include "gunstock_calibration.hpp"
#include "gunstock_prompt.hpp"
#include "gunstock.hpp"
#include "../controller_haptics.hpp"
#include "../head_pose_bridge.hpp"
#include "../overlay_text_texture.hpp"
#include "../spatial_panel_renderer.hpp"
#include "../settings.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/string.hpp>

namespace vr::gameplay::weapons::gunstock
{
	namespace
	{
		using clock = controller_input::clock;
		game::dvar_t* mode{};
		std::array<game::dvar_t*, 3> offset{}, shift{};

		configuration native_settings() noexcept
		{
			configuration result{};
			result.enabled = mode && mode->current.enabled;
			for (std::size_t i = 0; i < offset.size(); ++i)
			{
				if (offset[i])
					result.offset[i] = offset[i]->current.value;
				if (shift[i])
					result.shift[i] = shift[i]->current.value;
			}
			if (!valid(result.offset))
				result.offset = {};
			return result;
		}

		// Head-locked prompt text. The native message feeds are not drawn in the
		// headset, so the eye composition draws this panel itself.
		std::mutex prompt_mutex;
		std::string prompt_text;
		clock::time_point prompt_until{};

		void notify(std::string text, std::chrono::seconds duration = std::chrono::seconds(6))
		{
			{
				const std::lock_guard lock(prompt_mutex);
				prompt_text = text;
				prompt_until = clock::now() + duration;
			}
			scheduler::once([text = std::move(text)] { console::info("[VR gunstock] %s\n", text.data()); },
			                scheduler::pipeline::main);
		}

		// Greedy word wrap; each line stays well inside the text texture limit.
		std::vector<std::string> wrap(const std::string& text, std::size_t width, std::size_t max_lines)
		{
			std::vector<std::string> lines;
			std::string line;
			std::size_t start = 0;
			while (start < text.size() && lines.size() < max_lines)
			{
				auto end = text.find(' ', start);
				if (end == std::string::npos)
					end = text.size();
				const auto word = text.substr(start, std::min(end - start, width));
				if (!line.empty() && line.size() + 1 + word.size() > width)
				{
					lines.push_back(line);
					line.clear();
				}
				line += line.empty() ? word : " " + word;
				start = end + 1;
			}
			if (!line.empty() && lines.size() < max_lines)
				lines.push_back(line);
			return lines;
		}
		void pulse(const controller_input::frame& input, int hand, float seconds, float amplitude) noexcept
		{
			controller_haptics::request(hand, {seconds, 160.f, amplitude, input.reference_generation, input.sampled_at});
		}

		struct request
		{
			bool armed{}, baseline{}, countdown{};
			clock::time_point started{}, deadline{};
			std::uint64_t reference{};
			std::array<std::uint64_t, 2> generation{}, presses{};
		};
		std::mutex request_mutex;
		request pending{};
		std::array<float, 3> last_capture{};
		// Last attempt's geometry in degrees: rear aim vs gaze, stock line vs gaze, line vs rear aim.
		struct diagnostic
		{
			bool valid{};
			angles measured{};
			std::array<float, 3> spread{};
			int rear{-1};
		} last_attempt{};
		std::atomic_uint64_t captures{}, rejections{};

		constexpr auto trigger_timeout = std::chrono::seconds(60);

		void arm(float seconds)
		{
			const std::lock_guard lock(request_mutex);
			pending = {};
			pending.armed = true;
			pending.started = clock::now();
			pending.countdown = seconds > 0;
			pending.deadline = pending.started + (pending.countdown
			                                          ? std::chrono::duration_cast<clock::duration>(
			                                                std::chrono::duration<float>(seconds))
			                                          : clock::duration(trigger_timeout));
		}

		float round_tenth(float value) noexcept
		{
			return std::round(value * 10.f) / 10.f;
		}

		// Called with request_mutex held. Returns true when the request is finished.
		bool try_capture(const controller_input::frame& input,
		                 const std::array<spatial_math::anchor, 2>& targets,
		                 int front,
		                 spatial_math::vec head_forward)
		{
			const int rear = 1 - front;
			const auto aim = spatial_math::rotate(targets[rear].rotation, {1, 0, 0});
			const auto line = spatial_math::sub(targets[front].position, targets[rear].position);
			const auto measured = measure(targets[rear].rotation, head_forward, rear == 0);
			last_attempt = {measured.has_value(),
			                measured.value_or(angles{}),
			                {angle_between(aim, head_forward), angle_between(line, head_forward), angle_between(line, aim)},
			                rear};
			console::info("[VR gunstock] capture rear=%s measured=%.1f/%.1f/%.1f aim_vs_gaze=%.1f "
			              "line_vs_gaze=%.1f line_vs_aim=%.1f\n",
			              rear == 0 ? "left" : "right", last_attempt.measured[0], last_attempt.measured[1],
			              last_attempt.measured[2], last_attempt.spread[0], last_attempt.spread[1],
			              last_attempt.spread[2]);
			const auto result = capture(targets[rear].rotation, head_forward, rear == 0);
			if (!result)
			{
				rejections.fetch_add(1, std::memory_order_relaxed);
				pulse(input, front, .03f, .3f);
				const float needed = std::max({std::abs(last_attempt.measured[0]), std::abs(last_attempt.measured[1]),
				                               std::abs(last_attempt.measured[2])});
				notify(utils::string::va("Gunstock calibration rejected (needs %.0f deg, limit %.0f). Look along the "
				                         "stock at a distant point and try again",
				                         needed, max_offset_degrees));
				return false;
			}
			const std::array<float, 3> values{
			    round_tenth((*result)[0]), round_tenth((*result)[1]), round_tenth((*result)[2])};
			last_capture = values;
			captures.fetch_add(1, std::memory_order_relaxed);
			const auto& names = settings::gunstock_offset;
			std::string assignment = utils::string::va("seta %s %.1f; seta %s %.1f; seta %s %.1f",
			                                           names[0].name, values[0],
			                                           names[1].name, values[1],
			                                           names[2].name, values[2]);
			scheduler::once([assignment = std::move(assignment)] { command::execute(assignment); },
			                scheduler::pipeline::main);
			for (int h = 0; h < 2; ++h)
				pulse(input, h, .1f, .8f);
			notify(utils::string::va("Gunstock calibrated (%s shoulder): pitch %.1f, yaw %.1f, roll %.1f%s",
			                         rear == 0 ? "left" : "right", values[0], values[1], values[2],
			                         mode && mode->current.enabled ? "" : ". Enable vr_gunstockMode to use it"));
			return true;
		}
	}

	void observe(const controller_input::frame& input,
	             const std::array<spatial_math::anchor, 2>& targets,
	             std::array<bool, 2> tracked,
	             spatial_math::vec head_position,
	             spatial_math::vec head_forward,
	             float units_per_meter) noexcept
	{
		const std::lock_guard lock(request_mutex);
		if (!pending.armed)
			return;
		const auto now = clock::now();
		if (!pending.countdown && now > pending.deadline)
		{
			pending = {};
			notify("Gunstock calibration timed out");
			return;
		}
		if (!input.focused || !tracked[0] || !tracked[1] || !std::isfinite(units_per_meter) ||
		    units_per_meter <= 0)
			return;
		// Recentering or input loss restarts the trigger baseline; an old press never captures.
		if (!pending.baseline || pending.reference != input.reference_generation)
		{
			if (!pending.baseline)
				for (int h = 0; h < 2; ++h)
					pulse(input, h, .03f, .4f); // Armed: one light tick on both hands.
			pending.baseline = true;
			pending.reference = input.reference_generation;
			for (int h = 0; h < 2; ++h)
			{
				pending.generation[h] = input.trigger[h].generation;
				pending.presses[h] = input.trigger[h].presses;
			}
			return;
		}
		const auto head_distance = [&](int h)
		{ return spatial_math::length(spatial_math::sub(targets[h].position, head_position)); };
		if (pending.countdown)
		{
			if (now < pending.deadline)
				return;
			// The front controller is the one farther from the face.
			const int front = head_distance(0) > head_distance(1) ? 0 : 1;
			if (try_capture(input, targets, front, head_forward))
				pending = {};
			else
				pending.deadline = now + std::chrono::seconds(3);
			return;
		}
		for (int h = 0; h < 2; ++h)
		{
			const auto& trigger = input.trigger[h];
			if (trigger.generation != pending.generation[h])
			{
				pending.generation[h] = trigger.generation;
				pending.presses[h] = trigger.presses;
				continue;
			}
			if (trigger.presses == pending.presses[h])
				continue;
			pending.presses[h] = trigger.presses;
			if (head_distance(h) <= head_distance(1 - h))
			{
				notify("Gunstock calibration: pull the FRONT hand trigger, not the rear one");
				continue;
			}
			if (try_capture(input, targets, h, head_forward))
			{
				pending = {};
				return;
			}
		}
	}

	void present_prompt(const eye_composition::event& event,
	                    ID3D11DeviceContext* context,
	                    ID3D11ShaderResourceView* background,
	                    ID3D11RenderTargetView* output) noexcept
	{
		constexpr std::size_t max_lines = 3;
		thread_local std::array<overlay_text_texture, max_lines> lines;
		thread_local spatial_panel::renderer renderer;
		struct prepared_pair
		{
			std::uint64_t pair{}, publication{};
			std::size_t count{};
			std::array<std::array<spatial_panel::projected_quad, 2>, max_lines> corners{};
		};
		thread_local prepared_pair prepared;
		if (event.eye > 1 || !context)
			return;
		if (event.eye == 0)
		{
			prepared = {};
			prepared.pair = event.pair_id;
			prepared.publication = event.views.eyes[0].publication;
			std::string text;
			{
				const std::lock_guard lock(prompt_mutex);
				if (clock::now() < prompt_until)
					text = prompt_text;
			}
			head_pose_bridge::spatial_frame spatial{};
			if (text.empty() || !event.model_origins.valid || !head_pose_bridge::get_spatial_frame(spatial) ||
			    !std::isfinite(spatial.units_per_meter) || spatial.units_per_meter <= 0)
				return;
			const float units = spatial.units_per_meter;
			// The pair's own camera: origin, forward, left, up rows.
			const auto& camera = event.views.natural_camera;
			spatial_panel::vec3 origin{}, forward{}, right{}, up{};
			for (unsigned i = 0; i < 3; ++i)
			{
				origin[i] = camera[i];
				forward[i] = camera[3 + i];
				right[i] = -camera[6 + i];
				up[i] = camera[9 + i];
			}
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			context->GetDevice(&device);
			const auto wrapped = wrap(text, 52, max_lines);
			const float height = .028f * units, gap = .3f * height;
			for (std::size_t n = 0; n < wrapped.size(); ++n)
			{
				if (!lines[n].ensure_utf8(device.Get(), wrapped[n], 40, true) || !lines[n].height())
					return;
				// About 1.2 m ahead and slightly below eye level.
				const float lift = (float(wrapped.size() - 1) * .5f - float(n)) * (height + gap) - .12f * units;
				spatial_panel::vec3 center{};
				for (unsigned i = 0; i < 3; ++i)
					center[i] = origin[i] + forward[i] * 1.2f * units + up[i] * lift;
				spatial_panel::quad quad{};
				if (!spatial_panel::billboard(center, right, up,
				                              height * float(lines[n].width()) / float(lines[n].height()), height, quad))
					return;
				for (unsigned eye = 0; eye < 2; ++eye)
				{
					spatial_panel::matrix matrix{};
					std::memcpy(matrix.data(),
					            event.views.eyes[eye].bytes.data() + engine_stereo_view::h2_current_view_projection_offset,
					            sizeof(matrix));
					if (!spatial_panel::project(quad, event.model_origins.eyes[eye], matrix, units * .12f,
					                            prepared.corners[n][eye]))
						return;
				}
			}
			prepared.count = wrapped.size();
		}
		if (prepared.pair != event.pair_id || prepared.publication != event.views.eyes[0].publication)
			return;
		for (std::size_t n = 0; n < prepared.count; ++n)
			(void)renderer.draw(context, background, lines[n].view(), output, prepared.corners[n][event.eye],
			                    event.width, event.height, 0, 0);
	}

	class component final : public component_interface
	{
		void post_unpack() override
		{
			mode = dvars::register_bool(settings::gunstock_mode.name,
			                            settings::gunstock_mode.default_value,
			                            game::DVAR_FLAG_SAVED,
			                            "Physical gunstock: two-hand weapons keep the rear controller's aim");
			for (std::size_t i = 0; i < offset.size(); ++i)
			{
				const auto& field = settings::gunstock_offset[i];
				offset[i] = dvars::register_float(field.name,
				                                  field.default_value,
				                                  field.min,
				                                  field.max,
				                                  game::DVAR_FLAG_SAVED,
				                                  "Gunstock aim offset in degrees, for a right-shoulder stock");
				const auto& moved = settings::gunstock_shift[i];
				shift[i] = dvars::register_float(moved.name,
				                                 moved.default_value,
				                                 moved.min,
				                                 moved.max,
				                                 game::DVAR_FLAG_SAVED,
				                                 "Gunstock gun position offset in meters (right/up/forward)");
			}
			set_settings_provider(native_settings);
			command::add("vr_gunstockCalibrate",
			             [](const command::params& params)
			             {
				             const float seconds =
				                 params.size() > 1 ? std::clamp(float(std::atof(params.get(1))), 0.f, 30.f) : 0.f;
				             arm(seconds);
				             notify(seconds > 0
				                        ? utils::string::va("Gunstock calibration: shoulder the stock and look along it "
				                                            "at a distant point. Capturing in %.0f seconds",
				                                            seconds)
				                        : "Gunstock calibration: shoulder the stock, look along it at a distant point, "
				                          "then pull the FRONT hand trigger",
				                    seconds > 0 ? std::chrono::seconds(int(seconds) + 1) : trigger_timeout);
			             });
			command::add("vr_gunstockCancel",
			             []
			             {
				             {
					             const std::lock_guard lock(request_mutex);
					             pending = {};
				             }
				             notify("Gunstock calibration cancelled");
			             });
			command::add("vr_gunstockReset",
			             []
			             {
				             const auto& names = settings::gunstock_offset;
				             const auto& moved = settings::gunstock_shift;
				             command::execute(utils::string::va(
				                 "seta %s 0; seta %s 0; seta %s 0; seta %s 0; seta %s 0; seta %s 0", names[0].name,
				                 names[1].name, names[2].name, moved[0].name, moved[1].name, moved[2].name));
				             notify("Gunstock angle and position offsets reset to zero");
			             });
			command::add("vr_gunstock_status",
			             []
			             {
				             const auto config = native_settings();
				             bool armed{};
				             diagnostic attempt{};
				             {
					             const std::lock_guard lock(request_mutex);
					             armed = pending.armed;
					             attempt = last_attempt;
				             }
				             console::info("[VR gunstock] enabled=%d offset=%.1f/%.1f/%.1f shift=%.3f/%.3f/%.3f armed=%d "
				                           "captures=%llu rejections=%llu last=%.1f/%.1f/%.1f\n",
				                           config.enabled, config.offset[0], config.offset[1], config.offset[2],
				                           config.shift[0], config.shift[1], config.shift[2], armed,
				                           captures.load(), rejections.load(), last_capture[0], last_capture[1],
				                           last_capture[2]);
				             if (attempt.valid)
					             console::info("[VR gunstock] last attempt rear=%s measured=%.1f/%.1f/%.1f "
					                           "aim_vs_gaze=%.1f line_vs_gaze=%.1f line_vs_aim=%.1f\n",
					                           attempt.rear == 0 ? "left" : "right", attempt.measured[0],
					                           attempt.measured[1], attempt.measured[2], attempt.spread[0],
					                           attempt.spread[1], attempt.spread[2]);
			             });
		}
	};
} // namespace vr::gameplay::weapons::gunstock
REGISTER_COMPONENT(vr::gameplay::weapons::gunstock::component)
