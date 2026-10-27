# Regenerates content/zones/*.json (Waiting Area, Test Zone 01). Usage: python tools/make_zones.py content/zones
import json, sys
out = sys.argv[1]

def ent(id, name, type, pos, size, color, mass=0, rot=(0,0,0), params=None):
    e = {"id": id, "name": name, "type": type, "pos": list(pos), "rot": list(rot), "size": list(size), "color": list(color), "mass": mass}
    if params: e["params"] = params
    return e

def room(w, d, h, floor_col, wall_col):
    t = 0.1
    return [
        ent(1, "Floor", "static", (0, -t/2, 0), (w, t, d), floor_col),
        ent(2, "Wall_North", "static", (0, h/2, -d/2 - t/2), (w + 2*t, h, t), wall_col),
        ent(3, "Wall_South", "static", (0, h/2, d/2 + t/2), (w + 2*t, h, t), wall_col),
        ent(4, "Wall_West", "static", (-w/2 - t/2, h/2, 0), (t, h, d), wall_col),
        ent(5, "Wall_East", "static", (w/2 + t/2, h/2, 0), (t, h, d), wall_col),
        ent(6, "Ceiling", "static", (0, h + t/2, 0), (w + 2*t, t, d + 2*t), wall_col),
    ]

# Waiting Area: Jenny's home between tests
wa = room(6, 6, 2.6, (150, 140, 128), (205, 200, 190))
wa += [
    ent(10, "Bed", "static", (-2.0, 0.25, -1.9), (1.0, 0.5, 2.0), (120, 150, 190), params={"material": "soft"}),
    ent(11, "Pillow", "static", (-2.0, 0.56, -2.65), (0.6, 0.12, 0.35), (235, 235, 240), mass=0.6, params={"material": "soft"}),
    ent(12, "Table", "static", (1.2, 0.37, -1.6), (1.0, 0.74, 0.6), (140, 100, 70)),
    ent(13, "Chair", "static", (1.2, 0.23, -0.9), (0.45, 0.46, 0.45), (110, 80, 60)),
    # Water: a dispenser that runs while its push button is held, a cup on the drip tray
    ent(14, "WaterDispenser", "static", (2.6, 0.55, -2.6), (0.4, 1.1, 0.4), (90, 150, 200)),
    ent(30, "DispenserNose", "static", (2.6, 0.95, -2.34), (0.08, 0.06, 0.12), (70, 120, 170)),
    ent(31, "DispenserSpout", "tap", (2.6, 0.91, -2.31), (0.03, 0.02, 0.03), (200, 210, 220), params={"valve": 32, "rate": 45.0, "speed": 0.3}),
    ent(32, "DispenserButton", "button", (2.48, 1.0, -2.385), (0.05, 0.05, 0.02), (40, 90, 160), params={"axis": [0, 0, 1], "travel": 0.006, "force": 3.0}),
    ent(33, "DripTray", "static", (2.6, 0.5, -2.3), (0.3, 0.04, 0.16), (60, 60, 70)),
    ent(34, "Cup", "container", (2.6, 0.571, -2.3), (0.075, 0.1, 0.075), (235, 235, 230), mass=0.15, params={"capacity": 300.0}),
    # Toilet: a bowl she can sit on, with a flush lever on the tank
    ent(15, "Toilet", "bowl", (-2.6, 0.2, 2.5), (0.4, 0.4, 0.6), (240, 240, 240), params={"capacity": 6000.0, "drain": 36}),
    ent(35, "ToiletTank", "static", (-2.6, 0.6, 2.88), (0.4, 0.4, 0.16), (235, 235, 235)),
    ent(36, "FlushLever", "lever", (-2.44, 0.7, 2.79), (0.04, 0.08, 0.04), (180, 180, 185),
        params={"axis": [0, 0, -1], "handle": 0.12, "throw": 0.5, "detent": 0.25, "leverMass": 0.1}),
    # Food: apples in a bowl on the table
    ent(37, "FruitBowl", "static", (1.0, 0.765, -1.6), (0.3, 0.05, 0.3), (200, 170, 120)),
    ent(38, "Apple1", "food", (0.93, 0.83, -1.62), (0.075, 0.075, 0.075), (190, 40, 40), mass=0.15, params={"grams": 150.0, "kcalPerG": 0.52, "waterPerG": 0.85}),
    ent(39, "Apple2", "food", (1.06, 0.83, -1.56), (0.075, 0.075, 0.075), (120, 170, 50), mass=0.15, params={"grams": 150.0, "kcalPerG": 0.52, "waterPerG": 0.85}),
    ent(40, "Bread", "food", (1.0, 0.83, -1.7), (0.12, 0.05, 0.08), (200, 160, 100), mass=0.08, params={"grams": 80.0, "kcalPerG": 2.6, "waterPerG": 0.35}),
    # Snacks: a vending machine object (content/objects/vending_machine.json): push its button, an apple drops into the tray
    ent(60, "Vending", "prefab", (-0.4, 0.0, -2.69), (0.1, 0.1, 0.1), (0, 0, 0), params={"prefab": "vending_machine"}),
    # Fridge object: pull its door open (magnetic seal), 4 °C inside, light comes on; an apple and a bottle of water inside
    ent(61, "Fridge", "prefab", (0.35, 0.0, -2.7), (0.1, 0.1, 0.1), (0, 0, 0), params={"prefab": "fridge"}),
    # Heat: a wall heater on its own battery, switched by the rocker beside it
    ent(41, "Heater", "heater", (-2.93, 0.35, 0.0), (0.12, 0.45, 0.7), (150, 110, 90), params={"ratedPower": 150.0, "ratedVoltage": 12.0}),
    ent(42, "HeaterBattery", "battery", (-2.93, 0.9, 0.6), (0.06, 0.15, 0.1), (60, 60, 60), params={"voltage": 12.0, "internalR": 0.03, "capacityAh": 40.0}),
    ent(43, "HeaterSwitch", "switch", (-2.96, 1.1, 0.45), (0.02, 0.12, 0.08), (245, 245, 245), params={"axis": [1, 0, 0]}),
    ent(44, "HeaterWireA", "wire", (-2.99, 0.9, 0.5), (0.01, 0.01, 0.3), (180, 60, 40), params={"length": 0.4, "hidden": True}),
    ent(45, "HeaterWireB", "wire", (-2.99, 0.7, 0.2), (0.01, 0.01, 0.4), (180, 60, 40), params={"length": 0.6, "hidden": True}),
    ent(46, "HeaterWireC", "wire", (-2.99, 0.6, 0.4), (0.01, 0.01, 0.4), (40, 40, 40), params={"length": 0.6, "hidden": True}),
    # Shower: a head in the corner, a valve lever on the wall, a tray that drains while it runs
    ent(47, "ShowerTray", "bowl", (2.5, 0.02, 2.5), (0.9, 0.04, 0.9), (210, 215, 220), params={"capacity": 20000.0, "drain": 49}),
    ent(48, "ShowerHead", "tap", (2.5, 2.1, 2.5), (0.12, 0.03, 0.12), (190, 190, 195), params={"valve": 49, "rate": 120.0, "speed": 0.4, "jets": 9, "spread": 0.35}),
    ent(49, "ShowerValve", "lever", (2.97, 1.2, 2.05), (0.04, 0.12, 0.08), (170, 170, 175),
        params={"axis": [-1, 0, 0], "handle": 0.18, "throw": 0.6, "detent": 0.6, "leverMass": 0.2}),
    ent(16, "Door_Test", "static", (0, 1.0, -3.04), (1.0, 2.0, 0.06), (100, 100, 110)),
    # Light circuit: battery in wall box -> wall switch -> ceiling lamp
    ent(20, "WallBattery_12V", "battery", (2.93, 1.0, 1.6), (0.06, 0.15, 0.1), (60, 60, 60),
        params={"voltage": 12.0, "internalR": 0.05, "capacityAh": 7.0}),
    ent(21, "WallSwitch", "switch", (2.96, 1.2, 1.0), (0.02, 0.12, 0.08), (245, 245, 245), params={"axis": [-1, 0, 0]}),
    ent(22, "CeilingLamp", "lamp", (0, 2.5, 0), (0.3, 0.1, 0.3), (255, 240, 200),
        params={"ratedPower": 10.0, "ratedVoltage": 12.0}),
    ent(23, "WireA", "wire", (2.94, 1.1, 1.3), (0.01, 0.01, 0.6), (180, 60, 40), params={"length": 0.6, "hidden": True}),
    ent(24, "WireB", "wire", (1.5, 2.55, 0.5), (3.0, 0.01, 0.01), (180, 60, 40), params={"length": 4.0, "hidden": True}),
    ent(25, "WireC", "wire", (1.5, 2.56, 0.8), (3.0, 0.01, 0.01), (40, 40, 40), params={"length": 5.0, "hidden": True}),
]
wa_links = [[20, 0, 23, 0], [23, 1, 21, 0], [21, 1, 24, 0], [24, 1, 22, 0], [22, 1, 25, 0], [25, 1, 20, 1],
            [42, 0, 44, 0], [44, 1, 43, 0], [43, 1, 45, 0], [45, 1, 41, 0], [41, 1, 46, 0], [46, 1, 42, 1]]

# Test Zone 01: a table with a battery, a button, a lamp, and spare parts
tz = room(5, 5, 2.8, (110, 115, 120), (170, 175, 180))
tz += [
    ent(10, "TestTable", "static", (0, 0.37, 0), (1.4, 0.74, 0.8), (200, 200, 200)),
    ent(20, "Battery_9V", "battery", (-0.4, 0.765, 0.1), (0.026, 0.048, 0.017), (30, 30, 30), mass=0.045,
        params={"voltage": 9.0, "internalR": 1.5, "capacityAh": 0.5}),
    ent(21, "BigButton", "button", (0, 0.765, 0.15), (0.08, 0.05, 0.08), (200, 30, 30)),   # screwed to the table
    ent(22, "BulbOnStand", "lamp", (0.4, 0.8, 0.1), (0.06, 0.12, 0.06), (250, 250, 220), mass=0.1,
        params={"ratedPower": 1.0, "ratedVoltage": 9.0}),
    ent(23, "Wire_Red", "wire", (-0.2, 0.745, 0.15), (0.4, 0.006, 0.006), (200, 40, 40), mass=0.01, params={"length": 0.4}),
    ent(24, "Wire_Black1", "wire", (0.2, 0.745, 0.15), (0.4, 0.006, 0.006), (30, 30, 30), mass=0.01, params={"length": 0.4}),
    ent(25, "Wire_Black2", "wire", (0, 0.745, -0.05), (0.8, 0.006, 0.006), (30, 30, 30), mass=0.01, params={"length": 1.0}),
    ent(30, "SpareBattery_9V", "battery", (0.55, 0.765, 0.15), (0.026, 0.048, 0.017), (30, 30, 30), mass=0.045,
        params={"voltage": 9.0, "internalR": 1.5, "capacityAh": 0.5, "charge": 0.2}),
    # Room light: hidden battery -> switch by the door -> ceiling lamp
    ent(40, "RoomBattery_12V", "battery", (2.43, 1.0, 2.2), (0.06, 0.15, 0.1), (60, 60, 60),
        params={"voltage": 12.0, "internalR": 0.05, "capacityAh": 7.0}),
    ent(41, "RoomSwitch", "switch", (2.46, 1.2, 1.8), (0.02, 0.12, 0.08), (245, 245, 245), params={"closed": True, "axis": [-1, 0, 0]}),
    ent(42, "RoomLamp", "lamp", (0, 2.7, 0), (0.4, 0.1, 0.4), (255, 245, 220), params={"ratedPower": 15.0, "ratedVoltage": 12.0}),
    ent(43, "RoomWireA", "wire", (2.44, 1.1, 2.0), (0.01, 0.01, 0.4), (180, 60, 40), params={"length": 0.4, "hidden": True}),
    ent(44, "RoomWireB", "wire", (1.2, 2.74, 0.9), (2.4, 0.01, 0.01), (180, 60, 40), params={"length": 3.5, "hidden": True}),
    ent(45, "RoomWireC", "wire", (1.2, 2.75, 1.1), (2.4, 0.01, 0.01), (40, 40, 40), params={"length": 4.5, "hidden": True}),
    ent(31, "SpareWire", "wire", (-0.5, 0.745, -0.25), (0.3, 0.006, 0.006), (40, 120, 200), mass=0.008, params={"length": 0.3}),
    # A pull lever on the west wall that lights a lamp above it (hidden battery behind the wall)
    ent(50, "LeverBattery_12V", "battery", (-2.43, 0.6, 0.5), (0.06, 0.15, 0.1), (60, 60, 60),
        params={"voltage": 12.0, "internalR": 0.05, "capacityAh": 7.0, "hidden": True}),
    ent(51, "WallLever", "lever", (-2.47, 1.15, 0.8), (0.04, 0.16, 0.1), (90, 90, 95),
        params={"axis": [1, 0, 0], "handle": 0.3, "throw": 0.6, "detent": 1.0, "leverMass": 0.4}),
    ent(52, "LeverLamp", "lamp", (-2.44, 1.9, 0.8), (0.08, 0.1, 0.08), (255, 230, 180), params={"ratedPower": 5.0, "ratedVoltage": 12.0}),
    ent(53, "LeverWireA", "wire", (-2.56, 0.9, 0.65), (0.01, 0.4, 0.01), (180, 60, 40), params={"length": 0.6, "hidden": True}),
    ent(54, "LeverWireB", "wire", (-2.56, 1.5, 0.8), (0.01, 0.6, 0.01), (180, 60, 40), params={"length": 0.8, "hidden": True}),
    ent(55, "LeverWireC", "wire", (-2.57, 1.2, 0.6), (0.01, 1.2, 0.01), (40, 40, 40), params={"length": 1.8, "hidden": True}),
    # Things to carry: a small crate (4 kg) and a light box
    ent(60, "Crate", "dynamic", (-1.2, 0.15, -1.6), (0.4, 0.3, 0.3), (150, 110, 70), mass=4.0),
    ent(61, "ShoeBox", "dynamic", (1.4, 0.06, -1.7), (0.3, 0.12, 0.18), (200, 190, 170), mass=0.6),
]
tz_links = [[20, 0, 23, 0], [23, 1, 21, 0], [21, 1, 24, 0], [24, 1, 22, 0], [22, 1, 25, 0], [25, 1, 20, 1],
            [40, 0, 43, 0], [43, 1, 41, 0], [41, 1, 44, 0], [44, 1, 42, 0], [42, 1, 45, 0], [45, 1, 40, 1],
            [50, 0, 53, 0], [53, 1, 51, 0], [51, 1, 54, 0], [54, 1, 52, 0], [52, 1, 55, 0], [55, 1, 50, 1]]

for fname, name, ents, links in [("waiting_area.json", "Waiting Area", wa, wa_links), ("test_zone_01.json", "Test Zone 01", tz, tz_links)]:
    with open(f"{out}/{fname}", "w") as f:
        json.dump({"zone": name, "ambientC": 22.0, "entities": ents, "links": links}, f, indent=2)
        f.write("\n")
print("ok")
