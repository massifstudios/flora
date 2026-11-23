// Flora viewer (M8 first cut): grow a plant, orbit around it, inspect debug
// views, scrub growth steps. Rendering is deliberately simple — fixed-function
// OpenGL with client-side arrays — because the viewer is a diagnostic tool,
// not a renderer.
//
//   flora-viewer <seed> <steps> [--species file.json] [--materials file.json]
//                               [--shade raycast|off] [--no-leaves]
//                               [--lod 0..3] [--lod-compare]
//
//   1..5     render mode: solid / orders / light / buds / vigor
//   [ ]      detail level down / up
//   t        split the window into one pane per detail level, one shared camera
//   , .      step back / forward (re-simulates deterministically)
//   c        foliage: twig cards (branchlets unmeshed) or leaf clusters
//   l        hide foliage (bare wood, twigs meshed instead of card-culled)
//   drag     orbit    wheel zoom    right-drag pan
//   escape   quit

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#ifdef FLORA_VIEWER_PNG
#include <png.h>
#endif
#ifdef FLORA_VIEWER_JPEG
#include <jpeglib.h>
#endif

#include "flora/geometry/branch_mesh.hpp"
#include "flora/geometry/foliage_cards.hpp"
#include "flora/geometry/foliage_mesh.hpp"
#include "flora/geometry/lod.hpp"
#include "flora/geometry/material.hpp"
#include "flora/growth/simulation.hpp"
#include "flora/species/species.hpp"
#include "flora/species/species_io.hpp"
#include "flora/structure/foliage.hpp"
#include "flora/version.hpp"

using namespace flora;

namespace {

constexpr float kPi = 3.1415927f;

enum class RenderMode { Solid, Orders, Light, Buds, Vigor };

struct Color {
    float r, g, b;
};

Color orderColor(std::uint16_t order) {
    static const Color palette[] = {
        {0.85f, 0.75f, 0.55f}, {0.90f, 0.55f, 0.25f}, {0.30f, 0.75f, 0.35f},
        {0.25f, 0.55f, 0.95f}, {0.80f, 0.35f, 0.85f}, {0.95f, 0.85f, 0.25f},
        {0.35f, 0.85f, 0.85f}, {0.95f, 0.35f, 0.40f},
    };
    return palette[order % (sizeof(palette) / sizeof(palette[0]))];
}

Color lightColor(float light) {
    const float t = std::clamp(light, 0.0f, 1.0f);
    return {0.15f + 0.85f * t, 0.15f + 0.75f * t, 0.35f * (1.0f - t) + 0.10f};
}

// Textures are diagnostic dressing: colour maps only. Normal and roughness
// maps are carried by the material set for exporters and engines, which is
// where they can actually be shaded; fixed-function OpenGL cannot use them.
struct Image {
    std::vector<unsigned char> pixels;
    unsigned width = 0;
    unsigned height = 0;
    unsigned channels = 0; // 3 = RGB, 4 = RGBA
};

// Texture packs routinely ship a JPEG under a .png name, so the format comes
// from the file's own signature rather than its extension.
bool readSignature(const std::string& path, unsigned char signature[4]) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "texture %s: cannot open\n", path.c_str());
        return false;
    }
    file.read(reinterpret_cast<char*>(signature), 4);
    return file.gcount() == 4;
}

bool loadPng(const std::string& path, Image& image) {
#ifdef FLORA_VIEWER_PNG
    png_image reader{};
    reader.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_file(&reader, path.c_str()) == 0) {
        std::fprintf(stderr, "texture %s: %s\n", path.c_str(), reader.message);
        return false;
    }
    reader.format = PNG_FORMAT_RGBA;
    image.pixels.resize(PNG_IMAGE_SIZE(reader));
    if (png_image_finish_read(&reader, nullptr, image.pixels.data(), 0, nullptr) == 0) {
        std::fprintf(stderr, "texture %s: %s\n", path.c_str(), reader.message);
        png_image_free(&reader);
        return false;
    }
    image.width = reader.width;
    image.height = reader.height;
    image.channels = 4;
    return true;
#else
    std::fprintf(stderr, "texture %s ignored: viewer built without libpng\n", path.c_str());
    return false;
#endif
}

bool loadJpeg(const std::string& path, Image& image) {
#ifdef FLORA_VIEWER_JPEG
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        std::fprintf(stderr, "texture %s: cannot open\n", path.c_str());
        return false;
    }
    jpeg_decompress_struct info{};
    jpeg_error_mgr errors{};
    info.err = jpeg_std_error(&errors);
    jpeg_create_decompress(&info);
    jpeg_stdio_src(&info, file);
    jpeg_read_header(&info, TRUE);
    info.out_color_space = JCS_RGB;
    jpeg_start_decompress(&info);

    image.width = info.output_width;
    image.height = info.output_height;
    image.channels = 3;
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 3);
    const std::size_t stride = static_cast<std::size_t>(image.width) * 3;
    while (info.output_scanline < info.output_height) {
        unsigned char* row = image.pixels.data() + info.output_scanline * stride;
        jpeg_read_scanlines(&info, &row, 1);
    }
    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    std::fclose(file);
    return true;
#else
    std::fprintf(stderr, "texture %s ignored: viewer built without libjpeg\n", path.c_str());
    return false;
#endif
}

// Both decoders hand back the top image row first, while GL samples the first
// uploaded row at v=0. Geometry puts v=0 at a card's attachment point (see
// generateTwigCardMesh), which is where a twig photograph has its stem, so
// uploading unflipped hangs every card by its tip.
void flipRows(Image& image) {
    const std::size_t stride = static_cast<std::size_t>(image.width) * image.channels;
    if (stride == 0 || image.height < 2) {
        return;
    }
    std::vector<unsigned char> row(stride);
    for (unsigned y = 0; y < image.height / 2; ++y) {
        unsigned char* top = image.pixels.data() + static_cast<std::size_t>(y) * stride;
        unsigned char* bottom =
            image.pixels.data() + static_cast<std::size_t>(image.height - 1 - y) * stride;
        std::copy(top, top + stride, row.begin());
        std::copy(bottom, bottom + stride, top);
        std::copy(row.begin(), row.end(), bottom);
    }
}

GLuint uploadTexture(const std::string& path) {
    if (path.empty()) {
        return 0;
    }
    unsigned char signature[4] = {0, 0, 0, 0};
    if (!readSignature(path, signature)) {
        return 0;
    }

    Image image;
    const bool isPng = signature[0] == 0x89 && signature[1] == 'P';
    const bool isJpeg = signature[0] == 0xFF && signature[1] == 0xD8;
    if (isPng) {
        if (!loadPng(path, image)) {
            return 0;
        }
    } else if (isJpeg) {
        if (!loadJpeg(path, image)) {
            return 0;
        }
    } else {
        std::fprintf(stderr, "texture %s: unsupported format (PNG and JPEG only)\n", path.c_str());
        return 0;
    }

    flipRows(image);

    const GLint format = image.channels == 4 ? GL_RGBA : GL_RGB;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format, static_cast<GLsizei>(image.width),
                 static_cast<GLsizei>(image.height), 0, static_cast<GLenum>(format),
                 GL_UNSIGNED_BYTE, image.pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    std::printf("texture: %s (%ux%u, %s)\n", path.c_str(), image.width, image.height,
                isPng ? "png" : "jpeg");
    return texture;
}

// Column-major 4x4 helpers for the fixed-function matrix stack.
void loadOrthographic(float halfWidth, float halfHeight, float nearZ, float farZ) {
    // An impostor is sampled from one bearing at a time and pasted onto a flat
    // quad, so the bake has to be orthographic: perspective would bake the
    // convergence of one particular viewing distance into the texture, and the
    // tree would look wrong at every other distance.
    const float m[16] = {1.0f / halfWidth, 0, 0, 0,
                         0, 1.0f / halfHeight, 0, 0,
                         0, 0, -2.0f / (farZ - nearZ), 0,
                         0, 0, -(farZ + nearZ) / (farZ - nearZ), 1};
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m);
}

void loadPerspective(float fovYRadians, float aspect, float nearZ, float farZ) {
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    const float m[16] = {f / aspect, 0, 0, 0,
                         0, f, 0, 0,
                         0, 0, (farZ + nearZ) / (nearZ - farZ), -1,
                         0, 0, (2 * farZ * nearZ) / (nearZ - farZ), 0};
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m);
}

void loadLookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 forward = (target - eye).normalizedOr({0, 1, 0});
    const Vec3 side = forward.cross(up).normalizedOr({1, 0, 0});
    const Vec3 trueUp = side.cross(forward);
    const float m[16] = {side.x, trueUp.x, -forward.x, 0,
                         side.y, trueUp.y, -forward.y, 0,
                         side.z, trueUp.z, -forward.z, 0,
                         -side.dot(eye), -trueUp.dot(eye), forward.dot(eye), 1};
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
}

struct ViewerState {
    std::uint64_t seed = 42;
    std::uint32_t targetSteps = 30;
    std::uint32_t currentSteps = 0;
    SpeciesDefinition species;
    MaterialSet materials;
    ShadeModel shadeModel = ShadeModel::Raycast;

    Plant plant;
    GrowthContext context;

    bool useCards = true;
    bool showFoliage = true;
    std::vector<TwigCard> cards;
    // Every level is built every rebuild. They come from one structure and one
    // cluster set, so building all four costs a few milliseconds and is what
    // lets them be compared side by side without re-simulating.
    std::array<LodMesh, kLodLevels> lods;
    // The near crown's outline. The impostor is framed and drawn against it, so
    // the bake and the draw cannot disagree about where the tree is.
    CrownEnvelope envelope;
    std::uint32_t lodLevel = 0;
    bool compareLods = false;
    std::vector<Color> barkColors;
    std::vector<Vec3> tipPositions;
    std::vector<FoliageCluster> clusters;

    // Skeleton lines and point sets rebuilt per step/mode.
    std::vector<Vec3> linePositions;
    std::vector<Color> lineColors;
    std::vector<Vec3> pointPositions;
    std::vector<Color> pointColors;

    RenderMode mode = RenderMode::Solid;
    bool structuresDirty = true;
};

void simulateTo(ViewerState& state, std::uint32_t steps) {
    if (steps < state.currentSteps) {
        state.plant = Plant{};
        state.plant.seed = state.seed;
        plantSeedling(state.plant, materializeGrowthParameters(state.species, state.seed, 0));
        state.currentSteps = 0;
    }
    if (steps > state.currentSteps) {
        grow(state.plant, state.species, SkyEnvironment{}, steps - state.currentSteps,
             state.context, state.shadeModel);
        state.currentSteps = steps;
    }
    state.structuresDirty = true;
}

float internodeLightEstimate(const ViewerState& state, std::vector<float>& perInternode) {
    // Per-bud light from the last executed step, aggregated per internode.
    // Buds created during that step are absent from the record; they read 0.
    const auto& budLight = state.context.lastBudLight();
    perInternode.assign(state.plant.graph.internodeCount(), 0.0f);
    float maximum = 0.0f;
    const std::size_t recorded = std::min(budLight.size(), state.plant.graph.budCount());
    for (std::size_t i = 0; i < recorded; ++i) {
        const Bud& bud = state.plant.graph.bud(static_cast<Index>(i));
        if (bud.state == BudState::Dead) {
            continue;
        }
        float& slot = perInternode[bud.internode];
        slot = std::max(slot, budLight[i]);
        maximum = std::max(maximum, slot);
    }
    return maximum;
}

void rebuildStructures(ViewerState& state) {
    const StemGraph& graph = state.plant.graph;
    graph.computeTipPositions(state.tipPositions);

    const GrowthParameters parameters =
        materializeGrowthParameters(state.species, state.seed, state.plant.ageSteps);
    computeFoliagePlacement(graph, parameters, state.tipPositions, state.clusters);

    BranchMeshParameters barkParameters;
    barkParameters.uvLengthScale = state.materials.barkUvLengthScale;
    barkParameters.uvGirthScale = state.materials.barkUvLengthScale;
    for (LodMesh& lod : state.lods) {
        lod = LodMesh{};
    }
    if (!state.showFoliage) {
        // Bare wood: the twigs a card would have covered must be swept, or the
        // branch structure reads as amputated rather than leafless.
        state.cards.clear();
        generateBranchMesh(graph, barkParameters, state.lods[0].bark);
    } else if (state.useCards) {
        const TwigCardParameters cardParameters =
            twigCardParameters(state.materials, parameters, state.seed);
        for (std::uint32_t level = 0; level < kLodLevels; ++level) {
            buildLodMesh(graph, state.clusters, state.tipPositions, level, barkParameters,
                         cardParameters, state.lods[level]);
        }
        // Cards of the level on screen, for the title's card count.
        buildLodCards(graph, state.clusters, state.tipPositions, state.lodLevel, cardParameters,
                      state.cards);
        std::vector<TwigCard> nearCards;
        buildLodCards(graph, state.clusters, state.tipPositions, 0, cardParameters, nearCards);
        state.envelope = measureCrownEnvelope(nearCards);
    } else {
        // Cluster billboards predate the variant atlas and carry no twig, so
        // they all draw with the first foliage material. No LOD ladder here:
        // this path exists to look at where the simulation put its foliage.
        state.cards.clear();
        FoliageMeshParameters leafParameters;
        leafParameters.seed = state.seed;
        generateFoliageMesh(state.clusters, leafParameters, state.lods[0].foliage);
        state.lods[0].sections = {
            {0, 0, static_cast<std::uint32_t>(state.lods[0].foliage.indices.size())}};
        generateBranchMesh(graph, barkParameters, state.lods[0].bark);
    }


    // Skeleton lines with per-mode colors.
    std::vector<float> lightPerInternode;
    float lightMaximum = 0.0f;
    if (state.mode == RenderMode::Light) {
        lightMaximum = internodeLightEstimate(state, lightPerInternode);
    }
    state.linePositions.clear();
    state.lineColors.clear();
    for (Index i = 0; i < static_cast<Index>(graph.internodeCount()); ++i) {
        const Internode& node = graph.internode(i);
        const Vec3 base = node.parent == kInvalidIndex ? Vec3{} : state.tipPositions[node.parent];
        Color color{0.6f, 0.6f, 0.6f};
        switch (state.mode) {
        case RenderMode::Orders:
            color = orderColor(node.branchOrder);
            break;
        case RenderMode::Light: {
            const float normalized =
                lightMaximum > 0.0f ? lightPerInternode[i] / lightMaximum : 0.0f;
            color = lightColor(normalized);
            break;
        }
        case RenderMode::Vigor:
            color = {0.45f, 0.45f, 0.50f}; // leafless wood
            break;
        default:
            break;
        }
        if (!node.alive) {
            color = {0.85f, 0.15f, 0.15f}; // deadwood awaiting decay
        }
        state.linePositions.push_back(base);
        state.linePositions.push_back(state.tipPositions[i]);
        state.lineColors.push_back(color);
        state.lineColors.push_back(color);
    }
    if (state.mode == RenderMode::Vigor) {
        // Foliage-supported internodes: these earn their maintenance.
        for (const FoliageCluster& cluster : state.clusters) {
            state.lineColors[cluster.internode * 2] = {0.20f, 0.85f, 0.30f};
            state.lineColors[cluster.internode * 2 + 1] = {0.20f, 0.85f, 0.30f};
        }
    }

    // Bud points.
    state.pointPositions.clear();
    state.pointColors.clear();
    if (state.mode == RenderMode::Buds) {
        for (const Bud& bud : graph.buds()) {
            if (bud.state == BudState::Dead) {
                continue;
            }
            state.pointPositions.push_back(state.tipPositions[bud.internode]);
            state.pointColors.push_back(bud.state == BudState::Active
                                            ? Color{0.2f, 1.0f, 0.3f}
                                            : Color{1.0f, 0.85f, 0.2f});
        }
    }

    // Solid-mode vertex colors: bark brown, foliage green (drawn separately).
    std::size_t barkVertices = 0;
    for (const LodMesh& lod : state.lods) {
        barkVertices = std::max(barkVertices, lod.bark.vertexCount());
    }
    state.barkColors.assign(barkVertices, Color{0.45f, 0.33f, 0.22f});
    state.structuresDirty = false;
}

void drawMesh(const Mesh& mesh, const std::vector<Color>& colors, GLuint texture = 0,
              std::uint32_t firstIndex = 0, std::uint32_t indexCount = 0xFFFFFFFFu) {
    if (mesh.vertexCount() == 0) {
        return;
    }
    if (firstIndex >= mesh.indices.size()) {
        return;
    }
    const std::uint32_t drawCount =
        std::min(indexCount, static_cast<std::uint32_t>(mesh.indices.size()) - firstIndex);
    if (drawCount == 0) {
        return;
    }
    if (texture != 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, mesh.uvs.data());
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, mesh.positions.data());
    glNormalPointer(GL_FLOAT, 0, mesh.normals.data());
    if (!colors.empty()) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(3, GL_FLOAT, 0, colors.data());
    }
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(drawCount), GL_UNSIGNED_INT,
                   mesh.indices.data() + firstIndex);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// Cards are cut-outs: the texture's alpha decides the silhouette, so they draw
// with an alpha test rather than blending (order-independent, depth-correct).
// One pass per section, since each carries its own photographed twig.
void drawFoliage(const Mesh& mesh, const std::vector<MeshSection>& sections, Color untexturedTint,
                 const std::vector<GLuint>& textures, float alphaCutoff) {
    if (mesh.vertexCount() == 0) {
        return;
    }
    static const std::vector<Color> noColors;
    for (const MeshSection& section : sections) {
        const GLuint texture = section.material < textures.size() ? textures[section.material] : 0;
        const Color tint = texture != 0 ? Color{1.0f, 1.0f, 1.0f} : untexturedTint;
        glColor3f(tint.r, tint.g, tint.b);
        if (texture != 0) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, alphaCutoff);
        }
        drawMesh(mesh, noColors, texture, section.firstIndex, section.indexCount);
        glDisable(GL_ALPHA_TEST);
    }
}

// Framebuffer-object entry points, fetched rather than linked: the viewer runs
// on a compatibility context and SDL's GL header only promises 1.1 prototypes.
// Everything else the bake needs is fixed-function and already there.
struct FramebufferApi {
    PFNGLGENFRAMEBUFFERSPROC genFramebuffers = nullptr;
    PFNGLBINDFRAMEBUFFERPROC bindFramebuffer = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC deleteFramebuffers = nullptr;
    PFNGLGENRENDERBUFFERSPROC genRenderbuffers = nullptr;
    PFNGLBINDRENDERBUFFERPROC bindRenderbuffer = nullptr;
    PFNGLDELETERENDERBUFFERSPROC deleteRenderbuffers = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC renderbufferStorage = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC framebufferRenderbuffer = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC checkFramebufferStatus = nullptr;

    bool load() {
        genFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(
            SDL_GL_GetProcAddress("glGenFramebuffers"));
        bindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(
            SDL_GL_GetProcAddress("glBindFramebuffer"));
        deleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(
            SDL_GL_GetProcAddress("glDeleteFramebuffers"));
        genRenderbuffers = reinterpret_cast<PFNGLGENRENDERBUFFERSPROC>(
            SDL_GL_GetProcAddress("glGenRenderbuffers"));
        bindRenderbuffer = reinterpret_cast<PFNGLBINDRENDERBUFFERPROC>(
            SDL_GL_GetProcAddress("glBindRenderbuffer"));
        deleteRenderbuffers = reinterpret_cast<PFNGLDELETERENDERBUFFERSPROC>(
            SDL_GL_GetProcAddress("glDeleteRenderbuffers"));
        renderbufferStorage = reinterpret_cast<PFNGLRENDERBUFFERSTORAGEPROC>(
            SDL_GL_GetProcAddress("glRenderbufferStorage"));
        framebufferRenderbuffer = reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(
            SDL_GL_GetProcAddress("glFramebufferRenderbuffer"));
        checkFramebufferStatus = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(
            SDL_GL_GetProcAddress("glCheckFramebufferStatus"));
        return genFramebuffers != nullptr && bindFramebuffer != nullptr &&
               deleteFramebuffers != nullptr && genRenderbuffers != nullptr &&
               bindRenderbuffer != nullptr && deleteRenderbuffers != nullptr &&
               renderbufferStorage != nullptr && framebufferRenderbuffer != nullptr &&
               checkFramebufferStatus != nullptr;
    }
};

// Per-vertex view-space normals, packed as colours for the normal pass. The
// bake camera only ever yaws, so the whole transform is one rotation about Z
// plus the fixed camera basis; doing it here rather than in a shader is what
// lets a fixed-function context write a normal map at all.
//
// Foliage does not use its own normals. A card's plane normal is its roll, and
// the roll is a random draw, so baking those gives a texture of noise: lit at
// distance the crown fizzes instead of turning. What a distant crown actually
// needs is the normal of the *crown*, so foliage takes the outward direction
// from the trunk axis, and the whole mass shades as the rounded body it reads
// as. Wood keeps its real normals, since a bole is a cylinder and looks like one.
void encodeViewNormals(const Mesh& mesh, const Vec3& right, const Vec3& up, const Vec3& forward,
                       const Vec3* crownAxis, std::vector<Color>& colors) {
    colors.resize(mesh.normals.size());
    for (std::size_t i = 0; i < mesh.normals.size(); ++i) {
        Vec3 normal = mesh.normals[i].normalizedOr(kUpAxis);
        if (crownAxis != nullptr && i < mesh.positions.size()) {
            const Vec3 at = mesh.positions[i];
            normal = Vec3{at.x - crownAxis->x, at.y - crownAxis->y, 0.0f}.normalizedOr(kUpAxis);
        }
        // -forward, so +B faces the camera: the usual tangent-space convention,
        // and the one a consumer will assume when it lights this.
        const Vec3 view{normal.dot(right), normal.dot(up), -normal.dot(forward)};
        colors[i] = {view.x * 0.5f + 0.5f, view.y * 0.5f + 0.5f, view.z * 0.5f + 0.5f};
    }
}

// Renders the near mesh from kImpostorAngles yaws into a 4x2 atlas and writes
// an albedo and a normal PNG. Returns false on any failure, having reported it.
//
// The framing is the impostor quad's own box -- the crown envelope, ground to
// top, widest band to widest band -- so a tile maps onto the quad with no fit
// factor anywhere. Get that wrong and the tree floats inside a margin or has its
// boughs cut off, and neither is visible until the level swaps in.
bool bakeImpostorAtlas(const std::string& prefix, std::uint32_t atlasWidth, const LodMesh& near,
                       const CrownEnvelope& envelope, const std::vector<Color>& barkColors,
                       GLuint barkTexture, const std::vector<GLuint>& foliageTextures,
                       float alphaCutoff) {
    if (!envelope.valid()) {
        std::fprintf(stderr, "bake: the plant has no crown to bake\n");
        return false;
    }
    FramebufferApi fbo;
    if (!fbo.load()) {
        std::fprintf(stderr, "bake: framebuffer objects are unavailable on this context\n");
        return false;
    }

    const std::uint32_t tile = std::max(16u, atlasWidth / kImpostorAtlasColumns);
    const std::uint32_t width = tile * kImpostorAtlasColumns;
    const std::uint32_t height = tile * kImpostorAtlasRows;

    GLuint framebuffer = 0;
    GLuint color = 0;
    GLuint depth = 0;
    fbo.genFramebuffers(1, &framebuffer);
    fbo.genRenderbuffers(1, &color);
    fbo.genRenderbuffers(1, &depth);
    fbo.bindRenderbuffer(GL_RENDERBUFFER, color);
    fbo.renderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, static_cast<GLsizei>(width),
                            static_cast<GLsizei>(height));
    fbo.bindRenderbuffer(GL_RENDERBUFFER, depth);
    fbo.renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(width),
                            static_cast<GLsizei>(height));
    fbo.bindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    fbo.framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    fbo.framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    const bool complete =
        fbo.checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    std::vector<unsigned char> albedo;
    std::vector<unsigned char> normal;
    if (complete) {
        const float halfWidth = envelope.radius;
        const float halfHeight = envelope.top * 0.5f;
        const float centreZ = halfHeight;
        const float range = envelope.radius * 4.0f + envelope.top;
        std::vector<Color> normalColors;

        for (std::uint32_t pass = 0; pass < 2; ++pass) {
            const bool normalPass = pass == 1;
            // Transparent black, so the alpha is a clean cut-out and the RGB of
            // an empty texel cannot bleed into the tree under filtering.
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            for (std::uint32_t angle = 0; angle < kImpostorAngles; ++angle) {
                const std::uint32_t column = angle % kImpostorAtlasColumns;
                const std::uint32_t row = angle / kImpostorAtlasColumns;
                // Row 0 is the top of the image, and GL counts from the bottom.
                const std::uint32_t y = (kImpostorAtlasRows - 1 - row) * tile;
                glViewport(static_cast<GLint>(column * tile), static_cast<GLint>(y),
                           static_cast<GLsizei>(tile), static_cast<GLsizei>(tile));
                glScissor(static_cast<GLint>(column * tile), static_cast<GLint>(y),
                          static_cast<GLsizei>(tile), static_cast<GLsizei>(tile));
                glEnable(GL_SCISSOR_TEST);

                const float yaw = impostorAngleYaw(angle);
                const Vec3 bearing{std::cos(yaw), std::sin(yaw), 0.0f};
                const Vec3 target{envelope.centre.x, envelope.centre.y, centreZ};
                const Vec3 eye = target + bearing * (range * 0.5f);
                loadOrthographic(halfWidth, halfHeight, 0.01f, range);
                loadLookAt(eye, target, kUpAxis);

                if (!normalPass) {
                    glEnable(GL_LIGHTING);
                    const float lightDirection[4] = {0.35f, 0.25f, 0.9f, 0.0f};
                    glLightfv(GL_LIGHT0, GL_POSITION, lightDirection);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                    drawMesh(near.bark, barkColors, barkTexture);
                    drawFoliage(near.foliage, near.sections, Color{0.24f, 0.55f, 0.20f},
                                foliageTextures, alphaCutoff);
                } else {
                    // Colour carries the normal and must survive untouched, so
                    // lighting is off and the texture contributes alpha only --
                    // the cut-out still has to match the albedo pass exactly.
                    glDisable(GL_LIGHTING);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
                    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
                    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
                    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);

                    const Vec3 forward = (target - eye).normalizedOr({0, 1, 0});
                    const Vec3 right = forward.cross(kUpAxis).normalizedOr({1, 0, 0});
                    const Vec3 up = right.cross(forward);

                    encodeViewNormals(near.bark, right, up, forward, nullptr, normalColors);
                    drawMesh(near.bark, normalColors, 0);
                    encodeViewNormals(near.foliage, right, up, forward, &envelope.centre,
                                      normalColors);
                    glEnable(GL_ALPHA_TEST);
                    glAlphaFunc(GL_GREATER, alphaCutoff);
                    for (const MeshSection& section : near.sections) {
                        const GLuint texture =
                            section.material < foliageTextures.size()
                                ? foliageTextures[section.material]
                                : 0;
                        drawMesh(near.foliage, normalColors, texture, section.firstIndex,
                                 section.indexCount);
                    }
                    glDisable(GL_ALPHA_TEST);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                }
                glDisable(GL_SCISSOR_TEST);
            }

            std::vector<unsigned char>& into = normalPass ? normal : albedo;
            into.assign(static_cast<std::size_t>(width) * height * 4, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA,
                         GL_UNSIGNED_BYTE, into.data());
        }
    }

    fbo.bindFramebuffer(GL_FRAMEBUFFER, 0);
    fbo.deleteFramebuffers(1, &framebuffer);
    fbo.deleteRenderbuffers(1, &color);
    fbo.deleteRenderbuffers(1, &depth);
    if (!complete) {
        std::fprintf(stderr, "bake: could not create a %ux%u render target\n", width, height);
        return false;
    }

    // GL hands back bottom-up rows; PNG stores top-down.
    const auto flip = [width, height](std::vector<unsigned char>& pixels) {
        const std::size_t stride = static_cast<std::size_t>(width) * 4;
        std::vector<unsigned char> scratch(stride);
        for (std::uint32_t row = 0; row < height / 2; ++row) {
            unsigned char* low = pixels.data() + static_cast<std::size_t>(row) * stride;
            unsigned char* high =
                pixels.data() + static_cast<std::size_t>(height - 1 - row) * stride;
            std::memcpy(scratch.data(), low, stride);
            std::memcpy(low, high, stride);
            std::memcpy(high, scratch.data(), stride);
        }
    };
    flip(albedo);
    flip(normal);

    const std::string albedoPath = prefix + "_albedo.png";
    const std::string normalPath = prefix + "_normal.png";
    const auto stride = static_cast<int>(width * 4);
    if (stbi_write_png(albedoPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                       albedo.data(), stride) == 0 ||
        stbi_write_png(normalPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                       normal.data(), stride) == 0) {
        std::fprintf(stderr, "bake: could not write %s\n", albedoPath.c_str());
        return false;
    }
    std::printf("baked %s and %s (%ux%u, %u angles at %ux%u, crown %.2f m x %.2f m)\n",
                albedoPath.c_str(), normalPath.c_str(), width, height, kImpostorAngles, tile, tile,
                envelope.radius * 2.0f, envelope.top);
    return true;
}

// The impostor as drawn: the level-3 quad spun about the trunk to face the
// camera, with the atlas cell for the bearing it turned through. Rebuilt per
// frame because it is four vertices and depends on where the camera is.
void drawImpostor(const LodMesh& lod, const CrownEnvelope& envelope, const Vec3& eye,
                  GLuint atlas, float alphaCutoff) {
    if (lod.foliage.vertexCount() < kImpostorVertices || !envelope.valid()) {
        return;
    }
    const float yaw = std::atan2(eye.y - envelope.centre.y, eye.x - envelope.centre.x);
    const ImpostorTile tile = impostorTileForYaw(yaw);

    // A quad authored facing -Y turns to face a camera on bearing yaw when it is
    // rotated about Z by yaw + 90 degrees.
    const float spin = yaw + kPi * 0.5f;
    const float cosine = std::cos(spin);
    const float sine = std::sin(spin);

    Mesh quad;
    quad.positions.reserve(kImpostorVertices);
    quad.normals.reserve(kImpostorVertices);
    quad.uvs.reserve(kImpostorVertices);
    for (std::uint32_t corner = 0; corner < kImpostorVertices; ++corner) {
        // Spins about the trunk only. Height is absolute: the quad already
        // stands on the ground, and rotating it about the crown's centroid
        // would sink it by the centroid's own height.
        const Vec3 at = lod.foliage.positions[corner];
        const float x = at.x - envelope.centre.x;
        const float y = at.y - envelope.centre.y;
        quad.positions.push_back({envelope.centre.x + x * cosine - y * sine,
                                  envelope.centre.y + x * sine + y * cosine, at.z});
        quad.normals.push_back({std::cos(yaw), std::sin(yaw), 0.0f});
        const Vec2 uv = lod.foliage.uvs[corner];
        quad.uvs.push_back({tile.uOffset + uv.x * tile.uScale, tile.vOffset + uv.y * tile.vScale});
    }
    quad.indices = lod.foliage.indices;

    static const std::vector<Color> noColors;
    glColor3f(1.0f, 1.0f, 1.0f);
    if (atlas != 0) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, alphaCutoff);
        // The atlas is a grid of independent bakes: a filtered texel must never
        // be fetched from the neighbouring cell, which wrapping would allow at
        // every tile seam.
        glBindTexture(GL_TEXTURE_2D, atlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    drawMesh(quad, noColors, atlas);
    glDisable(GL_ALPHA_TEST);
}

void drawLines(const std::vector<Vec3>& positions, const std::vector<Color>& colors) {
    if (positions.empty()) {
        return;
    }
    glDisable(GL_LIGHTING);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, positions.data());
    glColorPointer(3, GL_FLOAT, 0, colors.data());
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(positions.size()));
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void drawPoints(const std::vector<Vec3>& positions, const std::vector<Color>& colors) {
    if (positions.empty()) {
        return;
    }
    glDisable(GL_LIGHTING);
    glPointSize(5.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, positions.data());
    glColorPointer(3, GL_FLOAT, 0, colors.data());
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(positions.size()));
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// A 3x5 bitmap font, one byte per scanline with the glyph in the low three
// bits, leftmost pixel highest. Only the characters the overlay actually emits
// are here: pulling in a font library to print one counter is not worth the
// dependency, and a viewer that draws its own quads has everything it needs.
struct Glyph {
    char character;
    std::uint8_t rows[5];
};

constexpr Glyph kGlyphs[] = {
    {'0', {0b111, 0b101, 0b101, 0b101, 0b111}},
    {'1', {0b010, 0b110, 0b010, 0b010, 0b111}},
    {'2', {0b111, 0b001, 0b111, 0b100, 0b111}},
    {'3', {0b111, 0b001, 0b111, 0b001, 0b111}},
    {'4', {0b101, 0b101, 0b111, 0b001, 0b001}},
    {'5', {0b111, 0b100, 0b111, 0b001, 0b111}},
    {'6', {0b111, 0b100, 0b111, 0b101, 0b111}},
    {'7', {0b111, 0b001, 0b010, 0b010, 0b010}},
    {'8', {0b111, 0b101, 0b111, 0b101, 0b111}},
    {'9', {0b111, 0b101, 0b111, 0b001, 0b111}},
    {'E', {0b111, 0b100, 0b110, 0b100, 0b111}},
    {'R', {0b110, 0b101, 0b110, 0b101, 0b101}},
    {'S', {0b011, 0b100, 0b010, 0b001, 0b110}},
    {'T', {0b111, 0b010, 0b010, 0b010, 0b010}},
    {'V', {0b101, 0b101, 0b101, 0b101, 0b010}},
    {'L', {0b100, 0b100, 0b100, 0b100, 0b111}},
    {'O', {0b111, 0b101, 0b101, 0b101, 0b111}},
    {'D', {0b110, 0b101, 0b101, 0b101, 0b110}},
};

// Emits the lit pixels of a string as quads in window pixels, origin top left.
// Caller owns the projection and the colour.
void emitTextQuads(float x, float y, float pixel, const std::string& text) {
    float penX = x;
    for (const char character : text) {
        const Glyph* glyph = nullptr;
        for (const Glyph& candidate : kGlyphs) {
            if (candidate.character == character) {
                glyph = &candidate;
                break;
            }
        }
        if (glyph != nullptr) {
            for (int row = 0; row < 5; ++row) {
                for (int column = 0; column < 3; ++column) {
                    if ((glyph->rows[row] & (0b100 >> column)) == 0) {
                        continue;
                    }
                    const float left = penX + static_cast<float>(column) * pixel;
                    const float top = y + static_cast<float>(row) * pixel;
                    glVertex2f(left, top);
                    glVertex2f(left + pixel, top);
                    glVertex2f(left + pixel, top + pixel);
                    glVertex2f(left, top + pixel);
                }
            }
        }
        penX += 4.0f * pixel; // three columns and one of spacing
    }
}

// Grouped in threes so a six-figure count stays readable at a glance.
std::string groupedNumber(std::size_t value) {
    std::string digits = std::to_string(value);
    for (std::size_t inserted = 3; inserted < digits.size(); inserted += 4) {
        digits.insert(digits.size() - inserted, 1, ' ');
    }
    return digits;
}

// Vertex counts of what is actually on screen, one label per pane in that
// pane's own corner. The solid pass draws meshes; every other mode draws the
// line and point overlays, and reporting the mesh either way would be a number
// for geometry nobody sees.
void drawVertexOverlay(const ViewerState& state, int width, int height, int panes,
                       int paneWidth) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(width), static_cast<double>(height), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);

    const float pixel = std::max(2.0f, static_cast<float>(height) / 320.0f);
    glBegin(GL_QUADS);
    // Rules between the panes, so four views read as four views rather than as
    // one crowded scene.
    glColor3f(0.20f, 0.21f, 0.24f);
    for (int pane = 1; pane < panes; ++pane) {
        const float x = static_cast<float>(pane * paneWidth);
        glVertex2f(x - 1.0f, 0.0f);
        glVertex2f(x + 1.0f, 0.0f);
        glVertex2f(x + 1.0f, static_cast<float>(height));
        glVertex2f(x - 1.0f, static_cast<float>(height));
    }

    for (int pane = 0; pane < panes; ++pane) {
        std::string label;
        if (state.mode != RenderMode::Solid) {
            label = "VERTS " +
                    groupedNumber(state.linePositions.size() + state.pointPositions.size());
        } else {
            const std::uint32_t level =
                panes > 1 ? static_cast<std::uint32_t>(pane) : state.lodLevel;
            label = "LOD" + std::to_string(level) + " VERTS " +
                    groupedNumber(state.lods[level].vertexCount());
        }
        const float x = static_cast<float>(pane * paneWidth) + 2.0f * pixel;
        // The overlay sits over both dark sky and bright canopy, so it carries
        // its own drop shadow rather than relying on a dark background.
        glColor3f(0.04f, 0.04f, 0.05f);
        emitTextQuads(x + pixel, 3.0f * pixel, pixel, label);
        glColor3f(0.96f, 0.96f, 0.92f);
        emitTextQuads(x, 2.0f * pixel, pixel, label);
    }
    glEnd();

    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void updateWindowTitle(SDL_Window* window, const ViewerState& state) {
    const char* modeName = "solid";
    switch (state.mode) {
    case RenderMode::Orders: modeName = "branch orders"; break;
    case RenderMode::Light: modeName = "light"; break;
    case RenderMode::Buds: modeName = "buds"; break;
    case RenderMode::Vigor: modeName = "vigor"; break;
    default: break;
    }
    char title[320];
    std::snprintf(title, sizeof(title),
                  "flora — %s | seed %llu | step %u/%u | %zu internodes | %zu %s | lod %s | "
                  "%zu tris | [%s]",
                  state.species.name.c_str(), static_cast<unsigned long long>(state.seed),
                  state.currentSteps, state.targetSteps, state.plant.graph.internodeCount(),
                  state.useCards && state.showFoliage ? state.cards.size() : state.clusters.size(),
                  !state.showFoliage ? "foliage clusters (hidden)"
                                     : (state.useCards ? "twig cards" : "leaf clusters"),
                  state.compareLods ? "all" : std::to_string(state.lodLevel).c_str(),
                  state.lods[state.compareLods ? 0 : state.lodLevel].triangleCount(), modeName);
    SDL_SetWindowTitle(window, title);
}

} // namespace

int main(int argc, char** argv) {
    ViewerState state;
    std::string bakePrefix;
    std::string impostorPrefix;
    std::uint32_t bakeWidth = 2048;
    if (argc >= 3) {
        state.seed = std::stoull(argv[1]);
        state.targetSteps = static_cast<std::uint32_t>(std::stoul(argv[2]));
    }
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--species") == 0 && i + 1 < argc) {
            std::ifstream file(argv[++i]);
            std::ostringstream text;
            text << file.rdbuf();
            std::string error;
            if (!file || !loadSpeciesFromJson(text.str(), state.species, &error)) {
                std::fprintf(stderr, "invalid species file %s: %s\n", argv[i], error.c_str());
                return 1;
            }
        } else if (std::strcmp(argv[i], "--materials") == 0 && i + 1 < argc) {
            std::string error;
            if (!loadMaterialSetFromFile(argv[++i], state.materials, &error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
        } else if (std::strcmp(argv[i], "--shade") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            if (std::strcmp(mode, "off") == 0) {
                state.shadeModel = ShadeModel::Off;
            } else if (std::strcmp(mode, "raycast") == 0) {
                state.shadeModel = ShadeModel::Raycast;
            }
        } else if (std::strcmp(argv[i], "--lod") == 0 && i + 1 < argc) {
            const unsigned long level = std::strtoul(argv[++i], nullptr, 10);
            state.lodLevel = static_cast<std::uint32_t>(std::min<unsigned long>(level, kLodLevels - 1));
        } else if (std::strcmp(argv[i], "--lod-compare") == 0) {
            state.compareLods = true;
        } else if (std::strcmp(argv[i], "--bake-impostor") == 0 && i + 1 < argc) {
            bakePrefix = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                bakeWidth = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            }
        } else if (std::strcmp(argv[i], "--impostor") == 0 && i + 1 < argc) {
            impostorPrefix = argv[++i];
        } else if (std::strcmp(argv[i], "--no-leaves") == 0) {
            state.showFoliage = false;
        } else {
            std::fprintf(stderr,
                         "usage: flora-viewer <seed> <steps> [--species file.json] "
                         "[--materials file.json] [--shade raycast|off] "
                         "[--lod n] [--lod-compare] [--no-leaves] "
                         "[--bake-impostor <prefix> [width]] [--impostor <prefix>]\n");
            return 1;
        }
    }

    std::printf("flora-viewer %u.%u.%u — 1..5 modes, ,/. playback, drag orbit, wheel zoom\n",
                kVersionMajor, kVersionMinor, kVersionPatch);

    state.plant.seed = state.seed;
    plantSeedling(state.plant, materializeGrowthParameters(state.species, state.seed, 0));
    simulateTo(state, state.targetSteps);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("flora", 1280, 900,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (gl == nullptr) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
    glEnable(GL_NORMALIZE);

    const GLuint barkTexture = uploadTexture(state.materials.bark.color);
    std::vector<GLuint> foliageTextures;
    bool anyFoliageTexture = false;
    for (const FoliageVariant& variant : state.materials.foliage) {
        foliageTextures.push_back(uploadTexture(variant.maps.color));
        anyFoliageTexture = anyFoliageTexture || foliageTextures.back() != 0;
    }
    if (barkTexture != 0 || anyFoliageTexture) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }

    rebuildStructures(state);
    if (!bakePrefix.empty()) {
        // Bake and exit: this is an asset step, not a viewing mode, and leaving
        // the window up afterwards would only invite it to be mistaken for one.
        const bool baked =
            bakeImpostorAtlas(bakePrefix, bakeWidth, state.lods[0], state.envelope,
                              state.barkColors, barkTexture, foliageTextures,
                              state.materials.foliageAlphaCutoff);
        SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return baked ? 0 : 1;
    }
    const GLuint impostorAtlas =
        impostorPrefix.empty() ? 0 : uploadTexture(impostorPrefix + "_albedo.png");

    float yaw = 0.6f;
    float pitch = 0.35f;
    float distance = 40.0f;
    Vec3 target{0.0f, 0.0f, 8.0f};
    bool orbiting = false;
    bool panning = false;

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                switch (event.key.key) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_1: state.mode = RenderMode::Solid; state.structuresDirty = true; break;
                case SDLK_2: state.mode = RenderMode::Orders; state.structuresDirty = true; break;
                case SDLK_3: state.mode = RenderMode::Light; state.structuresDirty = true; break;
                case SDLK_4: state.mode = RenderMode::Buds; state.structuresDirty = true; break;
                case SDLK_5: state.mode = RenderMode::Vigor; state.structuresDirty = true; break;
                case SDLK_C:
                    state.useCards = !state.useCards;
                    state.structuresDirty = true;
                    break;
                case SDLK_T:
                    state.compareLods = !state.compareLods;
                    state.structuresDirty = true;
                    break;
                case SDLK_LEFTBRACKET:
                    state.lodLevel = state.lodLevel == 0 ? 0 : state.lodLevel - 1;
                    state.structuresDirty = true;
                    break;
                case SDLK_RIGHTBRACKET:
                    state.lodLevel = std::min(state.lodLevel + 1, kLodLevels - 1);
                    state.structuresDirty = true;
                    break;
                case SDLK_L:
                    state.showFoliage = !state.showFoliage;
                    state.structuresDirty = true;
                    break;
                case SDLK_COMMA:
                    if (state.currentSteps > 0) {
                        simulateTo(state, state.currentSteps - 1);
                    }
                    break;
                case SDLK_PERIOD:
                    simulateTo(state, state.currentSteps + 1);
                    state.targetSteps = std::max(state.targetSteps, state.currentSteps);
                    break;
                default:
                    break;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    orbiting = true;
                }
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    panning = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    orbiting = false;
                }
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    panning = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (orbiting) {
                    yaw += event.motion.xrel * 0.008f;
                    pitch = std::clamp(pitch + event.motion.yrel * 0.008f, -1.5f, 1.5f);
                }
                if (panning) {
                    const float scale = distance * 0.0015f;
                    target.z += event.motion.yrel * scale;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                distance = std::clamp(distance * (event.wheel.y > 0 ? 0.9f : 1.1f), 2.0f, 400.0f);
                break;
            default:
                break;
            }
        }

        if (state.structuresDirty) {
            rebuildStructures(state);
            updateWindowTitle(window, state);
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // One camera, several viewports. Comparing detail levels means reading
        // the same silhouette at the same angle, so the panes share the orbit
        // state entirely rather than each keeping their own: whatever moves the
        // camera moves all four, and each pane draws its level at the origin.
        const Vec3 eye{target.x + distance * std::cos(pitch) * std::cos(yaw),
                       target.y + distance * std::cos(pitch) * std::sin(yaw),
                       target.z + distance * std::sin(pitch)};
        const bool tiled = state.compareLods && state.mode == RenderMode::Solid;
        const int panes = tiled ? static_cast<int>(kLodLevels) : 1;
        const int paneWidth = std::max(1, width / panes);

        for (int pane = 0; pane < panes; ++pane) {
            const int paneX = pane * paneWidth;
            glViewport(paneX, 0, paneWidth, height);
            glScissor(paneX, 0, paneWidth, height);
            glEnable(GL_SCISSOR_TEST);
            // Depth only: the panes share one cleared colour buffer, but each
            // needs its own depth range or the first pane occludes the rest.
            glClear(GL_DEPTH_BUFFER_BIT);

            loadPerspective(50.0f * kPi / 180.0f,
                            static_cast<float>(paneWidth) / static_cast<float>(std::max(height, 1)),
                            0.1f, 1000.0f);
            loadLookAt(eye, target, kUpAxis);

            const float lightDirection[4] = {0.35f, 0.25f, 0.9f, 0.0f};
            glLightfv(GL_LIGHT0, GL_POSITION, lightDirection);

            // Ground reference disc.
            glDisable(GL_LIGHTING);
            glColor3f(0.16f, 0.17f, 0.16f);
            glBegin(GL_TRIANGLE_FAN);
            glVertex3f(0.0f, 0.0f, 0.0f);
            for (int i = 0; i <= 48; ++i) {
                const float angle = static_cast<float>(i) / 48.0f * 2.0f * kPi;
                glVertex3f(30.0f * std::cos(angle), 30.0f * std::sin(angle), 0.0f);
            }
            glEnd();

            if (state.mode == RenderMode::Solid) {
                glEnable(GL_LIGHTING);
                const std::uint32_t level =
                    tiled ? static_cast<std::uint32_t>(pane) : state.lodLevel;
                const LodMesh& lod = state.lods[level];
                if (lodIsImpostor(level)) {
                    drawImpostor(lod, state.envelope, eye, impostorAtlas,
                                 state.materials.foliageAlphaCutoff);
                } else {
                    drawMesh(lod.bark, state.barkColors, barkTexture);
                    drawFoliage(lod.foliage, lod.sections, Color{0.24f, 0.55f, 0.20f},
                                foliageTextures, state.materials.foliageAlphaCutoff);
                }
            } else {
                drawLines(state.linePositions, state.lineColors);
                drawPoints(state.pointPositions, state.pointColors);
            }
            glDisable(GL_SCISSOR_TEST);
        }
        glViewport(0, 0, width, height);

        drawVertexOverlay(state, width, height, panes, paneWidth);

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
