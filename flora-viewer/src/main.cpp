// Flora viewer (M8 first cut): grow a plant, orbit around it, inspect debug
// views, scrub growth steps. Rendering is deliberately simple — fixed-function
// OpenGL with client-side arrays — because the viewer is a diagnostic tool,
// not a renderer.
//
//   flora-viewer <seed> <steps> [--species file.json] [--materials file.json]
//                               [--shade voxel|raycast|off]
//
//   1..5     render mode: solid / orders / light / buds / vigor
//   , .      step back / forward (re-simulates deterministically)
//   c        foliage: twig cards (branchlets unmeshed) or leaf clusters
//   drag     orbit    wheel zoom    right-drag pan
//   escape   quit

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef FLORA_VIEWER_PNG
#include <png.h>
#endif

#include "flora/geometry/branch_mesh.hpp"
#include "flora/geometry/foliage_cards.hpp"
#include "flora/geometry/foliage_mesh.hpp"
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
GLuint uploadTexture(const std::string& path) {
    if (path.empty()) {
        return 0;
    }
#ifdef FLORA_VIEWER_PNG
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_file(&image, path.c_str()) == 0) {
        std::fprintf(stderr, "texture %s: %s\n", path.c_str(), image.message);
        return 0;
    }
    image.format = PNG_FORMAT_RGBA;
    std::vector<png_byte> pixels(PNG_IMAGE_SIZE(image));
    if (png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) == 0) {
        std::fprintf(stderr, "texture %s: %s\n", path.c_str(), image.message);
        png_image_free(&image);
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(image.width),
                 static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    std::printf("texture: %s (%ux%u)\n", path.c_str(), image.width, image.height);
    return texture;
#else
    std::fprintf(stderr, "texture %s ignored: viewer built without libpng\n", path.c_str());
    return 0;
#endif
}

// Column-major 4x4 helpers for the fixed-function matrix stack.
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
    ShadeModel shadeModel = ShadeModel::VoxelPropagation;

    Plant plant;
    GrowthContext context;

    bool useCards = true;
    std::vector<TwigCard> cards;
    Mesh barkMesh;
    Mesh leafMesh;
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
    if (state.useCards) {
        TwigCardParameters cardParameters;
        cardParameters.maxBearingRadius = parameters.leafMaxBearingRadius;
        cardParameters.aspect = state.materials.foliageAspect;
        cardParameters.pivotU = state.materials.foliagePivotU;
        cardParameters.seed = state.seed;
        barkParameters.cullBelowRadius = cardParameters.maxBearingRadius;
        buildTwigCards(graph, state.clusters, state.tipPositions, cardParameters, state.cards);
        generateTwigCardMesh(state.cards, cardParameters, state.leafMesh);
    } else {
        state.cards.clear();
        FoliageMeshParameters leafParameters;
        leafParameters.seed = state.seed;
        generateFoliageMesh(state.clusters, leafParameters, state.leafMesh);
    }
    generateBranchMesh(graph, barkParameters, state.barkMesh);

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
    state.barkColors.assign(state.barkMesh.vertexCount(), Color{0.45f, 0.33f, 0.22f});
    state.structuresDirty = false;
}

void drawMesh(const Mesh& mesh, const std::vector<Color>& colors, GLuint texture = 0) {
    if (mesh.vertexCount() == 0) {
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
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT,
                   mesh.indices.data());
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// Cards are cut-outs: the texture's alpha decides the silhouette, so they draw
// with an alpha test rather than blending (order-independent, depth-correct).
void drawFoliage(const Mesh& mesh, Color tint, GLuint texture, float alphaCutoff) {
    if (mesh.vertexCount() == 0) {
        return;
    }
    glColor3f(tint.r, tint.g, tint.b);
    if (texture != 0) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, alphaCutoff);
    }
    static const std::vector<Color> noColors;
    drawMesh(mesh, noColors, texture);
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
                  "flora — %s | seed %llu | step %u/%u | %zu internodes | %zu %s | %zu tris | [%s]",
                  state.species.name.c_str(), static_cast<unsigned long long>(state.seed),
                  state.currentSteps, state.targetSteps, state.plant.graph.internodeCount(),
                  state.useCards ? state.cards.size() : state.clusters.size(),
                  state.useCards ? "twig cards" : "leaf clusters",
                  state.barkMesh.triangleCount() + state.leafMesh.triangleCount(), modeName);
    SDL_SetWindowTitle(window, title);
}

} // namespace

int main(int argc, char** argv) {
    ViewerState state;
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
        } else {
            std::fprintf(stderr,
                         "usage: flora-viewer <seed> <steps> [--species file.json] "
                         "[--materials file.json] [--shade voxel|raycast|off]\n");
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
    const GLuint foliageTexture = uploadTexture(state.materials.foliage.color);
    if (barkTexture != 0 || foliageTexture != 0) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }

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

        loadPerspective(50.0f * kPi / 180.0f,
                        static_cast<float>(width) / static_cast<float>(std::max(height, 1)), 0.1f,
                        1000.0f);
        const Vec3 eye{target.x + distance * std::cos(pitch) * std::cos(yaw),
                       target.y + distance * std::cos(pitch) * std::sin(yaw),
                       target.z + distance * std::sin(pitch)};
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
            drawMesh(state.barkMesh, state.barkColors, barkTexture);
            const Color foliageTint =
                foliageTexture != 0 ? Color{1.0f, 1.0f, 1.0f} : Color{0.24f, 0.55f, 0.20f};
            drawFoliage(state.leafMesh, foliageTint, foliageTexture,
                        state.materials.foliageAlphaCutoff);
        } else {
            drawLines(state.linePositions, state.lineColors);
            drawPoints(state.pointPositions, state.pointColors);
        }

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
