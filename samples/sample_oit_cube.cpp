/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/arguments.h"

#include <cstddef>
#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <getopt/getopt.h>

#include <cmath>
#include <iostream>
#include <vector>

#include "generated/resources/resources.h"

#include <imgui.h>

using namespace filament;
using namespace filament::math;
using namespace utils;

struct App {
    Config config;
    utils::Entity light;
    bool oitEnabled = true;
    bool useMBOIT = false;
    float momentBias = 0.000001f;
    float overestimation = 0.1f;

    Material* transparentMaterial = nullptr;
    VertexBuffer* vb = nullptr;
    IndexBuffer* ib = nullptr;
    Box aabb;
    std::vector<utils::Entity> renderables;
    std::vector<MaterialInstance*> materialInstances;
};

static const char* IBL_FOLDER = "assets/ibl/lightroom_14b";


// To avoid header dependency issues, I will treat TANGENTS as FLOAT4 for now and pass standard
// Quaternions. This is supported by VertexBuffer::AttributeType::FLOAT4.
struct VertexFloat {
    float3 position;
    float4 tangent;
};

static const float3 CUBE_POSITIONS[24] = {
    // Front Transformed
    { -0.5, -0.5, 0.5 }, { 0.5, -0.5, 0.5 }, { 0.5, 0.5, 0.5 }, { -0.5, 0.5, 0.5 },
    // Back
    { 0.5, -0.5, -0.5 }, { -0.5, -0.5, -0.5 }, { -0.5, 0.5, -0.5 }, { 0.5, 0.5, -0.5 },
    // Top
    { -0.5, 0.5, 0.5 }, { 0.5, 0.5, 0.5 }, { 0.5, 0.5, -0.5 }, { -0.5, 0.5, -0.5 },
    // Bottom
    { -0.5, -0.5, -0.5 }, { 0.5, -0.5, -0.5 }, { 0.5, -0.5, 0.5 }, { -0.5, -0.5, 0.5 },
    // Right
    { 0.5, -0.5, 0.5 }, { 0.5, -0.5, -0.5 }, { 0.5, 0.5, -0.5 }, { 0.5, 0.5, 0.5 },
    // Left
    { -0.5, -0.5, -0.5 }, { -0.5, -0.5, 0.5 }, { -0.5, 0.5, 0.5 }, { -0.5, 0.5, -0.5 }
};

static const float3 CUBE_NORMALS[6] = {
    { 0, 0, 1 },  // Front
    { 0, 0, -1 }, // Back
    { 0, 1, 0 },  // Top
    { 0, -1, 0 }, // Bottom
    { 1, 0, 0 },  // Right
    { -1, 0, 0 }  // Left
};

static const float3 CUBE_TANGENTS[6] = {
    { 1, 0, 0 },  // Front
    { -1, 0, 0 }, // Back
    { 1, 0, 0 },  // Top
    { 1, 0, 0 },  // Bottom
    { 0, 0, -1 }, // Right
    { 0, 0, 1 }   // Left
};

static const uint16_t CUBE_INDICES[36] = { 0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4, 8, 9, 10, 10, 11, 8,
    12, 13, 14, 14, 15, 12, 16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20 };

static void printUsage(char* name) {
    std::string exec_name(utils::Path(name).getName());
    std::string usage(
            "EXEC renders a simple OIT example\n"
            "Usage:\n"
            "    EXEC [options]\n"
            "Options:\n"
            "   --help, -h\n"
            "       Prints this message\n\n"
            "API_USAGE"
    );
    const std::string from("EXEC");
    while (true) {
        size_t pos = usage.find(from);
        if (pos == std::string::npos) break;
        usage.replace(pos, from.length(), exec_name);
    }
    const std::string apiUsage("API_USAGE");
    while (true) {
        size_t pos = usage.find(apiUsage);
        if (pos == std::string::npos) break;
        usage.replace(pos, apiUsage.length(), samples::getBackendAPIArgumentsUsage());
    }
    std::cout << usage;
}

static int handleCommandLineArguments(int argc, char* argv[], App* app) {
    static constexpr const char* OPTSTR = "ha:";
    static const struct option OPTIONS[] = {
            { "help", no_argument,       nullptr, 'h' },
            { "api",  required_argument, nullptr, 'a' },
            { nullptr, 0,                nullptr, 0 }
    };
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, OPTSTR, OPTIONS, &option_index)) >= 0) {
        std::string arg(optarg ? optarg : "");
        switch (opt) {
            default:
            case 'h':
                printUsage(argv[0]);
                exit(0);
            case 'a':
                app->config.backend = samples::parseArgumentsForBackend(arg);
                break;
        }
    }
    return optind;
}

int main(int argc, char** argv) {
    App app;
    app.config.title = "sample_oit";
    app.config.samples = 1; // Keep MSAA off for OIT.
    app.config.iblDirectory = FilamentApp::getRootAssetsPath() + IBL_FOLDER;
    handleCommandLineArguments(argc, argv, &app);

    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        auto& tcm = engine->getTransformManager();
        auto& em = utils::EntityManager::get();

        view->setOitEnabled(app.oitEnabled);
        view->setScreenSpaceRefractionEnabled(false);

        // Create materials.
        app.transparentMaterial = Material::Builder()
                                          .package(RESOURCES_SANDBOXLITTRANSPARENT_DATA,
                                                  RESOURCES_SANDBOXLITTRANSPARENT_SIZE)
                                          .build(*engine);

        // Build Cube Geometry
        std::vector<VertexFloat> vertices(24);
        for (int f = 0; f < 6; ++f) {
            float3 n = CUBE_NORMALS[f];
            float3 t = CUBE_TANGENTS[f];
            float3 b = cross(n, t);
            mat3f tbn(t, b, n);
            quatf q = mat3f::packTangentFrame(tbn);

            for (int i = 0; i < 4; ++i) {
                vertices[f * 4 + i].position = CUBE_POSITIONS[f * 4 + i];
                vertices[f * 4 + i].tangent = float4(q.x, q.y, q.z, q.w);
            }
        }


        app.vb = VertexBuffer::Builder()
                         .vertexCount(24)
                         .bufferCount(1)
                         .attribute(VertexAttribute::POSITION, 0,
                                 VertexBuffer::AttributeType::FLOAT3,
                                 offsetof(VertexFloat, position), sizeof(VertexFloat))
                         .attribute(VertexAttribute::TANGENTS, 0,
                                 VertexBuffer::AttributeType::FLOAT4,
                                 offsetof(VertexFloat, tangent), sizeof(VertexFloat))
                         .build(*engine);

        // Allocate vertex buffer on heap to ensure it stays valid until upload is complete
        size_t size = vertices.size() * sizeof(VertexFloat);
        void* buffer = malloc(size);
        memcpy(buffer, vertices.data(), size);

        app.vb->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(
                        buffer, size, [](void* buffer, size_t size, void* user) { free(buffer); },
                        nullptr));

        app.ib = IndexBuffer::Builder()
                         .indexCount(36)
                         .bufferType(IndexBuffer::IndexType::USHORT)
                         .build(*engine);

        app.ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(
                                           CUBE_INDICES, 36 * sizeof(uint16_t),
                                           [](void* buffer, size_t size, void* user) {}, nullptr));

        // Create Grid of Cubes
        // Grid: 10 wide (X), 10 deep (Z)
        // Transparency 0->1 along X
        // Color var along Z

        const int GRID_X = 10;
        const int GRID_Z = 10;
        const float SPACING = 1.2f;
        const float START_X = -(GRID_X * SPACING) / 2.0f;
        const float START_Z = -(GRID_Z * SPACING) / 2.0f;

        // Simple palette for Z
        const float3 COLORS[] = {
            { 1.0f, 0.2f, 0.2f }, // Red
            { 1.0f, 0.6f, 0.0f }, // Orange
            { 1.0f, 1.0f, 0.0f }, // Yellow
            { 0.2f, 1.0f, 0.2f }, // Green
            { 0.2f, 0.6f, 1.0f }, // Light Blue
            { 0.2f, 0.2f, 1.0f }, // Blue
            { 0.6f, 0.2f, 1.0f }, // Purple
            { 1.0f, 0.2f, 0.8f }, // Pink
            { 1.0f, 1.0f, 1.0f }, // White
            { 0.5f, 0.5f, 0.5f }, // Gray
        };

        for (int z = 0; z < GRID_Z; ++z) {
            for (int x = 0; x < GRID_X; ++x) {
                auto mi = app.transparentMaterial->createInstance();

                float alpha = (float) (x) / (GRID_X - 1); // 0 to 1
                float3 color = COLORS[z % 10];

                mi->setParameter("baseColor", color);
                mi->setParameter("alpha", alpha);
                mi->setParameter("metallic", 0.0f);
                mi->setParameter("roughness", 0.2f);
                mi->setParameter("reflectance", 0.5f);

                // Set stencil needed for some OIT modes?
                // The original sample set StencilWrite=true for "transparent monkeys".
                // We should keep it for safety if using weighting that relies on it or depth
                // peeling? Standard OIT usually handles this, but let's keep it consistent.
                mi->setStencilWrite(true);
                mi->setStencilOpDepthStencilPass(MaterialInstance::StencilOperation::INCR);

                app.materialInstances.push_back(mi);

                auto renderable = em.create();
                RenderableManager::Builder(1)
                        .boundingBox({ { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } })
                        .material(0, mi)
                        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib)
                        .culling(false)
                        .castShadows(false)
                        .receiveShadows(false) // Translucent shadows are hard
                        .build(*engine, renderable);

                app.renderables.push_back(renderable);
                scene->addEntity(renderable);

                auto ti = tcm.getInstance(renderable);
                float3 pos = { START_X + x * SPACING, 0.0f, START_Z + z * SPACING };
                tcm.setTransform(ti,
                        mat4f::translation(pos) * mat4f::scaling(float3{ 0.8f, 0.8f, 0.8f }));
            }
        }

        // Add light.
        app.light = em.create();
        LightManager::Builder(LightManager::Type::SUN)
                .color(Color::toLinear<ACCURATE>(sRGBColor(0.98f, 0.92f, 0.89f)))
                .intensity(110000)
                .direction({ 0.7, -1, -0.8 })
                .sunAngularRadius(1.9f)
                .castShadows(false)
                .build(*engine, app.light);
        scene->addEntity(app.light);

        // Set camera.
        view->getCamera().lookAt({ 0, 10, 15 }, { 0, 0, 0 }, { 0, 1, 0 });
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.light);
        for (auto r : app.renderables) {
            engine->destroy(r);
        }
        for (auto mi : app.materialInstances) {
            engine->destroy(mi);
        }
        engine->destroy(app.transparentMaterial);
        engine->destroy(app.vb);
        engine->destroy(app.ib);
    };

    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {
        if (app.oitEnabled != view->isOitEnabled()) {
            view->setOitEnabled(app.oitEnabled);
        }
        View::OitType desiredType =
                app.useMBOIT ? View::OitType::MOMENT_BASED : View::OitType::WEIGHTED_BLENDED;
        if (desiredType != view->getOitType()) {
            view->setOitType(desiredType);
        }
        OitOptions options = view->getOitOptions();
        options.momentBias = app.momentBias;
        options.overestimation = app.overestimation;
        view->setOitOptions(options);
    });

    auto gui = [&app](Engine* engine, View* view) {
        ImGui::Begin("Controls");
        ImGui::Checkbox("Enable OIT", &app.oitEnabled);
        ImGui::Checkbox("Use MBOIT", &app.useMBOIT);
        if (app.useMBOIT) {
            ImGui::SliderFloat("Moment Bias", &app.momentBias, 0.0f, 0.01f, "%.8f");
            ImGui::SliderFloat("Overestimation", &app.overestimation, 0.0f, 1.0f);
        }
        ImGui::End();
    };

    FilamentApp::get().run(app.config, setup, cleanup, gui);

    return 0;
}
