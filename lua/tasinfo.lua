-- tasinfo.lua - SRB2 TAS info for Chimera: the player's speed, angle,
-- position, momentum, the momentum a conveyor or moving platform adds, the
-- spindash's revs and the power timers, drawn over the game every frame.
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
--
-- Spindash revs: SRB2 charges a spindash 1.0 of speed for every tic Spin is
-- held, from the character's least charge to its most (Sonic: 15 to 70), so a
-- rev is a tic of charge; shown as revs so far / the revs to full charge.
--
-- The conveyor and platform momentum, the spindash revs and the four timers
-- are shown only while they are not 0.

-- where the text goes: the bottom right corner, clear of Chimera's own HUD
-- (FPS, frame and lag counters, input, re-records: top left; messages: bottom
-- left; autohold and game time: top right). X and Y are the gap to the right
-- and bottom edges in the window's pixels; LINE is the room a line takes,
-- as Chimera's HUD spaces its own lines. Each line is right-aligned.
local X, Y, LINE = 2, 2, 14
local ANCHOR = "bottomright"
local COLOR = 0xFFFFFFFF

local FRACUNIT = 65536
local TICRATE = 35

-- the core must have its Game State: a build from before it has no properties
local function has_properties()
	for _, name in ipairs(game.list()) do
		if name == "Player.Charging Spindash" then return true end
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

-- a tic of charge each, while revving (0 when not)
local function spindash_revs()
	if game.get("Player.Charging Spindash") == 0 then return 0, 0 end
	local least = game.get("Player.Min Dash")
	return (game.get("Player.Dash Speed") - least) // FRACUNIT, (game.get("Player.Max Dash") - least) // FRACUNIT
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
		-- the rest only while not 0
		local function nonzero(label, name, show)
			if game.get(name) ~= 0 then add(label .. show(name)) end
		end
		nonzero("Conveyor Mom X: ", "Player.Conveyor Momentum X", fixed)
		nonzero("Conveyor Mom Y: ", "Player.Conveyor Momentum Y", fixed)
		nonzero("Platform Mom Z: ", "Player.Platform Momentum Z", fixed)
		local revs, full = spindash_revs()
		if revs ~= 0 then add(string.format("Spindash revs: %d/%d", revs, full)) end
		nonzero("Shoes: ", "Timers.Speed Shoes", timer)
		nonzero("Invincibility: ", "Timers.Invincibility", timer)
		nonzero("Space: ", "Timers.Space", timer)
		nonzero("Air: ", "Timers.Air", timer)
	end

	-- from the bottom up: the last line nearest the corner, the first on top
	for i, text in ipairs(lines) do
		gui.text(X, Y + (#lines - i) * LINE, text, COLOR, ANCHOR)
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
