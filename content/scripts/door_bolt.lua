-- Door bolt: put this on a door; it is locked while the bolt switch is flipped on. params: bolt (name of the switch/lever)
local bolt
function on_start(self) bolt = world.find(self:param("bolt", "")) end
function on_tick(self, dt) if bolt then self:set_locked(bolt:closed()) end end
