-- norns.lua
-- main norns script, called by matron
-- defines top-level global tables and functions needed by other modules
-- external c functions are in the _norns table
-- external callbacks in the norns table, which also includes management

norns = {}

local engine = require 'core/engine'
local poll = require 'core/poll'
local tab = require 'tabutil'
local util = require 'util'
local arc = require 'core/arc'

-- Global Functions.

-- global functions required by the C interface;
-- we "declare" these here with placeholders;
-- individual modules will redefine them as needed.

-- key callback
norns.key = function(n,z) end
-- enc callback
norns.enc = function(n,delta) end

-- monome device management
norns.monome = {}
norns.monome.add = function(id, serial, name, dev)
  if util.string_starts(name, "monome arc") then
    norns.arc.add(id, serial, name, dev)
  else norns.grid.add(id, serial, name, dev) end
end
norns.monome.remove = function(id)
  if arc.devices[id] then norns.arc.remove(id)
  else norns.grid.remove(id) end
end

-- grid device callbacks.
norns.grid = {}
norns.grid.key = function(id, x, y, val) end

-- arc device callbacks.
norns.arc = {}
norns.arc.delta = function(id, n, delta) end
norns.arc.key = function(id, n, s) end

-- hid callbacks.
norns.hid = {}
norns.hid.add = function(id, name, types, codes, dev) end
norns.hid.event = function(id, ev_type, ev_code, value) end

-- midi callbacks (defined in midi.lua).
norns.midi = {}

-- osc callbacks (defined in osc.lua)
norns.osc = {}


-- report callbacks
norns.report = {}
norns.report.engines = function(names, count)
   engine.register(names, count)
end
norns.report.commands = function(commands, count)
   engine.register_commands(commands, count)
   engine.list_commands()
end
norns.report.polls = function(names, count)
   poll.register(names, count)
   poll.list_names()
end


-- called when all reports are complete after engine load.
norns.report.did_engine_load = function() end

-- startup handlers.
norns.startup_status = {}
norns.startup_status.ok = function() print(">>> startup ok") end
norns.startup_status.timeout = function() print(">>> startup timeout") end

-- poll callback; used by C interface.
norns.poll = function(id, value)
   local name = poll.poll_names[id]
   local p = poll.polls[name]
   if p then
    p:perform(value)
   else
    print ("warning: norns.poll callback couldn't find poll")
   end
end

-- i/o level callback.
norns.vu = function(in1, in2, out1, out2) end
-- softcut phase
norns.softcut_phase = function(id, value) end

-- default readings for battery
norns.battery_percent = 0
norns.battery_current = 0

-- battery percent handler
norns.battery = function(percent, current)
  if current < 0 and percent < 5 then
    screen.update = screen.update_low_battery
  elseif current > 0 and norns.battery_current < 0 then
    screen.update = screen.update_default
  end
  norns.battery_percent = tonumber(percent)
  norns.battery_current = tonumber(current)
  --print("battery: "..norns.battery_percent.."% "..norns.battery_current.."mA")
end

-- power present handler
norns.power = function(present)
  norns.powerpresent = present
  --print("power: "..present)
end

-- stat handler
norns.stat = function(disk, temp, cpu)
  --print("stat",disk,temp,cpu)
  norns.disk = disk
  norns.temp = temp
  norns.cpu = cpu
end


-- management
norns.script = require 'core/script'
norns.state = require 'core/state'
norns.encoders = require 'core/encoders'

norns.enc = norns.encoders.process

-- Error handling.
norns.scripterror = function(msg) print(msg) end
norns.try = function(f,msg)
  local handler = function (err) return err .. "\n" .. debug.traceback() end
  local status, err = xpcall(f, handler)
  if not status then
    norns.scripterror(msg)
    print(err)
  end
  return status
end

-- Null functions.
-- @section null

-- do nothing.
norns.none = function() end

-- blank screen.
norns.blank = function()
  _norns.screen_clear()
  _norns.screen_update()
end


-- Version
norns.version = {}
-- import update version number
local fd = io.open(os.getenv("HOME").."/version.txt","r")
if fd then
  io.input(fd)
  norns.version.update = io.read()
  io.close(fd)
else
  norns.version.update = "000000"
end


-- Util (system_cmd)
local system_cmd_q = {}
local system_cmd_busy = false

-- add cmd to queue
norns.system_cmd = function(cmd, callback)
  table.insert(system_cmd_q, {cmd=cmd, callback=callback})
  if system_cmd_busy == false then
    system_cmd_busy = true
    _norns.system_cmd(cmd)
  end
end

-- callback management from c
norns.system_cmd_capture = function(cap)
  if system_cmd_q[1].callback == nil then print(cap)
  else system_cmd_q[1].callback(cap) end
  table.remove(system_cmd_q,1)
  if #system_cmd_q > 0 then
    _norns.system_cmd(system_cmd_q[1].cmd)
  else
    system_cmd_busy = false
  end
end


-- startup function will be run after I/O subsystems are initialized,
-- but before I/O event loop starts ticking (see readme-script.md)
_startup = function()
  require('core/startup')
end
