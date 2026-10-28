-- Fences: posts and rails, built along a line or around the cursor.
-- (Example plugin: eon.add puts things in the map, eon.ground finds the terrain height.)

local wood = {120, 88, 60}

local function post(x, z, h)
  local g = eon.ground(x, z) or 0
  return eon.add{type = "static", name = "FencePost", pos = {x, g + h / 2, z}, size = {0.08, h, 0.08}, color = wood}
end

local function rail(x1, z1, x2, z2, y)
  local dx, dz = x2 - x1, z2 - z1
  local len = math.sqrt(dx * dx + dz * dz)
  local g1, g2 = eon.ground(x1, z1) or 0, eon.ground(x2, z2) or 0
  local yaw = math.deg(math.atan(dx, dz))
  eon.add{type = "static", name = "FenceRail", pos = {(x1 + x2) / 2, (g1 + g2) / 2 + y, (z1 + z2) / 2}, rot = {0, yaw, 0},
          size = {0.04, 0.08, len}, color = wood}
end

eon.register_tool{
  name = "Fence ring around the cursor",
  help = "A round fence (8 m across) centred where the mouse is.",
  run = function(ctx)
    local cx, cz = ctx.cursor[1], ctx.cursor[3]
    local r, n = 4.0, 20
    local px, pz
    for i = 0, n do
      local a = i / n * math.pi * 2
      local x, z = cx + math.cos(a) * r, cz + math.sin(a) * r
      if i < n then post(x, z, 1.1) end
      if px then rail(px, pz, x, z, 0.45); rail(px, pz, x, z, 0.9) end
      px, pz = x, z
    end
    eon.log("fence ring built")
  end
}

eon.register_tool{
  name = "Fence between two selected things",
  help = "Select two things (Ctrl+click): a straight fence runs between them.",
  run = function(ctx)
    local s = ctx.selection
    if #s < 2 then eon.log("select two things first"); return end
    local a, b = eon.get(s[1]), eon.get(s[2])
    local x1, z1, x2, z2 = a.pos[1], a.pos[3], b.pos[1], b.pos[3]
    local len = math.sqrt((x2 - x1) ^ 2 + (z2 - z1) ^ 2)
    local n = math.max(1, math.floor(len / 2.0 + 0.5))
    local px, pz
    for i = 0, n do
      local x, z = x1 + (x2 - x1) * i / n, z1 + (z2 - z1) * i / n
      post(x, z, 1.1)
      if px then rail(px, pz, x, z, 0.45); rail(px, pz, x, z, 0.9) end
      px, pz = x, z
    end
  end
}

eon.register_part{label = "Fence post", category = "Build", type = "static", size = {0.08, 1.1, 0.08}, color = wood, help = "One post."}
