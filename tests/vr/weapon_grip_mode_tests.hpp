#pragma once
#include "component/vr/gameplay/grip_edges.hpp"
#include "component/vr/gameplay/weapon_carry_grip.hpp"
#include "component/vr/gameplay/support_retention.hpp"

namespace weapon_grip_mode_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;
		using namespace carry;
		using namespace std::chrono_literals;
		for (bool toggle : {false, true})
		for (unsigned h = 0; h < 2; ++h)
		{
			inventory owned;
			const owned_weapon items[]{{52, {false, false}}};
			owned.reconcile(items);
			owned.equip_definition(52, hand(1 - h));
			const auto id = owned.in_hand(hand(1 - h))->id;
			grip_edge_gate gate;
			vr::controller_input::digital_sampler sampler;
			vr::controller_input::frame f;
			f.focused = true;
			f.reference_generation = 1;
			f.sampled_at = vr::controller_input::clock::time_point{} + 1s;
			const auto tick = [&](bool down)
			{
				++f.sequence;
				f.sampled_at += 10ms;
				f.grip[h].valid = true;
				f.squeeze[h] = sampler.sample(true, down, f.sampled_at);
				return gate.consume(f, true, f.sampled_at, toggle_grip_owners(owned, toggle));
			};
			tick(false);
			check(claim_grip(owned, {.actor=hand(h), .pressed=bool(tick(true).pressed),
				.available=true, .support=id}).pose_changed, "fresh Grip press claims support for either hand and button mode");
			profile p;
			p.support_enabled = true;
			p.aiming = aim_rule::two_hand;
			p.acquire_meters = .15f;
			p.release_meters = .3f;
			const vr::gameplay::hands::anchor contact{{0, 0, .25f}, {0, 0, 0, 1}};
			const vr::gameplay::hands::vec distant{0, 0, 2};
			check(!support_contact(p, {}, contact, {}, distant, 1), "support lock cannot acquire a distant contact");
			check(retained_support(p, {}, contact, {}, distant, 1, true, true)==support_retention::retained &&
				tick(true).released==0, "held support ignores distance in either button mode");
			check(retained_support(p, {}, contact, {}, distant, 1, true, false)==support_retention::released,
				"disabling support lock restores distance release immediately");
			check(retained_support(p, {}, contact, {}, distant, 0, true, true)==support_retention::suspended,
				"locking cannot admit invalid geometry units");
			p.support_enabled = false;
			check(retained_support(p, {}, contact, {}, distant, 1, true, true)==support_retention::released,
				"locking cannot keep support on a profile that forbids it");
			auto release = tick(false);
			if (toggle)
			{
				check(release.released==0 && owned.find(id)->owner.support==hand(h), "toggle support survives button-up");
				release = tick(true);
			}
			const auto result = owned.release(id, release.released, location::absent, false, [](const auto&) { return false; });
			check(release.released==(1u << h) && result.action==outcome::support_released &&
				owned.find(id)->owner.support==hand::none && owned.in_hand(hand(1 - h)) && owned.invariant(),
				"explicit Grip release removes only support even while the hand is far away");
		}
		for (unsigned h = 0; h < 2; ++h)
		{
			const auto actor = hand(h);
			const auto bit = 1u << h;
			inventory owned;
			const owned_weapon items[]{{101, {true, true}}, {102, {true, true}}, {64, {false, false, true}}};
			check(owned.reconcile(items), "grip mode fixture has ordinary and mission equipment");
			grip_edge_gate gate;
			vr::controller_input::digital_sampler sampler;
			vr::controller_input::frame f;
			f.focused = true;
			f.reference_generation = 1;
			f.sampled_at = vr::controller_input::clock::time_point{} + 1s;
			bool toggle = true;
			const auto tick = [&](bool down, bool pose = true, bool active = true)
			{
				++f.sequence;
				f.sampled_at += 10ms;
				f.grip[h].valid = pose;
				f.squeeze[h] = sampler.sample(active, down, f.sampled_at);
				return gate.consume(f, true, f.sampled_at, toggle_grip_owners(owned, toggle));
			};
			tick(false);
			check(tick(true).pressed == bit, "empty hand still acquires only on a physical press");
			owned.equip_definition(101, actor);
			const auto id = owned.in_hand(actor)->id;
			check(tick(false).released == 0 && tick(false).released == 0,
			      "toggle pickup ignores physical release and repeated neutral input");
			const auto release = tick(true);
			check(release.released == bit && release.pressed == 0 && release.consumed == bit,
			      "second press releases the weapon and cannot also acquire another object");
			check(tick(true).released == 0, "a held second press cannot repeat a failed drop");
			tick(false);
			auto retry = tick(true);
			const auto exchanged = owned.release(id, retry.released, owned.find_definition(102)->at,
			                                    true, [](const auto&) { return true; });
			check(exchanged.action == outcome::exchanged, "toggle release uses the existing holster exchange");
			gate.latch(actor);
			check(tick(false).released == 0 && tick(true).released == bit,
			      "exchanged weapon stays held until another press");

			tick(false);
			const auto lost = tick(true, false);
			check(lost.released == 0 && lost.deferred == bit && lost.consumed == bit,
			      "toggle release waits for valid geometry while reserving its press");
			tick(false, false);
			check(tick(false).released == bit && tick(false).released == 0,
			      "a quick second tap during pose loss settles once on recovery");
			tick(true, false);
			++f.reference_generation;
			check(tick(false).released == 0, "recenter cancels deferred toggle release");
			tick(true, false);
			f.focused = false;
			tick(true);
			f.focused = true;
			check(tick(true).released == 0 && tick(false).released == 0,
			      "focus recovery cannot drop a toggled weapon");
			check(tick(true).released == bit, "next fresh press works after focus recovery");
			tick(false);
			tick(true, false);
			owned.equip_definition(101, actor);
			check(tick(false).released == 0, "deferred press cannot drop a replacement instance");
			tick(true, false);
			toggle = false;
			check(tick(true).released == 0, "changing to hold cancels a pending toggle press");
			tick(false);
			tick(true);
			check(tick(false).released == bit, "hold mode still releases on button up");
			toggle = true;
			tick(true, true, false);
			check(tick(true).released == 0 && tick(false).released == 0,
			      "inactive controller recovery requires a new toggle press");

			owned.equip_definition(64, actor);
			tick(true);
			check(tick(false).released == bit, "mission equipment keeps physical release in toggle mode");
			owned.put_away();
			tick(true);
			check(!owned.in_hand(actor) && tick(false).released == bit,
			      "failed pickup never creates logical weapon ownership");

			owned.equip_definition(101, actor);
			const auto other = hand(1 - h);
			owned.support(owned.in_hand(actor)->id, other);
			const auto normal = toggle_grip_owners(owned, true);
			const auto mechanical = toggle_grip_owners(owned, true, 1u << unsigned(other));
			check(normal[h] && normal[1 - h] && mechanical[h] && !mechanical[1 - h],
			      "ordinary support toggles while mechanical module grips retain physical input");
		}
		{
			inventory owned;
			const owned_weapon items[]{{101, {true, true}}};
			owned.reconcile(items);
			owned.equip_definition(101, hand::right);
			owned.support(owned.in_hand(hand::right)->id, hand::left);
			grip_edge_gate gate;
			vr::controller_input::frame f;
			f.focused = true;
			f.sequence = f.reference_generation = 1;
			f.sampled_at = vr::controller_input::clock::now();
			for (unsigned h = 0; h < 2; ++h)
			{
				f.grip[h].valid = f.squeeze[h].active = true;
				f.squeeze[h].generation = 1;
			}
			gate.consume(f, true, f.sampled_at, toggle_grip_owners(owned, true));
			++f.sequence;
			for (auto& button : f.squeeze)
			{
				++button.presses;
				button.down = true;
			}
			const auto edges = gate.consume(f, true, f.sampled_at, toggle_grip_owners(owned, true));
			unsigned drops{};
			const auto released = owned.release(owned.in_hand(hand::right)->id, edges.released,
			                                    location::absent, true, [&](const auto&) { ++drops; return true; });
			check(edges.released == 3 && edges.consumed == 3 && drops == 1 &&
			      released.action == outcome::dropped && owned.invariant(),
			      "simultaneous toggle presses drop once without transient hand promotion");
		}
		{
			// Physical gunstock latch: hold mode, but the stocked front hand toggles.
			inventory owned;
			const owned_weapon items[]{{101, {true, true}}};
			owned.reconcile(items);
			owned.equip_definition(101, hand::right);
			const auto id = owned.in_hand(hand::right)->id;
			grip_edge_gate gate;
			std::array<vr::controller_input::digital_sampler, 2> samplers;
			vr::controller_input::frame f;
			f.focused = true;
			f.reference_generation = 1;
			f.sampled_at = vr::controller_input::clock::time_point{} + 1s;
			const auto stocked = [](const instance&) { return true; };
			const auto tick = [&](bool rear_down, bool front_down)
			{
				++f.sequence;
				f.sampled_at += 10ms;
				f.grip[0].valid = f.grip[1].valid = true;
				f.squeeze[0] = samplers[0].sample(true, front_down, f.sampled_at);
				f.squeeze[1] = samplers[1].sample(true, rear_down, f.sampled_at);
				const auto latched = latched_support_hands(owned, true, stocked);
				auto edges = gate.consume(f, true, f.sampled_at, toggle_grip_owners(owned, false, 0, latched));
				edges.released = released_with_latch(owned, edges.released, latched);
				return edges;
			};
			tick(true, false);
			check(latched_support_hands(owned, true, stocked) == 0, "a rear hand alone is never latched");
			check(tick(true, true).pressed == 1u, "the front hand still takes support on a physical press");
			owned.support(id, hand::left);
			check(latched_support_hands(owned, true, stocked) == 1u &&
			      latched_support_hands(owned, false, stocked) == 0 &&
			      latched_support_hands(owned, true, [](const instance&) { return false; }) == 0,
			      "only an enabled latch on a stocked weapon marks the support hand");
			check(tick(true, false).released == 0 && tick(true, false).released == 0,
			      "a latched front hand survives letting go of Grip");
			const auto again = tick(true, true);
			check(again.released == 1u && again.consumed == 1u && again.pressed == 0,
			      "a second front press releases support and cannot acquire another object");
			auto result = owned.release(id, again.released, location::absent, false, [](const auto&) { return false; });
			check(result.action == outcome::support_released && owned.in_hand(hand::right) &&
			      owned.find(id)->owner.support == hand::none,
			      "the latch release removes only the support hand");
			tick(true, false);
			check(tick(true, true).pressed == 1u, "support can be taken again after a latch release");
			owned.support(id, hand::left);
			check(tick(true, false).released == 0, "the retaken support is latched again");
			check(tick(false, false).released == 3u,
			      "letting go of the rear hand also lets go of its latched front hand");
		}
	}
}
