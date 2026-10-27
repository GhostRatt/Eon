-- Thermostat: put this on a "relay" wired in series with a heater.
-- params: setpoint (°C, default 22), band (°C, default 1), sensor (name of the object it measures at; default itself)
local sensor
function on_start(self)
  sensor = world.find(self:param("sensor", self:name())) or self
end
function on_tick(self, dt)
  local x, y, z = sensor:pos()
  local t = world.air_temp(x, y, z)
  local set, band = self:param("setpoint", 22), self:param("band", 1)
  if t < set - band then self:set_relay(true) elseif t > set + band then self:set_relay(false) end
end
