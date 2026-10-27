-- Vending machine logic, on the machine's push "button": each press drops one item from the chute.
-- params: chute (name of an object marking where items fall out), stock (default 5), item ("apple" or "bread"),
--         lamp (optional name of an object that glows while it's working)
local stock
local items = {
  apple = {size = {0.075, 0.075, 0.075}, mass = 0.15, color = {190, 40, 40}, params = {grams = 150, kcalPerG = 0.52, waterPerG = 0.85}},
  bread = {size = {0.12, 0.05, 0.08}, mass = 0.08, color = {200, 160, 100}, params = {grams = 80, kcalPerG = 2.6, waterPerG = 0.35}},
}
function on_start(self) stock = self:param("stock", 5) end
function on_press(self)
  if stock <= 0 then print(self:name() .. ": empty") return end
  local lamp = world.find(self:param("lamp", ""))
  if lamp then lamp:set_glow(1.0) end
  after(0.6, function()
    local chute = world.find(self:param("chute", "")) or self
    local x, y, z = chute:pos()
    local kind = self:param("item", "apple")
    local spec = items[kind]
    world.spawn("food", x, y, z, {name = kind, size = spec.size, mass = spec.mass, color = spec.color, params = spec.params})
    stock = stock - 1
    if lamp then lamp:set_glow(0) end
  end)
end
