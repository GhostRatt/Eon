-- Blinker / timer: a "relay" that switches on and off on a schedule.
-- params: on_time (s, default 2), off_time (s, default 2)
local on = false
local function flip(self)
  on = not on
  self:set_relay(on)
  after(on and self:param("on_time", 2) or self:param("off_time", 2), flip)
end
function on_start(self) flip(self) end
