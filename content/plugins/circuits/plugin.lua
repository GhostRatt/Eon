-- Circuits: ready-wired electrical kits, and the settings of the parts they use.
-- (Example plugin: eon.add + eon.link wire things; eon.register_schema adds inspector settings.)

local function at(ctx, dx, dy, dz)
  local x, y, z = ctx.cursor[1], ctx.cursor[2], ctx.cursor[3]
  return {x + dx, y + dy, z + dz}
end

eon.register_tool{
  name = "Lamp + switch + battery (wired)",
  help = "A 12 V battery, a rocker switch and a lamp on a small board at the cursor, wired in a loop.",
  run = function(ctx)
    local board = eon.add{type = "static", name = "Board", pos = at(ctx, 0, 0.01, 0), size = {0.5, 0.02, 0.3}, color = {150, 120, 80}}
    local bat = eon.add{type = "battery", name = "KitBattery", pos = at(ctx, -0.15, 0.095, 0), size = {0.06, 0.15, 0.1}, color = {60, 60, 60},
                        params = {voltage = 12, internalR = 0.05, capacityAh = 7}}
    local sw = eon.add{type = "switch", name = "KitSwitch", pos = at(ctx, 0.02, 0.06, 0.08), size = {0.08, 0.02, 0.12}, color = {245, 245, 245},
                       params = {axis = {0, 1, 0}}}
    local lamp = eon.add{type = "lamp", name = "KitLamp", pos = at(ctx, 0.17, 0.07, 0), size = {0.06, 0.1, 0.06}, color = {250, 240, 200},
                         params = {ratedPower = 5, ratedVoltage = 12}}
    eon.link(bat, 0, sw, 0)
    eon.link(sw, 1, lamp, 0)
    eon.link(lamp, 1, bat, 1)
    eon.select{bat, sw, lamp}
  end
}

eon.register_tool{
  name = "Doorbell (button + lamp)",
  help = "A wall button that lights a lamp while it's held.",
  run = function(ctx)
    local bat = eon.add{type = "battery", name = "BellBattery", pos = at(ctx, 0, 1.0, 0), size = {0.06, 0.15, 0.1}, params = {voltage = 12, internalR = 0.05, capacityAh = 7, hidden = true}}
    local b = eon.add{type = "button", name = "BellButton", pos = at(ctx, 0.3, 1.2, 0), size = {0.05, 0.05, 0.02}, color = {200, 60, 60}, params = {axis = {0, 0, 1}}}
    local l = eon.add{type = "lamp", name = "BellLamp", pos = at(ctx, 0.3, 1.6, 0), size = {0.06, 0.1, 0.06}, params = {ratedPower = 5, ratedVoltage = 12}}
    eon.link(bat, 0, b, 0); eon.link(b, 1, l, 0); eon.link(l, 1, bat, 1)
  end
}

eon.register_part{label = "Battery 1.5V AA", category = "Electric", type = "battery", size = {0.014, 0.05, 0.014}, mass = 0.023,
                  color = {200, 160, 40}, params = {voltage = 1.5, internalR = 0.15, capacityAh = 2.5}, help = "Loose AA cell."}
