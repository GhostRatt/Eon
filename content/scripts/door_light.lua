-- Fridge light: a relay that is closed while the door is open. params: door (name of the door)
local door
function on_start(self) door = world.find(self:param("door", "")) end
function on_tick(self, dt) if door then self:set_relay(not door:shut()) end end
