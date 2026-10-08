-- tasinfo.lua - SRB2 TAS info for Chimera: the player's speed, angle,
-- position, momentum, the momentum a conveyor or moving platform adds, and the
-- power timers, drawn over the game every frame.
--
-- Open it in Chimera's Lua Console (Tools > Lua Console) with an SRB2 project
-- loaded. It reads the core's Game State properties by name (game.get; the
-- names are the ones RAM Watch's Add Game Properties lists), which the core
-- copies from the game after every step, so what it shows is the frame on
-- screen.
--
-- Positions, momenta and speed are SRB2's 16.16 fixed point, shown in map
-- units (65536 to a unit) to 4 decimal places. The angle is shown as the
-- game's own 32-bit value in hex (0x40000000 a quarter turn, counterclockwise
-- from east) and in degrees to 4 decimal places. Timers are tics left (35 a
-- second), and seconds.

-- where the text goes, in the window's pixels, and the room a line takes
local X, Y, LINE = 2, 2, 14
local COLOR = 0xFFFFFFFF

local FRACUNIT = 65536
local TICRATE = 35

-- the core must have its Game State: a build from before it has no properties
local function has_properties()
	for _, name in ipairs(game.list()) do
		if name == "Player.Speed" then return true end
	end
	return false
end

local function fixed(name)
	return string.format("%.4f", game.get(name) / FRACUNIT)
end

local function timer(name)
	local tics = game.get(name)
	return string.format("%d (%.2fs)", tics, tics / TICRATE)
end

local function draw()
	local lines = {}
	local function add(text) lines[#lines + 1] = text end

	if not game.get("Player.In Level") then
		add("Not in a level")
	else
		local angle = game.get("Player.Angle")
		add("Speed: " .. fixed("Player.Speed"))
		add(string.format("Angle: 0x%08X", angle))
		add(string.format("Angle: %.4f deg", angle * 360 / 4294967296))
		add("X: " .. fixed("Player.X"))
		add("Y: " .. fixed("Player.Y"))
		add("Z: " .. fixed("Player.Z"))
		add("Mom X: " .. fixed("Player.Momentum X"))
		add("Mom Y: " .. fixed("Player.Momentum Y"))
		add("Mom Z: " .. fixed("Player.Momentum Z"))
		add("Conveyor Mom X: " .. fixed("Player.Conveyor Momentum X"))
		add("Conveyor Mom Y: " .. fixed("Player.Conveyor Momentum Y"))
		add("Platform Mom Z: " .. fixed("Player.Platform Momentum Z"))
		add("Shoes: " .. timer("Timers.Speed Shoes"))
		add("Invincibility: " .. timer("Timers.Invincibility"))
		add("Space: " .. timer("Timers.Space"))
		add("Air: " .. timer("Timers.Air"))
	end

	for i, text in ipairs(lines) do
		gui.text(X, Y + (i - 1) * LINE, text, COLOR)
	end
end

if not has_properties() then
	console.log("tasinfo.lua: this core has no SRB2 Game State properties - load an SRB2 project, with a chimera-core-srb2 that has them")
	return
end

while true do
	draw()
	emu.frameadvance()
end
