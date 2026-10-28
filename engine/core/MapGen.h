#pragma once
// New map templates (the editor's File > New map): an empty floor, a lit room, or outdoor ground with a pond and plants.
#include <string>

#include "Zone.h"

namespace eon {

struct NewMapSpec {
    std::string name = "new_map", kind = "outdoor";   // outdoor | room | empty
    float sizeX = 40.f, sizeZ = 40.f, wallH = 2.6f;
    int seed = 1;
    float hills = 2.f, feature = 16.f;
    bool pond = true, plants = true;
    float ambientC = 20.f, sun = 1.f;
};
Zone MakeMap(const NewMapSpec& s);

} // namespace eon
