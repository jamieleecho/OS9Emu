-- Drive a real NitrOS-9 machine in MAME and capture what it shows.
--
-- MAME runs this as an autoboot script. It types whatever OS9SCRIPT tells it
-- to and takes named screenshots, so a session on the real system can be put
-- beside the same session under os9emu.
--
-- Two ways of capturing exist, because which one works depends on the machine:
--
--   snap NAME   a PNG screenshot. Always works; you read it with your eyes.
--   dump NAME   the text screen decoded from memory. Only works where the
--               screen is in the CPU's own address space -- true on a CoCo 1
--               or 2, but not on a CoCo 3, whose GIME keeps video RAM in
--               physical memory that MAME's Lua cannot reach.
--
-- Environment:
--   OS9SCRIPT   file of directives, one per line
--   OS9OUT      file to write dumped screens and the snapshot manifest to
--   OS9SCREEN   base address for "dump" (default 0x0400)
--
-- Directives:
--   wait N      advance N emulated seconds
--   idle N      wait until the screen has been unchanged for N seconds
--   type TEXT   post TEXT through the keyboard, then a carriage return
--   raw TEXT    post TEXT with no carriage return
--   snap NAME   take a screenshot
--   dump NAME   decode the text screen out of memory
--   quit        stop

local script_path = os.getenv("OS9SCRIPT")
local out_path    = os.getenv("OS9OUT") or "/tmp/os9mame.txt"
local screen_base = tonumber(os.getenv("OS9SCREEN") or "0x0400")

local COLS, ROWS = 32, 16

local space = manager.machine.devices[":maincpu"].spaces["program"]
local out = assert(io.open(out_path, "w"), "cannot write " .. out_path)

-- A VDG character byte is not ASCII. Bits 0-5 index the 64-character generator
-- (@, A-Z, punctuation, digits), bit 6 is the inverse-video flag, and bit 7
-- means semigraphics, which is not text at all.
--
-- The CoCo's normal look is inverse -- dark letters on green -- so a byte with
-- bit 6 SET is ordinary uppercase. Letters written without it come out as
-- reverse video, and that is how both BASIC and OS-9 render lowercase.
local function vdg_to_ascii(b)
	if b >= 0x80 then return " " end
	local code = b & 0x3f
	local c = (code < 0x20) and (code + 0x40) or code
	if (b & 0x40) == 0 and c >= 0x41 and c <= 0x5a then
		c = c + 32
	end
	if c < 0x20 or c > 0x7e then return " " end
	return string.char(c)
end

local function screen_text()
	local lines = {}
	for row = 0, ROWS - 1 do
		local chars = {}
		for col = 0, COLS - 1 do
			chars[#chars + 1] =
				vdg_to_ascii(space:read_u8(screen_base + row * COLS + col))
		end
		lines[#lines + 1] = (table.concat(chars):gsub("%s+$", ""))
	end
	while #lines > 0 and lines[#lines] == "" do table.remove(lines) end
	return table.concat(lines, "\n")
end

-- Whether the picture has stopped changing, sampled from the screen device so
-- it works whatever the machine does with its video memory.
local function frame_hash()
	local scr
	for _, s in pairs(manager.machine.screens) do scr = s break end
	if scr == nil then return 0 end
	local w, h = scr.width, scr.height
	local x = 2166136261
	for sy = 0, 7 do
		for sx = 0, 9 do
			local px = math.floor((sx + 0.5) * w / 10)
			local py = math.floor((sy + 0.5) * h / 8)
			x = (x ~ scr:pixel(px, py)) * 16777619 % 4294967296
		end
	end
	return x
end

local function wait_idle(secs)
	local last, stable = frame_hash(), manager.machine.time
	local started = manager.machine.time
	while true do
		emu.wait(1 / 20)
		local h = frame_hash()
		if h ~= last then last, stable = h, manager.machine.time end
		if (manager.machine.time - stable):as_double() >= secs then return end
		-- A CoCo blinks its cursor, so the picture is never actually still
		-- and this would otherwise never return. Give up quickly and let the
		-- session use explicit waits, which are what it should rely on.
		if (manager.machine.time - started):as_double() >= secs * 2 + 5 then
			return
		end
	end
end

-- Post a line through the keyboard.
--
-- The pause first matters: posting into a prompt that has only just appeared
-- loses the leading character -- "free" arrives as "ree". The pause after lets
-- MAME drain the buffer, which it does one character at a time.
local function post(text)
	emu.wait(1.5)
	manager.machine.natkeyboard:post(text)
	emu.wait(#text * 0.25 + 1.5)
end

local snap_index = 0

local directives = {}
if script_path then
	for line in assert(io.lines(script_path)) do
		directives[#directives + 1] = line
	end
end

for _, line in ipairs(directives) do
	local cmd, rest = line:match("^(%S+)%s?(.*)$")
	if cmd == nil or cmd == "#" then
		-- comment or blank
	elseif cmd == "wait" then
		emu.wait(tonumber(rest) or 1)
	elseif cmd == "idle" then
		wait_idle(tonumber(rest) or 2)
	elseif cmd == "type" then
		post(rest .. "\n")
	elseif cmd == "raw" then
		post(rest)
	elseif cmd == "snap" then
		-- MAME numbers snapshots itself, in the order they are taken, so
		-- record the pairing for the runner to rename them by.
		manager.machine.video:snapshot()
		out:write(("snapshot\t%04d\t%s\n"):format(snap_index, rest))
		snap_index = snap_index + 1
		out:flush()
	elseif cmd == "dump" then
		out:write(("===== %s\n%s\n"):format(rest, screen_text()))
		out:flush()
	elseif cmd == "quit" then
		break
	end
end

out:close()
manager.machine:exit()
