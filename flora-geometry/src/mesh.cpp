#include "flora/geometry/mesh.hpp"

#include <cmath>

namespace flora {

namespace {

void appendError(std::string* errors, bool& valid, const char* message) {
    valid = false;
    if (errors != nullptr) {
        errors->append(message);
        errors->push_back('\n');
    }
}

} // namespace

bool validateMesh(const Mesh& mesh, std::string* errors) {
    bool valid = true;

    if (mesh.normals.size() != mesh.positions.size() || mesh.uvs.size() != mesh.positions.size()) {
        appendError(errors, valid, "vertex attribute arrays must have equal sizes");
    }
    if (mesh.indices.size() % 3 != 0) {
        appendError(errors, valid, "index count must be a multiple of three");
    }

    for (const Vec3& position : mesh.positions) {
        if (!position.isFinite()) {
            appendError(errors, valid, "position must be finite");
            break;
        }
    }
    for (const Vec3& normal : mesh.normals) {
        if (!normal.isFinite()) {
            appendError(errors, valid, "normal must be finite");
            break;
        }
        const float lengthSquared = normal.lengthSquared();
        if (lengthSquared < 0.98f || lengthSquared > 1.02f) {
            appendError(errors, valid, "normal must be unit length");
            break;
        }
    }
    for (const Vec2& uv : mesh.uvs) {
        if (!uv.isFinite()) {
            appendError(errors, valid, "uv must be finite");
            break;
        }
    }
    for (const std::uint32_t index : mesh.indices) {
        if (index >= mesh.positions.size()) {
            appendError(errors, valid, "index out of range");
            break;
        }
    }

    return valid;
}

bool computeBounds(const Mesh& mesh, Vec3& minimum, Vec3& maximum) {
    if (mesh.positions.empty()) {
        return false;
    }
    minimum = maximum = mesh.positions.front();
    for (const Vec3& position : mesh.positions) {
        minimum.x = std::fmin(minimum.x, position.x);
        minimum.y = std::fmin(minimum.y, position.y);
        minimum.z = std::fmin(minimum.z, position.z);
        maximum.x = std::fmax(maximum.x, position.x);
        maximum.y = std::fmax(maximum.y, position.y);
        maximum.z = std::fmax(maximum.z, position.z);
    }
    return true;
}

} // namespace flora
