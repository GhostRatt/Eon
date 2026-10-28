#!/usr/bin/env python3
"""Writes the stock objects (prefabs) into content/objects. Hand-made objects there are left alone.
Usage: python3 tools/make_objects.py [content/objects]
Part positions are relative to the object's origin on the floor (y up, front = +z)."""
import json
import os
import sys

WOOD = (150, 110, 75)
DARK_WOOD = (105, 75, 50)
WHITE = (238, 238, 236)
STEEL = (180, 182, 188)
FABRIC = (90, 110, 140)
BAT = (60, 60, 60)


def box(name, typ, pos, size, color, mass=None, rot=None, **params):
    p = {"name": name, "type": typ, "pos": [round(v, 4) for v in pos], "size": [round(v, 4) for v in size], "color": list(color)}
    if mass:
        p["mass"] = mass
    if rot:
        p["rot"] = list(rot)
    if params:
        p["params"] = params
    return p


def legs(prefix, half_x, half_z, height, thick, color, typ="static", mass=None, **params):
    out = []
    for i, (sx, sz) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        out.append(box(f"{prefix}{i + 1}", typ, (sx * half_x, height / 2, sz * half_z), (thick, height, thick), color, mass, **params))
    return out


def circuit(bat, sw, load):
    return [[bat, 0, sw, 0], [sw, 1, load, 0], [load, 1, bat, 1]]


OBJECTS = {}


def obj(name, category, description, parts, links=()):
    OBJECTS[name] = {"name": name, "category": category, "description": description, "parts": parts, "links": [list(l) for l in links]}


# ---------------- furniture ----------------
obj("chair", "Furniture", "Wooden chair (7 kg): one piece, she can drag or carry it, sit on it, tip it over.",
    [box("Seat", "dynamic", (0, 0.45, 0), (0.44, 0.04, 0.42), WOOD, 2.5)]
    + legs("Leg", 0.19, 0.18, 0.43, 0.035, WOOD, "dynamic", 0.4, weld="$Seat")
    + [box("Back", "dynamic", (0, 0.72, -0.195), (0.44, 0.5, 0.03), WOOD, 1.4, weld="$Seat")])
obj("stool", "Furniture", "Kitchen stool (3 kg), one piece.",
    [box("Seat", "dynamic", (0, 0.62, 0), (0.32, 0.04, 0.32), DARK_WOOD, 1.2)]
    + legs("Leg", 0.13, 0.13, 0.6, 0.03, DARK_WOOD, "dynamic", 0.3, weld="$Seat")
    + [box("Ring", "dynamic", (0, 0.22, 0.13), (0.26, 0.02, 0.02), DARK_WOOD, 0.2, weld="$Seat")])
obj("table", "Furniture", "Wooden table, 1.2 x 0.7 m (18 kg): heavy, but she can shove it.",
    [box("Top", "dynamic", (0, 0.735, 0), (1.2, 0.04, 0.7), WOOD, 12)]
    + legs("Leg", 0.55, 0.3, 0.715, 0.06, WOOD, "dynamic", 1.5, weld="$Top"))
obj("desk", "Furniture", "Fixed desk with a side cabinet that has a door.",
    [box("Top", "static", (0, 0.74, 0), (1.3, 0.03, 0.62), WOOD),
     box("SideL", "static", (-0.63, 0.3625, 0), (0.03, 0.725, 0.6), WOOD),
     box("Modesty", "static", (0, 0.45, -0.28), (1.2, 0.5, 0.02), WOOD),
     box("CabSide", "static", (0.2, 0.3625, 0), (0.02, 0.725, 0.6), WOOD),
     box("CabR", "static", (0.63, 0.3625, 0), (0.03, 0.725, 0.6), WOOD),
     box("CabShelf", "static", (0.415, 0.36, 0), (0.41, 0.02, 0.56), WOOD),
     box("CabBottom", "static", (0.415, 0.03, 0), (0.41, 0.06, 0.56), WOOD),
     box("CabDoor", "door", (0.415, 0.39, 0.312), (0.4, 0.66, 0.02), DARK_WOOD, 2.0, hingeSide="right", frame="$CabR", swing=100, seal=0.6, friction=0.2)])
obj("bookshelf", "Furniture", "Bookshelf 0.8 x 1.8 m with books she can take out (0.4 kg each).",
    [box("Back", "static", (0, 0.9, -0.14), (0.8, 1.8, 0.02), DARK_WOOD),
     box("SideL", "static", (-0.39, 0.9, 0), (0.02, 1.8, 0.3), DARK_WOOD),
     box("SideR", "static", (0.39, 0.9, 0), (0.02, 1.8, 0.3), DARK_WOOD),
     box("Top", "static", (0, 1.79, 0), (0.76, 0.02, 0.3), DARK_WOOD),
     box("Bottom", "static", (0, 0.04, 0), (0.76, 0.08, 0.3), DARK_WOOD)]
    + [box(f"Shelf{i + 1}", "static", (0, 0.08 + 0.43 * (i + 1), 0), (0.76, 0.02, 0.28), DARK_WOOD) for i in range(3)]
    + [box(f"Book{i + 1}", "dynamic", (-0.3 + 0.045 * i, 0.08 + 0.43 * (i % 3 + 1) + 0.12, 0.02), (0.035, 0.22, 0.16),
           ((170, 50, 40), (40, 90, 150), (60, 130, 70), (200, 170, 60))[i % 4], 0.4) for i in range(6)])
obj("bed", "Furniture", "Single bed: frame, soft mattress, headboard and a pillow.",
    [box("Frame", "static", (0, 0.15, 0), (0.95, 0.3, 2.0), WOOD),
     box("Mattress", "static", (0, 0.4, 0.0), (0.9, 0.2, 1.95), WHITE, material="soft"),
     box("Headboard", "static", (0, 0.5, -1.0), (0.95, 1.0, 0.05), WOOD),
     box("Pillow", "dynamic", (0, 0.56, -0.75), (0.55, 0.1, 0.32), (245, 245, 250), 0.6, material="soft")])
obj("sofa", "Furniture", "Two-seat sofa (soft).",
    [box("Base", "static", (0, 0.21, 0), (1.6, 0.42, 0.85), FABRIC, material="soft"),
     box("Back", "static", (0, 0.6, -0.33), (1.6, 0.42, 0.2), FABRIC, material="soft"),
     box("ArmL", "static", (-0.73, 0.55, 0.05), (0.14, 0.26, 0.75), FABRIC, material="soft"),
     box("ArmR", "static", (0.73, 0.55, 0.05), (0.14, 0.26, 0.75), FABRIC, material="soft"),
     box("Cushion1", "dynamic", (-0.33, 0.47, 0.06), (0.6, 0.1, 0.6), (110, 130, 160), 0.8, material="soft"),
     box("Cushion2", "dynamic", (0.33, 0.47, 0.06), (0.6, 0.1, 0.6), (110, 130, 160), 0.8, material="soft")])
obj("wardrobe", "Furniture", "Wardrobe 1 x 1.9 m with two hinged doors and a shelf.",
    [box("Back", "static", (0, 0.95, -0.265), (1.0, 1.9, 0.02), WOOD),
     box("SideL", "static", (-0.49, 0.95, 0), (0.02, 1.9, 0.55), WOOD),
     box("SideR", "static", (0.49, 0.95, 0), (0.02, 1.9, 0.55), WOOD),
     box("Top", "static", (0, 1.89, 0), (0.96, 0.02, 0.55), WOOD),
     box("Bottom", "static", (0, 0.04, 0), (0.96, 0.08, 0.55), WOOD),
     box("Shelf", "static", (0, 1.55, 0), (0.96, 0.02, 0.5), WOOD),
     box("Rail", "static", (0, 1.45, 0), (0.94, 0.02, 0.02), STEEL),
     box("DoorL", "door", (-0.247, 0.98, 0.287), (0.49, 1.8, 0.02), DARK_WOOD, 5.0, hingeSide="left", frame="$SideL", swing=110, seal=0.8, friction=0.3),
     box("DoorR", "door", (0.247, 0.98, 0.287), (0.49, 1.8, 0.02), DARK_WOOD, 5.0, hingeSide="right", frame="$SideR", swing=110, seal=0.8, friction=0.3)])

# ---------------- kitchen & bath ----------------
obj("kitchen_cabinet", "Kitchen & bath", "Base cabinet with a worktop and a door; a cup inside.",
    [box("Worktop", "static", (0, 0.89, 0), (0.62, 0.04, 0.6), (200, 195, 185)),
     box("Back", "static", (0, 0.44, -0.27), (0.6, 0.86, 0.02), WHITE),
     box("SideL", "static", (-0.29, 0.44, 0), (0.02, 0.86, 0.56), WHITE),
     box("SideR", "static", (0.29, 0.44, 0), (0.02, 0.86, 0.56), WHITE),
     box("Bottom", "static", (0, 0.05, 0), (0.56, 0.1, 0.56), WHITE),
     box("Shelf", "static", (0, 0.45, 0), (0.56, 0.02, 0.5), WHITE),
     box("Door", "door", (0, 0.49, 0.292), (0.58, 0.76, 0.02), WHITE, 3.0, hingeSide="left", frame="$SideL", swing=105, seal=0.8, friction=0.2),
     box("Cup", "container", (0.1, 0.51, 0.0), (0.075, 0.1, 0.075), WHITE, 0.15, capacity=300)])
obj("kitchen_sink", "Kitchen & bath", "Sink unit: the tap runs while its handle is on; flip the plug switch to let the basin drain.",
    [box("CounterL", "static", (-0.4, 0.45, 0), (0.3, 0.9, 0.6), WHITE),
     box("CounterR", "static", (0.4, 0.45, 0), (0.3, 0.9, 0.6), WHITE),
     box("Under", "static", (0, 0.35, 0.0), (0.5, 0.7, 0.6), WHITE),
     box("Basin", "bowl", (0, 0.8, 0.02), (0.5, 0.2, 0.5), STEEL, capacity=15000, drain="#Plug"),
     box("Splash", "static", (0, 1.0, -0.29), (1.1, 0.2, 0.02), (200, 210, 215)),
     box("Spout", "tap", (0, 1.05, -0.12), (0.03, 0.03, 0.18), STEEL, valve="#Handle", rate=100, speed=0.4),
     box("Handle", "switch", (0.12, 0.915, -0.24), (0.04, 0.03, 0.06), STEEL, axis=[0, 1, 0]),
     box("Plug", "switch", (-0.12, 0.915, -0.24), (0.04, 0.03, 0.06), (60, 60, 60), axis=[0, 1, 0])])
obj("bathtub", "Kitchen & bath", "Bathtub (150 l): the tap lever fills it, the plug switch empties it.",
    [box("Tub", "bowl", (0, 0.27, 0), (0.75, 0.54, 1.7), WHITE, capacity=150000, drain="#Plug"),
     box("Spout", "tap", (0, 0.72, -0.78), (0.04, 0.04, 0.12), STEEL, valve="#Lever", rate=250, speed=0.6),
     box("WallPlate", "static", (0, 0.75, -0.87), (0.4, 0.2, 0.02), (200, 210, 215)),
     box("Lever", "lever", (0.12, 0.75, -0.85), (0.04, 0.08, 0.04), STEEL, axis=[0, 0, 1], handle=0.08, throw=0.6, detent=0.3, leverMass=0.05),
     box("Plug", "switch", (-0.12, 0.75, -0.85), (0.05, 0.07, 0.02), (60, 60, 60), axis=[0, 0, 1])])
obj("toilet", "Kitchen & bath", "Toilet: bowl with a cistern; press the flush button to empty the bowl.",
    [box("Bowl", "bowl", (0, 0.2, 0.05), (0.38, 0.4, 0.55), WHITE, capacity=6000, water=1500, drain="#Flush"),
     box("Seat", "static", (0, 0.415, 0.06), (0.38, 0.03, 0.46), WHITE),
     box("Cistern", "static", (0, 0.6, -0.26), (0.4, 0.4, 0.16), WHITE),
     box("Flush", "button", (0.12, 0.7, -0.165), (0.05, 0.05, 0.02), STEEL, axis=[0, 0, 1])])
obj("stove", "Kitchen & bath", "Electric hob on a cabinet: the knob switch heats the plate (1 kW, wired in the wall).",
    [box("Body", "static", (0, 0.43, 0), (0.6, 0.86, 0.6), WHITE),
     box("Hob", "heater", (0, 0.875, 0.02), (0.22, 0.03, 0.22), (40, 40, 42), ratedPower=1000, ratedVoltage=230),
     box("Knob", "switch", (0.2, 0.78, 0.31), (0.06, 0.06, 0.02), (40, 40, 42), axis=[0, 0, 1]),
     box("Supply", "battery", (0, 0.4, 0), (0.1, 0.1, 0.1), BAT, voltage=230, internalR=0.5, capacityAh=500)],
    circuit("Supply", "Knob", "Hob"))

# ---------------- gadgets ----------------
obj("desk_lamp", "Gadgets", "Desk lamp with a switch on its base (battery inside the base).",
    [box("Base", "static", (0, 0.025, 0), (0.16, 0.05, 0.16), (40, 40, 45)),
     box("Cell", "battery", (0, 0.025, 0), (0.05, 0.03, 0.05), BAT, voltage=12, internalR=0.1, capacityAh=10),
     box("Stem", "static", (0, 0.22, -0.04), (0.02, 0.34, 0.02), (40, 40, 45)),
     box("Shade", "static", (0, 0.4, 0.0), (0.12, 0.05, 0.12), (40, 40, 45)),
     box("Bulb", "lamp", (0, 0.36, 0.0), (0.05, 0.04, 0.05), (250, 240, 200), ratedPower=5, ratedVoltage=12),
     box("Switch", "switch", (0.05, 0.0575, 0.04), (0.05, 0.015, 0.04), (220, 220, 220), axis=[0, 1, 0])],
    circuit("Cell", "Switch", "Bulb"))
obj("room_light", "Gadgets", "Wall light switch and a ceiling light 2.5 m up, wired inside the wall. Put it against a wall.",
    [box("Plate", "switch", (0, 1.2, 0.0), (0.08, 0.12, 0.02), WHITE, axis=[0, 0, 1]),
     box("Mains", "battery", (0, 1.0, -0.04), (0.06, 0.15, 0.06), BAT, voltage=12, internalR=0.05, capacityAh=50),
     box("Light", "lamp", (0, 2.47, 0.8), (0.2, 0.06, 0.2), (250, 245, 225), ratedPower=40, ratedVoltage=12)],
    circuit("Mains", "Plate", "Light"))
obj("desk_fan", "Gadgets", "Battery fan: a switch on the base runs the motor.",
    [box("Base", "static", (0, 0.03, 0), (0.18, 0.06, 0.14), (230, 230, 235)),
     box("Cell", "battery", (0, 0.03, 0), (0.05, 0.04, 0.05), BAT, voltage=12, internalR=0.2, capacityAh=5),
     box("Neck", "static", (0, 0.17, -0.02), (0.03, 0.22, 0.03), (230, 230, 235)),
     box("Motor", "motor", (0, 0.3, 0.0), (0.08, 0.08, 0.1), (200, 200, 205), resistance=6, k=0.02),
     box("Switch", "switch", (0.05, 0.0675, 0.03), (0.05, 0.015, 0.04), (90, 90, 95), axis=[0, 1, 0])],
    circuit("Cell", "Switch", "Motor"))
obj("space_heater", "Gadgets", "Plug-in heater (500 W): switch on, it warms the air around it.",
    [box("Panel", "heater", (0, 0.35, 0), (0.6, 0.55, 0.1), (230, 225, 215), ratedPower=500, ratedVoltage=230),
     box("Feet", "static", (0, 0.03, 0), (0.6, 0.06, 0.25), (80, 80, 85)),
     box("Mains", "battery", (0, 0.03, 0), (0.05, 0.04, 0.05), BAT, voltage=230, internalR=1, capacityAh=200),
     box("Switch", "switch", (0.24, 0.55, 0.0575), (0.05, 0.06, 0.015), (60, 60, 60), axis=[0, 0, 1])],
    circuit("Mains", "Switch", "Panel"))
obj("doorbell", "Gadgets", "Bell push by a door: while it's held a chime light inside flashes on.",
    [box("Push", "button", (0, 1.3, 0.0), (0.04, 0.04, 0.02), WHITE, axis=[0, 0, 1]),
     box("Cell", "battery", (0, 1.3, -0.05), (0.03, 0.05, 0.03), BAT, voltage=9, internalR=1, capacityAh=0.5),
     box("Chime", "lamp", (0, 2.1, -0.02), (0.08, 0.08, 0.04), (230, 220, 160), ratedPower=2, ratedVoltage=9)],
    circuit("Cell", "Push", "Chime"))

# ---------------- building ----------------
obj("wall_doorway", "Building", "2 m wall with a doorway (fits room_door).",
    [box("Left", "static", (-0.76, 1.3, 0), (0.48, 2.6, 0.1), (205, 200, 190)),
     box("Right", "static", (0.76, 1.3, 0), (0.48, 2.6, 0.1), (205, 200, 190)),
     box("Over", "static", (0, 2.36, 0), (1.04, 0.48, 0.1), (205, 200, 190))])
obj("wall_window", "Building", "2 m wall with a 1 x 1 m window opening.",
    [box("Left", "static", (-0.75, 1.3, 0), (0.5, 2.6, 0.1), (205, 200, 190)),
     box("Right", "static", (0.75, 1.3, 0), (0.5, 2.6, 0.1), (205, 200, 190)),
     box("Below", "static", (0, 0.45, 0), (1.0, 0.9, 0.1), (205, 200, 190)),
     box("Above", "static", (0, 2.25, 0), (1.0, 0.7, 0.1), (205, 200, 190)),
     box("Sill", "static", (0, 0.91, 0.04), (1.04, 0.03, 0.18), WHITE)])
obj("stairs", "Building", "Straight stairs up 1.36 m (8 steps of 17 cm), going toward -z.",
    [box(f"Step{i + 1}", "static", (0, 0.085 * (i + 1), -0.28 * i), (1.0, 0.17 * (i + 1), 0.28), (170, 160, 150)) for i in range(8)])
obj("railing", "Building", "2 m handrail on posts, 1 m high.",
    [box(f"Post{i + 1}", "static", (-1 + i, 0.5, 0), (0.05, 1.0, 0.05), STEEL) for i in range(3)]
    + [box("Rail", "static", (0, 1.0, 0), (2.05, 0.05, 0.06), STEEL)])

# ---------------- outdoor ----------------
obj("bench", "Outdoor", "Park bench (wooden slats on two stone feet).",
    [box("FootL", "static", (-0.65, 0.2, 0), (0.1, 0.4, 0.4), (130, 128, 122)),
     box("FootR", "static", (0.65, 0.2, 0), (0.1, 0.4, 0.4), (130, 128, 122))]
    + [box(f"Slat{i + 1}", "static", (0, 0.42, -0.13 + 0.09 * i), (1.6, 0.04, 0.07), WOOD) for i in range(4)]
    + [box("Back", "static", (0, 0.75, -0.2), (1.6, 0.25, 0.04), WOOD, rot=(-10, 0, 0))])
obj("picnic_table", "Outdoor", "Picnic table with a bench on each side.",
    [box("Top", "static", (0, 0.74, 0), (1.8, 0.05, 0.75), WOOD)]
    + legs("Leg", 0.75, 0.3, 0.715, 0.08, WOOD)
    + [box("BenchA", "static", (0, 0.44, 0.65), (1.8, 0.05, 0.3), WOOD), box("BenchB", "static", (0, 0.44, -0.65), (1.8, 0.05, 0.3), WOOD)]
    + [box(f"BenchLeg{i + 1}", "static", (sx * 0.75, 0.21, sz * 0.65), (0.08, 0.42, 0.08), WOOD) for i, (sx, sz) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1)))])
obj("fence_section", "Outdoor", "2 m fence: two posts and two rails. Line them up end to end.",
    [box("PostA", "static", (-1, 0.55, 0), (0.08, 1.1, 0.08), DARK_WOOD),
     box("PostB", "static", (1, 0.55, 0), (0.08, 1.1, 0.08), DARK_WOOD),
     box("RailTop", "static", (0, 0.95, 0), (2.0, 0.08, 0.03), WOOD),
     box("RailLow", "static", (0, 0.45, 0), (2.0, 0.08, 0.03), WOOD)])
obj("gate", "Outdoor", "1 m garden gate between two posts (swings both ways a little, light latch).",
    [box("PostA", "static", (-0.55, 0.6, 0), (0.1, 1.2, 0.1), DARK_WOOD),
     box("PostB", "static", (0.55, 0.6, 0), (0.1, 1.2, 0.1), DARK_WOOD),
     box("Gate", "door", (0, 0.55, 0.0), (0.98, 0.95, 0.04), WOOD, 8.0, hingeSide="left", frame="$PostA", swing=100, seal=1.0, friction=0.4)])
obj("water_trough", "Outdoor", "Stone trough holding 40 l of water.",
    [box("Trough", "bowl", (0, 0.25, 0), (1.2, 0.5, 0.5), (140, 138, 132), capacity=60000, water=40000)])
obj("log_pile", "Outdoor", "Three loose logs (they roll, they float).",
    [box(f"Log{i + 1}", "dynamic", (x, y, 0), (0.2, 0.2, 1.4), (110, 80, 55), 18.0) for i, (x, y) in enumerate(((-0.11, 0.1), (0.11, 0.1), (0.0, 0.3)))])
obj("crate_stack", "Outdoor", "Three stacked crates (4 kg each) that topple.",
    [box("Crate1", "dynamic", (-0.21, 0.15, 0), (0.4, 0.3, 0.3), (150, 110, 70), 4.0),
     box("Crate2", "dynamic", (0.21, 0.15, 0), (0.4, 0.3, 0.3), (150, 110, 70), 4.0),
     box("Crate3", "dynamic", (0.0, 0.45, 0), (0.4, 0.3, 0.3), (160, 120, 80), 4.0)])
obj("mailbox", "Outdoor", "Mailbox on a post with a front flap (hinged door) and a letter inside.",
    [box("Post", "static", (0, 0.55, 0), (0.08, 1.1, 0.08), DARK_WOOD),
     box("Floor", "static", (0, 1.11, -0.02), (0.25, 0.02, 0.38), (60, 80, 120)),
     box("Roof", "static", (0, 1.29, -0.02), (0.25, 0.02, 0.38), (60, 80, 120)),
     box("SideL", "static", (-0.115, 1.2, -0.02), (0.02, 0.16, 0.38), (60, 80, 120)),
     box("SideR", "static", (0.115, 1.2, -0.02), (0.02, 0.16, 0.38), (60, 80, 120)),
     box("Back", "static", (0, 1.2, -0.2), (0.21, 0.16, 0.02), (60, 80, 120)),
     box("Flap", "door", (0, 1.2, 0.18), (0.22, 0.17, 0.015), (70, 90, 130), 0.4, hingeSide="left", frame="$SideL", swing=110, seal=0.3, friction=0.05),
     box("Letter", "dynamic", (0, 1.125, -0.05), (0.16, 0.01, 0.11), (245, 240, 225), 0.02)])

# ---------------- food ----------------
obj("fruit_bowl", "Food", "Bowl with three apples on it.",
    [box("Bowl", "bowl", (0, 0.03, 0), (0.28, 0.06, 0.28), (210, 180, 130), capacity=1500)]
    + [box(f"Apple{i + 1}", "food", (x, 0.1, z), (0.075, 0.075, 0.075), (190, 40, 40), 0.15, grams=150, kcalPerG=0.52, waterPerG=0.85)
       for i, (x, z) in enumerate(((-0.06, -0.04), (0.06, -0.04), (0.0, 0.06)))])
obj("lunch_tray", "Food", "Tray with a bread roll, an apple and a cup of water.",
    [box("Tray", "dynamic", (0, 0.01, 0), (0.42, 0.02, 0.3), (160, 160, 165), 0.4),
     box("Bread", "food", (-0.1, 0.05, 0), (0.12, 0.05, 0.08), (200, 160, 100), 0.08, grams=80, kcalPerG=2.6, waterPerG=0.35),
     box("Apple", "food", (0.04, 0.06, 0.06), (0.075, 0.075, 0.075), (120, 180, 60), 0.15, grams=150, kcalPerG=0.52, waterPerG=0.85, inside=[245, 240, 200]),
     box("Cup", "container", (0.13, 0.07, -0.06), (0.075, 0.1, 0.075), WHITE, 0.15, capacity=300, water=250)])
obj("water_jug", "Food", "2 l jug of water.",
    [box("Jug", "container", (0, 0.13, 0), (0.12, 0.26, 0.12), (200, 225, 240), 0.2, capacity=2000, water=1800)])


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "content", "objects")
    os.makedirs(out, exist_ok=True)
    for name, o in OBJECTS.items():
        with open(os.path.join(out, name + ".json"), "w") as f:
            f.write("{\n")
            f.write(f'  "name": {json.dumps(o["name"])},\n  "category": {json.dumps(o["category"])},\n  "description": {json.dumps(o["description"])},\n')
            f.write('  "parts": [\n' + ",\n".join("    " + json.dumps(p) for p in o["parts"]) + "\n  ],\n")
            f.write('  "links": ' + json.dumps(o["links"]) + "\n}\n")
    print(f"wrote {len(OBJECTS)} objects to {os.path.normpath(out)}")


if __name__ == "__main__":
    main()
