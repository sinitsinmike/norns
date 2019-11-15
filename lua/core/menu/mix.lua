local m = {
  sel = 1
}

m.key = function(n,z)
  if n==2 and z==1 and m.sel > 1 then
    m.sel = m.sel - 1
  elseif n==3 and z==1 and m.sel < 3 then
    m.sel = m.sel + 1
  end
end

m.enc = function(n,d)
  local ch1 = {"output", "monitor", "softcut"}
  local ch2 = {"input", "engine", "tape"}

  if n==2 then
    mix:delta(ch1[m.sel],d)
  elseif n==3 then
    mix:delta(ch2[m.sel],d)
  end
end

m.redraw = function()
  local n
  screen.clear()
  screen.aa(1)
  screen.line_width(1)

  menu.draw_panel()

  local x = -40
  screen.level(2)
  n = mix:get_raw("output")*48
  screen.rect(x+42.5,55.5,2,-n)
  screen.stroke()

  screen.level(15)
  n = m.out1/64*48
  screen.rect(x+48.5,55.5,2,-n)
  screen.stroke()

  n = m.out2/64*48
  screen.rect(x+54.5,55.5,2,-n)
  screen.stroke()

  screen.level(2)
  n = mix:get_raw("input")*48
  screen.rect(x+64.5,55.5,2,-n)
  screen.stroke()

  screen.level(15)
  n = m.in1/64*48
  screen.rect(x+70.5,55.5,2,-n)
  screen.stroke()
  n = m.in2/64*48
  screen.rect(x+76.5,55.5,2,-n)
  screen.stroke()

  screen.level(2)
  n = mix:get_raw("monitor")*48
  screen.rect(x+86.5,55.5,2,-n)
  screen.stroke()

  screen.level(2)
  n = mix:get_raw("engine")*48
  screen.rect(x+108.5,55.5,2,-n)
  screen.stroke()

  screen.level(2)
  n = mix:get_raw("softcut")*48
  screen.rect(x+130.5,55.5,2,-n)
  screen.stroke()

  screen.level(2)
  n = mix:get_raw("tape")*48
  screen.rect(x+152.5,55.5,2,-n)
  screen.stroke()

  screen.level(m.sel==1 and 15 or 1)
  screen.move(2,63)
  screen.text("out")
  screen.move(24,63)
  screen.text("in")
  screen.level(m.sel==2 and 15 or 1)
  screen.move(46,63)
  screen.text("mon")
  screen.move(68,63)
  screen.text("eng")
  screen.level(m.sel==3 and 15 or 1)
  screen.move(90,63)
  screen.text("cut")
  screen.move(112,63)
  screen.text("tp")

  screen.update()
end

m.init = function()
  norns.vu = m.vu
  m.in1 = 0
  m.in2 = 0
  m.out1 = 0
  m.out2 = 0
  norns.encoders.set_accel(2,true)
  norns.encoders.set_sens(2,1)
  norns.encoders.set_sens(3,1)
end

m.deinit = function()
  norns.encoders.set_accel(2,false)
  norns.encoders.set_sens(2,2)
  norns.encoders.set_sens(3,2)
  norns.vu = norns.none
end

m.vu = function(in1,in2,out1,out2)
  m.in1 = in1
  m.in2 = in2
  m.out1 = out1
  m.out2 = out2
  menu.redraw()
end

return m
