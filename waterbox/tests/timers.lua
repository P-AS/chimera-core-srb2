-- timers.lua - the gate's properties leg: SRB2 Lua (an add-on the leg's
-- autoexec.cfg loads) that sets the four timers the Game State domain copies,
-- after the game's own think, so what the domain shows is known exactly.
-- Speed shoes and invincibility are set once, at leveltime 20, and count down
-- a tic at a time; the air and space timers, which the game zeroes outside
-- water and space every tic, are set every tic from 20 to a value that names
-- the tic. So are the conveyor and platform momenta (player.cmomx, cmomy and
-- mo.pmomz, raw 16.16 values: a few hundredths of a unit a tic).
addHook("PostThinkFrame", function()
	for player in players.iterate do
		if leveltime == 20 then
			player.powers[pw_sneakers] = 200
			player.powers[pw_invulnerability] = 150
		end
		if leveltime >= 20 then
			player.powers[pw_underwater] = 1000 + leveltime
			player.powers[pw_spacetime] = 3000 + leveltime
			player.cmomx = 5000 + leveltime
			player.cmomy = -(5000 + leveltime)
			player.mo.pmomz = 7000 + leveltime
		end
	end
end)
