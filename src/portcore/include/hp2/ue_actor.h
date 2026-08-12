#pragma once

#include "hp2/ue_model.h"
#include "hp2/ue_package.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hp2 {

struct Rotator {
    std::int32_t pitch = 0;
    std::int32_t yaw = 0;
    std::int32_t roll = 0;
};

struct SerializedProperty {
    std::string name;
    std::string struct_name;
    std::uint8_t type = 0;
    std::int32_t array_index = 0;
    bool bool_value = false;
    std::vector<std::uint8_t> bytes;
};

struct ObjectProperties {
    bool valid = false;
    bool has_state_frame = false;
    std::size_t native_data_offset = 0;
    std::vector<SerializedProperty> properties;
    std::string error;
};

struct ActorInstance {
    std::int32_t object_reference = 0;
    std::int32_t class_reference = 0;
    std::size_t export_index = 0;
    std::string object_name;
    std::string class_name;
    bool properties_valid = false;
    std::size_t property_count = 0;

    bool has_location = false;
    Vec3 location;
    bool has_rotation = false;
    Rotator rotation;
    bool has_pre_pivot = false;
    Vec3 pre_pivot;
    bool has_draw_scale = false;
    float draw_scale = 1.0f;
    bool has_draw_scale_3d = false;
    Vec3 draw_scale_3d{1.0f, 1.0f, 1.0f};
    bool has_hidden = false;
    bool hidden = false;

    std::int32_t mesh_reference = 0;
    std::int32_t static_mesh_reference = 0;
    std::string error;
};

struct ActorClassCount {
    std::string class_name;
    std::size_t count = 0;
};

struct LevelActorCensus {
    bool valid = false;
    std::size_t level_export_index = 0;
    std::string level_object_name;
    std::size_t actor_reference_count = 0;
    std::size_t non_null_actor_references = 0;
    std::size_t parsed_actor_count = 0;
    std::size_t actor_parse_failures = 0;
    std::size_t actors_with_location = 0;
    std::size_t actors_with_rotation = 0;
    std::size_t direct_mesh_references = 0;
    std::size_t direct_static_mesh_references = 0;
    std::vector<std::int32_t> actor_references;
    std::vector<ActorInstance> actors;
    std::vector<ActorClassCount> class_inventory;
    std::string error;
};

ObjectProperties LoadObjectProperties(
    const PackageIndex& package,
    std::size_t export_index
);

ObjectProperties ScanClassDefaultProperties(
    const PackageIndex& package,
    std::size_t export_index,
    std::size_t max_scan_bytes = 64u * 1024u
);

const SerializedProperty* FindObjectProperty(
    const ObjectProperties& object,
    const char* name
);

bool DecodePropertyVec3(const SerializedProperty* property, Vec3& value);
bool DecodePropertyRotator(const SerializedProperty* property, Rotator& value);
bool DecodePropertyFloat(const SerializedProperty* property, float& value);
bool DecodePropertyBool(const SerializedProperty* property, bool& value);
std::int32_t DecodePropertyObjectReference(const SerializedProperty* property);

LevelActorCensus LoadLevelActorCensus(const PackageIndex& package);

}  // namespace hp2
