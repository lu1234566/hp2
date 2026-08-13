#include "hp2/ue_animation_hp2.h"

#include "hp2/ue_actor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hp2 {
namespace {

constexpr std::int32_t kMaxBones = 4096;
constexpr std::int32_t kMaxMoves = 4096;
constexpr std::int32_t kMaxTracks = 8192;
constexpr std::int32_t kMaxNotifications = 1'000'000;
constexpr std::size_t kMaxPoolEntries = 40'000'000u;

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::size_t Remaining() const { return bytes_.size() - position_; }

    std::uint8_t U8() {
        Require(1u, "byte");
        return bytes_[position_++];
    }

    std::uint16_t U16() {
        const std::uint16_t low = U8();
        return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(U8()) << 8u));
    }

    std::int16_t I16() { return static_cast<std::int16_t>(U16()); }

    std::uint32_t U32() {
        std::uint32_t value = 0u;
        for (std::uint32_t shift = 0u; shift < 32u; shift += 8u) {
            value |= static_cast<std::uint32_t>(U8()) << shift;
        }
        return value;
    }

    std::int32_t I32() { return static_cast<std::int32_t>(U32()); }

    float F32() {
        const std::uint32_t raw = U32();
        float value = 0.0f;
        static_assert(sizeof(value) == sizeof(raw));
        std::memcpy(&value, &raw, sizeof(value));
        if (!std::isfinite(value)) {
            throw std::runtime_error("animation stream contains a non-finite float");
        }
        return value;
    }

    std::int32_t CompactIndex() {
        const std::uint8_t first = U8();
        const bool negative = (first & 0x80u) != 0u;
        std::uint32_t value = first & 0x3fu;
        bool more = (first & 0x40u) != 0u;
        std::uint32_t shift = 6u;
        for (int byte_index = 1; more; ++byte_index) {
            if (byte_index >= 5 || shift >= 32u) {
                throw std::runtime_error("animation compact index exceeds 32 bits");
            }
            const std::uint8_t byte = U8();
            const std::uint32_t payload = byte & 0x7fu;
            if (shift == 27u && payload > 0x0fu) {
                throw std::runtime_error("animation compact index overflows 32 bits");
            }
            value |= payload << shift;
            shift += 7u;
            more = (byte & 0x80u) != 0u;
        }
        if (value > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("animation compact index is outside signed range");
        }
        const auto signed_value = static_cast<std::int32_t>(value);
        return negative ? -signed_value : signed_value;
    }

private:
    void Require(std::size_t count, const char* description) const {
        if (count > bytes_.size() - position_) {
            throw std::runtime_error(std::string("unexpected end while reading animation ")
                                     + description);
        }
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0u;
};

std::int32_t Count(Reader& reader, std::int32_t limit, const char* description) {
    const std::int32_t value = reader.CompactIndex();
    if (value < 0 || value > limit) {
        throw std::runtime_error(std::string(description) + " count is outside the supported range");
    }
    return value;
}

void AddPoolCount(std::size_t& total, std::int32_t count, const char* description) {
    const std::size_t amount = static_cast<std::size_t>(count);
    if (amount > kMaxPoolEntries - total) {
        throw std::runtime_error(std::string(description) + " pool is too large");
    }
    total += amount;
}

std::string NameAt(const PackageIndex& package, std::int32_t index, const char* description) {
    if (index < 0 || static_cast<std::size_t>(index) >= package.names.size()) {
        throw std::runtime_error(std::string(description) + " name index is outside the name table");
    }
    return package.names[static_cast<std::size_t>(index)].value;
}

Vec3 ReadVec3(Reader& reader) {
    return {reader.F32(), reader.F32(), reader.F32()};
}

struct PackedVector {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

struct TrackDescriptor {
    std::size_t move_index = 0u;
    std::size_t track_index = 0u;
    std::size_t quaternion_count = 0u;
    std::size_t position_count = 0u;
    std::size_t delta_count = 0u;
    bool root_track = false;
};

std::vector<std::uint8_t> ReadPayload(
    const PackageIndex& package,
    const ExportEntry& entry
) {
    if (entry.serial_size <= 0 || entry.serial_offset < 0) {
        throw std::runtime_error("animation export has no serialized payload");
    }
    std::ifstream input(package.summary.path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open animation package payload");
    }
    input.seekg(entry.serial_offset, std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek to animation package payload");
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.serial_size));
    input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
        throw std::runtime_error("animation package payload is truncated");
    }
    return payload;
}

std::filesystem::path FindPackage(
    const std::filesystem::path& root,
    const std::string& package_name
) {
    const std::string wanted = Lowercase(package_name);
    std::error_code error_code;
    if (!std::filesystem::is_directory(root, error_code)) return {};
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(root, options, error_code);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (error_code) {
            error_code.clear();
            iterator.increment(error_code);
            continue;
        }
        if (iterator->is_regular_file(error_code) && !error_code
            && IsPackageExtension(iterator->path())
            && Lowercase(iterator->path().stem().string()) == wanted) {
            return iterator->path();
        }
        iterator.increment(error_code);
    }
    return {};
}

std::size_t FindAnimationExport(
    const PackageIndex& package,
    const std::string& object_name
) {
    const std::string wanted = Lowercase(object_name);
    for (std::size_t index = 0; index < package.exports.size(); ++index) {
        const ExportEntry& entry = package.exports[index];
        if (entry.serial_size > 0 && Lowercase(entry.class_name) == "animation"
            && Lowercase(entry.object_name) == wanted) {
            return index;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

}  // namespace

HP2AnimationData ParseHP2AnimationNative(
    const PackageIndex& package,
    const std::string& object_name,
    const std::vector<std::uint8_t>& native_bytes
) {
    HP2AnimationData result;
    result.package_path = package.summary.path;
    result.package_name = package.summary.path.stem().string();
    result.object_name = object_name;
    result.file_version = package.summary.file_version;
    if (!package.valid) {
        result.error = package.error.empty() ? "animation package index is invalid" : package.error;
        return result;
    }
    if (package.summary.file_version < 62 || package.summary.file_version >= 100) {
        result.error = "HP2 animation reader currently supports UE1 versions 62-99";
        return result;
    }
    if (native_bytes.empty()) {
        result.error = "animation native payload is empty";
        return result;
    }

    try {
        Reader reader(native_bytes);
        const std::int32_t bone_count = Count(reader, kMaxBones, "Animation.RefBones");
        result.bones.reserve(static_cast<std::size_t>(bone_count));
        for (std::int32_t index = 0; index < bone_count; ++index) {
            HP2AnimationBone bone;
            bone.name = NameAt(package, reader.CompactIndex(), "Animation.RefBones");
            bone.flags = reader.U32();
            bone.parent_index = reader.I32();
            if (bone.parent_index < 0 || bone.parent_index >= bone_count) {
                throw std::runtime_error("animation bone parent is outside the skeleton");
            }
            result.bones.push_back(std::move(bone));
        }

        const std::int32_t move_count = Count(reader, kMaxMoves, "Animation.Moves");
        result.moves.reserve(static_cast<std::size_t>(move_count));
        std::vector<TrackDescriptor> descriptors;
        std::size_t requested_quaternions = 0u;
        std::size_t requested_positions = 0u;
        std::size_t requested_deltas = 0u;
        for (std::int32_t move_index = 0; move_index < move_count; ++move_index) {
            HP2AnimationMove move;
            move.root_speed = ReadVec3(reader);
            move.track_time = reader.F32();
            move.start_bone = reader.I32();
            move.flags = reader.U32();

            const std::int32_t bone_index_count = Count(
                reader, kMaxTracks, "Animation.Move.BoneIndices"
            );
            move.bone_indices.reserve(static_cast<std::size_t>(bone_index_count));
            for (std::int32_t index = 0; index < bone_index_count; ++index) {
                const std::int32_t bone_index = reader.I32();
                if (bone_index < 0 || bone_index >= bone_count) {
                    throw std::runtime_error("animation move bone index is outside the skeleton");
                }
                move.bone_indices.push_back(bone_index);
            }

            const std::int32_t track_count = Count(
                reader, kMaxTracks, "Animation.Move.Tracks"
            );
            move.tracks.reserve(static_cast<std::size_t>(track_count));
            for (std::int32_t track_index = 0; track_index < track_count; ++track_index) {
                HP2AnimationTrack track;
                track.flags = reader.U32();
                const std::int32_t quaternion_count = Count(
                    reader, static_cast<std::int32_t>(kMaxPoolEntries),
                    "Animation.Track.Quaternions"
                );
                const std::int32_t position_count = Count(
                    reader, static_cast<std::int32_t>(kMaxPoolEntries),
                    "Animation.Track.Positions"
                );
                const std::int32_t delta_count = Count(
                    reader, static_cast<std::int32_t>(kMaxPoolEntries),
                    "Animation.Track.Deltas"
                );
                if (!((quaternion_count == 0 || quaternion_count == 1
                       || quaternion_count == delta_count)
                      && (position_count == 0 || position_count == 1
                          || position_count == delta_count))) {
                    throw std::runtime_error("animation track key counts have an unsupported shape");
                }
                track.position_scale = reader.F32();
                track.time_scale = reader.F32();
                if (track.time_scale < 0.0f) {
                    throw std::runtime_error("animation track has a negative time scale");
                }
                track.delta_count = static_cast<std::size_t>(delta_count);
                AddPoolCount(requested_quaternions, quaternion_count, "quaternion");
                AddPoolCount(requested_positions, position_count, "position");
                AddPoolCount(requested_deltas, delta_count, "delta");
                descriptors.push_back({
                    static_cast<std::size_t>(move_index),
                    static_cast<std::size_t>(track_index),
                    static_cast<std::size_t>(quaternion_count),
                    static_cast<std::size_t>(position_count),
                    static_cast<std::size_t>(delta_count),
                    track_index == 0,
                });
                move.tracks.push_back(std::move(track));
            }
            if (!move.bone_indices.empty() && move.bone_indices.size() != move.tracks.size()) {
                throw std::runtime_error("animation move bone-index and track counts differ");
            }
            result.total_track_count += move.tracks.size();
            result.moves.push_back(std::move(move));
        }

        const std::int32_t sequence_count = Count(reader, kMaxMoves, "Animation.Sequences");
        result.sequences.reserve(static_cast<std::size_t>(sequence_count));
        for (std::int32_t index = 0; index < sequence_count; ++index) {
            SkeletalAnimationSequence sequence;
            sequence.name = NameAt(package, reader.CompactIndex(), "Animation.Sequence.Name");
            sequence.group = NameAt(package, reader.CompactIndex(), "Animation.Sequence.Group");
            sequence.start_frame = reader.I32();
            sequence.frame_count = reader.I32();
            const std::int32_t notify_count = Count(
                reader, kMaxNotifications, "Animation.Sequence.Notifies"
            );
            sequence.notify_count = static_cast<std::size_t>(notify_count);
            for (std::int32_t notify = 0; notify < notify_count; ++notify) {
                reader.F32();
                NameAt(package, reader.CompactIndex(), "Animation.Sequence.Notify");
            }
            sequence.rate = reader.F32();
            result.sequences.push_back(std::move(sequence));
        }

        const std::int32_t quaternion_pool_count = Count(
            reader, static_cast<std::int32_t>(kMaxPoolEntries),
            "Animation.MasterQuaternions"
        );
        if (static_cast<std::size_t>(quaternion_pool_count) != requested_quaternions) {
            throw std::runtime_error("master quaternion pool does not match track descriptors");
        }
        std::vector<PackedVector> packed_quaternions;
        packed_quaternions.reserve(requested_quaternions);
        for (std::int32_t index = 0; index < quaternion_pool_count; ++index) {
            packed_quaternions.push_back({reader.I16(), reader.I16(), reader.I16()});
        }

        const std::int32_t position_pool_count = Count(
            reader, static_cast<std::int32_t>(kMaxPoolEntries),
            "Animation.MasterPositions"
        );
        if (static_cast<std::size_t>(position_pool_count) != requested_positions) {
            throw std::runtime_error("master position pool does not match track descriptors");
        }
        std::vector<PackedVector> packed_positions;
        packed_positions.reserve(requested_positions);
        for (std::int32_t index = 0; index < position_pool_count; ++index) {
            packed_positions.push_back({reader.I16(), reader.I16(), reader.I16()});
        }

        const std::int32_t delta_pool_count = Count(
            reader, static_cast<std::int32_t>(kMaxPoolEntries),
            "Animation.MasterDeltas"
        );
        if (static_cast<std::size_t>(delta_pool_count) != requested_deltas) {
            throw std::runtime_error("master delta pool does not match track descriptors");
        }
        std::vector<std::uint8_t> packed_deltas;
        packed_deltas.reserve(requested_deltas);
        for (std::int32_t index = 0; index < delta_pool_count; ++index) {
            packed_deltas.push_back(reader.U8());
        }

        result.master_quaternion_count = packed_quaternions.size();
        result.master_position_count = packed_positions.size();
        result.master_delta_count = packed_deltas.size();
        result.remaining_bytes = reader.Remaining();
        if (result.remaining_bytes != 0u) {
            throw std::runtime_error("animation native stream has unconsumed bytes");
        }

        std::size_t quaternion_cursor = 0u;
        std::size_t position_cursor = 0u;
        std::size_t delta_cursor = 0u;
        for (const TrackDescriptor& descriptor : descriptors) {
            HP2AnimationTrack& track = result.moves[descriptor.move_index].tracks[
                descriptor.track_index
            ];
            const std::vector<std::uint8_t> deltas(
                packed_deltas.begin() + static_cast<std::ptrdiff_t>(delta_cursor),
                packed_deltas.begin() + static_cast<std::ptrdiff_t>(
                    delta_cursor + descriptor.delta_count
                )
            );
            std::vector<float> times = DecodeHP2KeyDeltas(deltas);
            for (float& value : times) {
                value *= track.time_scale;
                if (!std::isfinite(value)) {
                    throw std::runtime_error("decoded animation key time is non-finite");
                }
            }
            auto key_time = [&](std::size_t key_index, std::size_t key_count) {
                return key_count <= 1u ? 0.0f : times[key_index];
            };

            track.keys.rotations.reserve(descriptor.quaternion_count);
            for (std::size_t key_index = 0; key_index < descriptor.quaternion_count; ++key_index) {
                const PackedVector& packed = packed_quaternions[quaternion_cursor++];
                track.keys.rotations.push_back({
                    key_time(key_index, descriptor.quaternion_count),
                    DecodeHP2PackedQuaternion(
                        packed.x, packed.y, packed.z, descriptor.root_track
                    ),
                });
            }
            track.keys.positions.reserve(descriptor.position_count);
            for (std::size_t key_index = 0; key_index < descriptor.position_count; ++key_index) {
                const PackedVector& packed = packed_positions[position_cursor++];
                track.keys.positions.push_back({
                    key_time(key_index, descriptor.position_count),
                    DecodeHP2PackedPosition(
                        packed.x, packed.y, packed.z, track.position_scale
                    ),
                });
            }
            delta_cursor += descriptor.delta_count;
        }
        if (quaternion_cursor != packed_quaternions.size()
            || position_cursor != packed_positions.size()
            || delta_cursor != packed_deltas.size()) {
            throw std::runtime_error("animation pool distribution did not close exactly");
        }
        result.valid = true;
    } catch (const std::exception& exception) {
        result.valid = false;
        result.error = exception.what();
    }
    return result;
}

HP2AnimationData LoadHP2AnimationExport(
    const PackageIndex& package,
    std::size_t export_index
) {
    HP2AnimationData result;
    result.package_path = package.summary.path;
    result.package_name = package.summary.path.stem().string();
    result.file_version = package.summary.file_version;
    if (!package.valid) {
        result.error = package.error.empty() ? "animation package index is invalid" : package.error;
        return result;
    }
    if (export_index >= package.exports.size()) {
        result.error = "animation export index is outside the export table";
        return result;
    }
    const ExportEntry& entry = package.exports[export_index];
    result.object_name = entry.object_name;
    if (Lowercase(entry.class_name) != "animation") {
        result.error = "selected export is not an Animation";
        return result;
    }
    try {
        const ObjectProperties properties = LoadObjectProperties(package, export_index);
        if (!properties.valid) {
            throw std::runtime_error(properties.error.empty()
                                     ? "animation property stream is invalid" : properties.error);
        }
        const std::vector<std::uint8_t> payload = ReadPayload(package, entry);
        if (properties.native_data_offset > payload.size()) {
            throw std::runtime_error("animation native-data offset is outside its payload");
        }
        const std::vector<std::uint8_t> native_bytes(
            payload.begin() + static_cast<std::ptrdiff_t>(properties.native_data_offset),
            payload.end()
        );
        return ParseHP2AnimationNative(package, entry.object_name, native_bytes);
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    }
}

HP2AnimationData LoadNamedHP2Animation(
    const std::filesystem::path& game_root,
    const std::string& package_name,
    const std::string& object_name
) {
    HP2AnimationData result;
    const std::filesystem::path package_path = FindPackage(game_root, package_name);
    if (package_path.empty()) {
        result.error = "requested animation package was not found";
        return result;
    }
    const PackageIndex package = LoadPackageIndex(package_path);
    if (!package.valid) {
        result.error = package.error.empty() ? "requested animation package is invalid"
                                             : package.error;
        return result;
    }
    const std::size_t export_index = FindAnimationExport(package, object_name);
    if (export_index == std::numeric_limits<std::size_t>::max()) {
        result.error = "requested Animation export was not found";
        return result;
    }
    return LoadHP2AnimationExport(package, export_index);
}

}  // namespace hp2
