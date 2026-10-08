-- tasinfo.lua - SRB2 TAS info for Chimera: the player's speed, angle,
-- position, momentum, the momentum a conveyor or moving platform adds, the
-- spindash's revs, Metal Sonic's dash mode, the power timers and the boss's
-- health and flashing, drawn over the game every frame.
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
-- The conveyor and platform momentum, the spindash revs, the dash mode
-- counter and the four timers are shown only while they are not 0.
--
-- As the SRB2 TAS build (TASBuild.2215.patch) shows them: Metal Sonic's dash
-- mode counter (tics at top speed: dash mode from 105, counting stops at 108),
-- and at 108 the dash mode's raised top speed; and while a boss is in the
-- level - the first MF_BOSS object, as the TAS build finds it - its health
-- (hits left / at the start) and whether it is flashing from a hit (it cannot
-- be hit again until it stops).

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
local DASHMODE_MAX = 108

-- what this core's Game State has: a core from before a property was added
-- lacks it, and the lines that need it are left out rather than the script
local HAVE = {}
for _, name in ipairs(game.list()) do HAVE[name] = true end
local function has(...)
	for _, name in ipairs({ ... }) do
		if not HAVE[name] then return false end
	end
	return true
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
	if not has("Player.Charging Spindash", "Player.Min Dash", "Player.Max Dash", "Player.Dash Speed")
		or game.get("Player.Charging Spindash") == 0 then return 0, 0 end
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
		-- the rest only while not 0, and only what the core has
		local function nonzero(label, name, show)
			if has(name) and game.get(name) ~= 0 then add(label .. show(name)) end
		end
		nonzero("Conveyor Mom X: ", "Player.Conveyor Momentum X", fixed)
		nonzero("Conveyor Mom Y: ", "Player.Conveyor Momentum Y", fixed)
		nonzero("Platform Mom Z: ", "Player.Platform Momentum Z", fixed)
		local revs, full = spindash_revs()
		if revs ~= 0 then add(string.format("Spindash revs: %d/%d", revs, full)) end
		local dashmode = has("Player.Dashmode") and game.get("Player.Dashmode") or 0
		if dashmode ~= 0 then add("Dashmode: " .. dashmode) end
		if dashmode == DASHMODE_MAX and has("Player.Normal Speed") then
			add("Dashmode Speed: " .. fixed("Player.Normal Speed"))
		end
		nonzero("Shoes: ", "Timers.Speed Shoes", timer)
		nonzero("Invincibility: ", "Timers.Invincibility", timer)
		nonzero("Space: ", "Timers.Space", timer)
		nonzero("Air: ", "Timers.Air", timer)
		if has("Boss.Active", "Boss.Health", "Boss.Max Health", "Boss.Flashing") and game.get("Boss.Active") then
			add(string.format("Boss Health: %d/%d", game.get("Boss.Health"), game.get("Boss.Max Health")))
			add("Boss Flashing: " .. (game.get("Boss.Flashing") and "Yes" or "No"))
		end
	end

	-- from the bottom up: the last line nearest the corner, the first on top
	for i, text in ipairs(lines) do
		gui.text(X, Y + (#lines - i) * LINE, text, COLOR, ANCHOR)
	end
end

-- what every line above needs; the rest is shown where the core has it
local BASE = { "Player.In Level", "Player.Speed", "Player.Angle", "Player.X", "Player.Y", "Player.Z",
	"Player.Momentum X", "Player.Momentum Y", "Player.Momentum Z" }
if not has(table.unpack(BASE)) then
	console.log("tasinfo.lua: no SRB2 Game State here - load an SRB2 project first, with a chimera-core-srb2 from 2026-10-08 or later")
	return
end
local OPTIONAL = { "Player.Conveyor Momentum X", "Player.Conveyor Momentum Y", "Player.Platform Momentum Z",
	"Player.Dash Speed", "Player.Min Dash", "Player.Max Dash", "Player.Charging Spindash",
	"Player.Dashmode", "Player.Normal Speed",
	"Timers.Speed Shoes", "Timers.Invincibility", "Timers.Space", "Timers.Air",
	"Boss.Active", "Boss.Health", "Boss.Max Health", "Boss.Flashing" }
local missing = {}
for _, name in ipairs(OPTIONAL) do
	if not HAVE[name] then missing[#missing + 1] = name end
end
if #missing > 0 then
	console.log("tasinfo.lua: this SRB2 core is older than the script; not shown: " .. table.concat(missing, ", ") ..
		" - install the latest chimera-core-srb2 for them")
end

while true do
	draw()
	emu.frameadvance()
end
