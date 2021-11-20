--- gamepad
-- @module gamepad

local tab = require 'tabutil'
local hid_events = require "hid_events"

gamepad = {}

-- NB: lots of gamepads like to use their own codes, different from what appears in core/hid_events.lua
gamepad.model = require 'gamepad_model/index'

--- button states
gamepad.state = {}

local debug_level = 1
-- cache to prevent spamming when val=0 (origin)

local prev_dir = {
  X = 0,
  Y = 0,
  Z = 0,
  RZ = 0,
}
local prev_dir_v = {
  X = 0,
  Y = 0,
  Z = 0,
  RZ = 0,
}


-- clear callbacks
function gamepad.clear()
  gamepad.dpad = function() end
  gamepad.apad = function() end
  gamepad.button = function() end
end

--- states shortcuts
function gamepad.up()
  return gamepad.state.UP end
function gamepad.down()
  return gamepad.state.DOWN end
function gamepad.left()
  return gamepad.state.LEFT end
function gamepad.right()
  return gamepad.state.RIGHT end


function gamepad.direction_event_code_type_to_axis(evt)
  if evt == 'ABS_HAT0Y' or evt == 'ABS_Y' then
    return 'Y'
  elseif evt == 'ABS_HAT0X' or evt == 'ABS_X' then
    return 'X'
  elseif evt == 'ABS_RY' then
    return 'RY'
  elseif evt == 'ABS_RX' then
    return 'RX'
  elseif evt == 'ABS_Z' then
    return 'Z'
  elseif evt == 'ABS_RZ' then
    return 'RZ'
  end
end

function gamepad.is_direction_event_code_analog(evt)
  return tab.contains({'ABS_Y', 'ABS_X',
                       'ABS_RY', 'ABS_RX',
                       'ABS_Z', 'ABS_RZ',}, evt)
end

function gamepad.axis_2_states(axis)
  local mapping = {
    Y = {'DOWN', 'UP'},
    X = {'LEFT', 'RIGHT'},
    RZ = {'DOWN2', 'UP2'},
    Z = {'LEFT2', 'RIGHT2'},
  }
  return mapping[axis]
end

function gamepad.register_direction_state(dev_name, axis, sign, do_log_event)
  local states = gamepad.axis_2_states(axis)
  local s1 = states[1]
  local s2 = states[2]

  if states then
    if sign == 0 then
      gamepad.state[s1] = false
      gamepad.state[s2] = false
    else
      if gamepad.model[dev_name].dpad_invert[axis] then
        sign = sign * - 1
      end
      if sign > 0 then
        if do_log_event and debug_level >= 1 then print(s1) end
        gamepad.state[s1] = true
        gamepad.state[s2] = false
      else
        if do_log_event and debug_level >= 1 then print(s2) end
        gamepad.state[s1] = false
        gamepad.state[s2] = true
      end
    end
  end
end


function gamepad.process(dev_name,typ,code,val)

  local event_code_type
  for k, v in pairs(hid_events.types) do
    if tonumber(v) == typ then
      event_code_type = k
      break
    end
  end

  local do_log_event = gamepad.is_loggable_event(dev_name, event_code_type, code, val)

  if do_log_event and debug_level >= 2 then
    local keycode = gamepad.code_2_keycode(event_code_type, code)
    local msg = "hid.event" .."\t".. " type: "..typ .."\t".. " code: ".. code .."\t".. " value: "..val
    if keycode then
      msg = msg .."\t".. " keycode: "..keycode
    end
    print(msg)
  end

  local event_key
  if event_code_type == "EV_ABS" then
    local axis_evt = gamepad.code_2_keycode(event_code_type, code)
    local axis = gamepad.direction_event_code_type_to_axis(axis_evt)

    local sign = val

    if axis then
      local is_analog = gamepad.is_direction_event_code_analog(axis_evt)

      if is_analog then
        local reso = gamepad.model[dev_name].dpad_resolution
        local half_reso = (reso/2)

        if gamepad.is_analog_origin(dev_name, val) then
          val = 0
        else
          val = val - half_reso
        end

        if val ~= prev_dir_v[axis] then
          prev_dir_v[axis] = val
          if gamepad.apad then gamepad.apad(axis, val) end
        end

        -- analog value count as a direction change IIF value > 2/3 of resolution
        if val <= half_reso * 2/3 and val >= - half_reso * 2/3 then
          sign = 0
        else
          sign = val < 0 and -1 or 1
        end
      else -- digital
        if sign ~= 0 then
          sign = val < 0 and -1 or 1
        end
      end

      gamepad.register_direction_state(dev_name, axis, sign, do_log_event)

      if sign ~= prev_dir[axis] then
        prev_dir[axis] = sign
        -- menu dpad
        if _menu.mode then _menu.dpad(axis, sign)
          -- script dpad
        elseif gamepad.dpad then gamepad.dpad(axis, sign) end
      else
      end

    end
  end

  -- TODO: handle relative axes

  local button_name
  if event_code_type == "EV_KEY" then
    button_name = gamepad.code_2_button(dev_name,code)
    if button_name then

      if do_log_event and debug_level >= 1 then print(button_name) end

      gamepad.state[button_name] = val

      -- menu button
      if _menu.mode then _menu.button(button_name, val)
        -- script button
      elseif gamepad.button then gamepad.button(button_name, val) end
    end
  end
end

--- Predicate that returns true only on non-reset values (i.e. on key/joystick presses)
function gamepad.is_loggable_event(dev_name,event_code_type,code,val)
  if (event_code_type == "EV_KEY" and val == 1) then
    return true
  end
  if event_code_type == "EV_ABS" then
    local axis_evt = gamepad.code_2_keycode(event_code_type, code)
    if gamepad.is_direction_event_code_analog(axis_evt) then
      return not gamepad.is_analog_origin(dev_name,val)
    else
      return (val ~= 0)
    end
  end
end

--- Returns true if value for axis is around origin
-- i.e. when joystick / d-pad is not actioned
function gamepad.is_analog_origin(dev_name,value)
  local resolution = gamepad.model[dev_name].dpad_resolution
  local noize_margin = gamepad.model[dev_name].dpad_o_margin
  return ( value >= ((resolution/2) - noize_margin) and value <= ((resolution/2) + noize_margin))
end

--- Returns button name associated w/ key code
function gamepad.code_2_button(dev_name,code)
  local code_2_button = tab.invert(gamepad.model[dev_name].button)
  return code_2_button[code]
end

--- Returns event key name associated w/ key code
function gamepad.code_2_keycode(event_code_type, code)
  for k, v in pairs(hid_events.codes) do
    if tonumber(v) == code then
      if util.string_starts(k, gamepad.event_code_type_2_key_prfx(event_code_type)) then
        return k
      end
    end
  end
end

function gamepad.event_code_type_2_key_prfx(event_code_type)
  return string.sub(event_code_type, -3)
end



return gamepad
