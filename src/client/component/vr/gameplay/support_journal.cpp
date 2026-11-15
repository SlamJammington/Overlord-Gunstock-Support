#include <std_include.hpp>
#include "support_journal.hpp"
#include "../controller_input.hpp"
#include "component/scheduler.hpp"
#include <utils/io.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>

namespace vr::gameplay::weapons::support_journal
{
	namespace
	{
		constexpr std::size_t capacity = 400;
		std::mutex journal_mutex;
		std::deque<std::string> journal_lines;
		bool write_pending{};
	}
	void note(const char* what, weapon_identity id, int hand, const char* cause, const std::string& detail) noexcept
	{
		try
		{
			const auto input = controller_input::latest_interaction();
			const auto wall = std::chrono::system_clock::now();
			const auto seconds = std::chrono::system_clock::to_time_t(wall);
			std::tm local{};
			localtime_s(&local, &seconds);
			const int milliseconds = int(
			    std::chrono::duration_cast<std::chrono::milliseconds>(wall.time_since_epoch()).count() % 1000);
			char text[768];
			std::snprintf(text, sizeof(text),
			              "%02d:%02d:%02d.%03d %s weapon=%u hand=%s cause=\"%s\" seq=%llu "
			              "grip_down L/R=%d/%d grip_value=%.2f/%.2f trigger_down L/R=%d/%d trigger_value=%.2f/%.2f "
			              "pose_ready L/R=%d/%d %s\n",
			              local.tm_hour, local.tm_min, local.tm_sec, milliseconds, what ? what : "?", id.weapon,
			              hand == 0 ? "left" : hand == 1 ? "right" : "none", cause ? cause : "?",
			              static_cast<unsigned long long>(input.sequence), input.squeeze[0].down, input.squeeze[1].down,
			              double(input.squeeze_analog[0].value), double(input.squeeze_analog[1].value),
			              input.trigger[0].down, input.trigger[1].down, double(input.trigger_analog[0].value),
			              double(input.trigger_analog[1].value), input.grip[0].valid && input.aim[0].valid,
			              input.grip[1].valid && input.aim[1].valid, detail.c_str());
			bool schedule{};
			{
				const std::lock_guard lock(journal_mutex);
				journal_lines.emplace_back(text);
				if (journal_lines.size() > capacity)
					journal_lines.pop_front();
				schedule = !write_pending;
				write_pending = true;
			}
			if (schedule)
				scheduler::once(
				    []
				    {
					    std::string all;
					    {
						    const std::lock_guard lock(journal_mutex);
						    write_pending = false;
						    for (const auto& line : journal_lines)
							    all += line;
					    }
					    utils::io::write_file_atomic("minidumps/overlord-support.txt", all);
				    },
				    scheduler::pipeline::async);
		}
		catch (...)
		{
		}
	}
}
