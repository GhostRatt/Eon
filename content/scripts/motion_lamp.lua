-- Motion sensor: closes its relay while someone is within `range` metres (default 1.5), stays on `hold` seconds after.
local last = -100
function on_tick(self, dt)
  local x, y, z = self:pos()
  if world.agent_near(x, y, z, self:param("range", 1.5)) then last = world.time() end
  self:set_relay(world.time() - last < self:param("hold", 3))
end
