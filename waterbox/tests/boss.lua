-- boss.lua - the gate's properties leg: SRB2 Lua (an add-on the leg's
-- autoexec.cfg loads) that puts a boss in Greenflower Zone Act 1, which has
-- none, and hits it, so the Game State's Boss fields are known: an Egg Mobile
-- spawned ahead of the player (256 units east and north, 64 up, along its facing) at
-- leveltime 30, hit once by the player
-- at 40 (one health off, flashing until its pain state ends). And Metal Sonic's
-- dash mode counter, set after the game's think every tic from 20 to a value
-- that names the tic.
local boss
addHook("PostThinkFrame", function()
	for player in players.iterate do
		if leveltime == 30 then
			boss = P_SpawnMobjFromMobj(player.mo, 256*FRACUNIT, 256*FRACUNIT, 64*FRACUNIT, MT_EGGMOBILE)
		end
		if leveltime == 40 and boss and boss.valid then
			P_DamageMobj(boss, player.mo, player.mo)
		end
		if leveltime >= 20 then
			player.dashmode = 2000 + leveltime
		end
	end
end)
