-- Furniture: quick room furnishing.
eon.register_tool{
  name = "Table and two chairs",
  help = "At the cursor.",
  run = function(ctx)
    local x, y, z = ctx.cursor[1], ctx.cursor[2], ctx.cursor[3]
    eon.add{type = "static", name = "Table", pos = {x, y + 0.37, z}, size = {1.0, 0.74, 0.6}, color = {140, 100, 70}}
    eon.add{type = "static", name = "Chair", pos = {x, y + 0.23, z + 0.65}, size = {0.45, 0.46, 0.45}, color = {110, 80, 60}}
    eon.add{type = "static", name = "Chair", pos = {x, y + 0.23, z - 0.65}, size = {0.45, 0.46, 0.45}, color = {110, 80, 60}}
  end
}
eon.register_part{label = "Bed", category = "Build", type = "static", size = {1.0, 0.5, 2.0}, color = {120, 150, 190}, params = {material = "soft"}, help = "Soft: she can sit and lie on it."}
eon.register_part{label = "Chair", category = "Build", type = "static", size = {0.45, 0.46, 0.45}, color = {110, 80, 60}, help = "Seat height 46 cm."}
