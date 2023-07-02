#include "memory.h"
#include "vector.h"
#include <thread>


//Taken from https://github.com/frk1/hazedumper
namespace offsets {
	// client offsets
	constexpr ::std::ptrdiff_t dwLocalPlayer = 0xDEA98C;
	constexpr ::std::ptrdiff_t dwEntityList = 0x4DFFF7C;

	// engine
	constexpr ::std::ptrdiff_t dwClientState = 0x59F19C;
	constexpr ::std::ptrdiff_t dwClientState_ViewAngles = 0x4D90;
	constexpr ::std::ptrdiff_t dwClientState_GetLocalPlayer = 0x180;

	// entity
	constexpr ::std::ptrdiff_t m_dwBoneMatrix = 0x26A8;
	constexpr ::std::ptrdiff_t m_bDormant = 0xED;
	constexpr ::std::ptrdiff_t m_iTeamNum = 0xF4;
	constexpr ::std::ptrdiff_t m_lifeState = 0x25F;
	constexpr ::std::ptrdiff_t m_vecOrigin = 0x138;
	constexpr ::std::ptrdiff_t m_vecViewOffset = 0x108;
	constexpr ::std::ptrdiff_t m_aimPunchAngle = 0x303C;
	constexpr ::std::ptrdiff_t m_bSpottedByMask = 0x980;
}


// To find the angle from the player to the enemy, we can subtract our vector from the enemy's.
// The resulting angle is the angle we need to move our crosshair to in order for it to be on the enemy's head.
// Something something, trigonometry 
constexpr Vector3 CalculateAngle(
	const Vector3& localPosition,
	const Vector3& enemyPosition,
	const Vector3& viewAngles) noexcept
{
	return ((enemyPosition - localPosition).ToAngle() - viewAngles);
}


int main() {

	// initialize an instance of the memory class
	const auto memory = Memory{ "csgo.exe" };

	// find the memory addresses of the client and engine
	const auto client = memory.GetModuleAddress("client.dll");
	const auto engine = memory.GetModuleAddress("engine.dll");

	// Infinite loop, slept to not overwhelm the CPU
	while (true) {
		// sleeps for 1 milisecond
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		// only do aimbot if the right mouse button is currently pressed
		if (!GetAsyncKeyState(VK_RBUTTON))
			continue;

		// get the local player and team. 
		// This can be done by using the memory address found for the client plus the offset for the local player (provided by HazeDumper)
		const auto localPlayer = memory.Read<std::uintptr_t>(client + offsets::dwLocalPlayer);
		//once the player is found, their team can be found by using the team num offset
		const auto localTeam = memory.Read<std::int32_t>(localPlayer + offsets::m_iTeamNum);


		// the player's "eye" position can be found by getting the local player's vector origin and adding the view offset
		const auto localEyePosition = memory.Read<Vector3>(localPlayer + offsets::m_vecOrigin) +
			memory.Read<Vector3>(localPlayer + offsets::m_vecViewOffset);

		// finding additional important variables can all be done with the same method used above
		const auto clientState = memory.Read<std::uintptr_t>(engine + offsets::dwClientState);

		const auto localPlayerId =
			memory.Read<std::int32_t>(clientState + offsets::dwClientState_GetLocalPlayer);

		const auto viewAngles = memory.Read<Vector3>(clientState + offsets::dwClientState_ViewAngles);
		const auto aimPunch = memory.Read<Vector3>(localPlayer + offsets::m_aimPunchAngle) * 2;


		// now we can finally write the actual aimbot portion
		// This aimbot will work by looking within a given field of view for the best enemy player to aim at
		// Here we can adjust the FOV of our aimbot
		auto bestFov = 5.f;
		auto bestAngle = Vector3{ };

		// loop through all entities in the entities list
		for (auto i = 1; i <= 32; ++i)
		{
			// current entity
			const auto player = memory.Read<std::uintptr_t>(client + offsets::dwEntityList + i * 0x10);

			// don't aimbot if:

			// entity is on our team
			if (memory.Read<std::int32_t>(player + offsets::m_iTeamNum) == localTeam)
				continue;

			// entityy is dormant
			if (memory.Read<bool>(player + offsets::m_bDormant))
				continue;

			// entity is dead
			if (memory.Read<std::int32_t>(player + offsets::m_lifeState))
				continue;

			// if we get this far, then the only other check to do is whether the enetity can be seen
			if (memory.Read<std::int32_t>(player + offsets::m_bSpottedByMask) & (1 << localPlayerId))
			{

				// now get the bone matrix of the entity
				const auto boneMatrix = memory.Read<std::uintptr_t>(player + offsets::m_dwBoneMatrix);

				// canculate the position of the entity's head
				// we multiply by 8 here because that is the position of the head within the bone matrix
				const auto playerHeadPosition = Vector3{
					memory.Read<float>(boneMatrix + 0x30 * 8 + 0x0C),
					memory.Read<float>(boneMatrix + 0x30 * 8 + 0x1C),
					memory.Read<float>(boneMatrix + 0x30 * 8 + 0x2C)
				};

				// calculate the angle between us and the player's head
				const auto angle = CalculateAngle(
					localEyePosition,
					playerHeadPosition,
					viewAngles + aimPunch
				);

				// the fov is the hypotenuse of of the angle between us and the player
				const auto fov = std::hypot(angle.x, angle.y);

				// if that fov is smaller than our predetermined fov, set best angle
				if (fov < bestFov)
				{
					bestFov = fov;
					bestAngle = angle;
				}
			}
		}

		// if we have a best angle, do aimbot
		if (!bestAngle.IsZero()) 
			memory.Write<Vector3>(clientState + offsets::dwClientState_ViewAngles, viewAngles + bestAngle); // smoothing / 3.f

	}


	return 0;
}