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

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/Box.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>

#include <filameshio/MeshReader.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <utils/getopt.h>

#include <iostream>
#include <vector>

#include "generated/resources/resources.h"
#include "generated/resources/monkey.h"

#include <imgui.h>

using namespace filament;
using namespace filament::math;
using namespace utils;

struct MonkeyControls {
    float3 color;
    float alpha;
    float3 position;
    float scale;
    float3 rotation; // Euler angles in degrees
};

struct App {
    Config config;
    utils::Entity light;
    bool oitEnabled = true;
    bool useMBOIT = false;
    float momentBias = 0.000001f;
    float overestimation = 0.1f;

    Material* transparentMaterial = nullptr;
    Material* opaqueMaterial = nullptr;
    VertexBuffer* vb = nullptr;
    IndexBuffer* ib = nullptr;
    Box aabb;
    std::vector<utils::Entity> renderables;
    std::vector<MaterialInstance*> materialInstances;

    // Store transform instances for the two transparent monkeys
    std::array<TransformManager::Instance, 2> transparentMonkeyTransforms;

    // Store GUI-controllable properties
    std::array<MonkeyControls, 2> monkeyControls;
};

static const char* IBL_FOLDER = "assets/ibl/lightroom_14b";

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
    static constexpr const char* OPTSTR = "ha:m";
    static const struct option OPTIONS[] = { { "help", getopt::no_argument, nullptr, 'h' },
        { "api", getopt::required_argument, nullptr, 'a' }, { "mboit", getopt::no_argument, nullptr, 'm' },
        { nullptr, 0, nullptr, 0 } };
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
            case 'm':
                app->useMBOIT = true;
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
        auto& rcm = engine->getRenderableManager();
        auto& em = utils::EntityManager::get();

        view->setOitEnabled(app.oitEnabled);
        view->setScreenSpaceRefractionEnabled(false);

        // Create materials.
        app.transparentMaterial = Material::Builder()
            .package(RESOURCES_SANDBOXLITTRANSPARENT_DATA, RESOURCES_SANDBOXLITTRANSPARENT_SIZE)
            .build(*engine);
        app.opaqueMaterial = Material::Builder()
            .package(RESOURCES_SANDBOXLIT_DATA, RESOURCES_SANDBOXLIT_SIZE)
            .build(*engine);

        // Load the monkey mesh from the filamesh buffer to get its geometry.
        auto dummyMI = app.transparentMaterial->createInstance();
        filamesh::MeshReader::Mesh mesh = filamesh::MeshReader::loadMeshFromBuffer(engine,
                MONKEY_SUZANNE_DATA, nullptr, nullptr, dummyMI);

        // Extract the geometry and bounding box, then clean up the temporary objects.
        app.vb = mesh.vertexBuffer;
        app.ib = mesh.indexBuffer;
        app.aabb = rcm.getAxisAlignedBoundingBox(rcm.getInstance(mesh.renderable));
        engine->destroy(mesh.renderable);
        engine->destroy(dummyMI);

        // Initialize monkey control properties
        app.monkeyControls[0] = { {0.8f, 0.1f, 0.1f}, 0.5f, {-0.5f, 0.0f, 0.0f}, 1.5f, {0.0f, 0.0f, 0.0f} }; // Left
        app.monkeyControls[1] = { {0.1f, 0.1f, 0.8f}, 0.5f, { 0.5f, 0.0f, 0.0f}, 1.5f, {0.0f, 0.0f, 0.0f} }; // Right

        // Create two transparent renderables.
        for (int i = 0; i < 2; ++i) {
            auto mi = app.transparentMaterial->createInstance();
            mi->setStencilWrite(true);
            mi->setStencilOpDepthStencilPass(MaterialInstance::StencilOperation::INCR);
            app.materialInstances.push_back(mi);

            auto renderable = em.create();
            RenderableManager::Builder(1)
                .boundingBox(app.aabb)
                .material(0, mi)
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib)
                .culling(false)
                .castShadows(false)
                .receiveShadows(false)
                .build(*engine, renderable);
            app.renderables.push_back(renderable);
            scene->addEntity(renderable);

            app.transparentMonkeyTransforms[i] = tcm.getInstance(renderable);
        }

        // Create the opaque monkey in the middle.
        auto mi = app.opaqueMaterial->createInstance();
        mi->setParameter("baseColor", float3{0.5f, 0.5f, 0.5f}); // Gray
        mi->setParameter("metallic", 0.0f);
        mi->setParameter("roughness", 0.1f);
        app.materialInstances.push_back(mi);

        auto renderable = em.create();
        RenderableManager::Builder(1)
            .boundingBox(app.aabb)
            .material(0, mi)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib)
            .culling(false)
            .castShadows(true)
            .receiveShadows(true)
            .build(*engine, renderable);
        app.renderables.push_back(renderable);
        scene->addEntity(renderable);

        auto ti = tcm.getInstance(renderable);
        // Place it in the center, slightly behind the transparent ones.
        tcm.setTransform(ti, mat4f::translation(float3{0.0f, 0.0f, -0.5f}) * mat4f::scaling(1.5f));

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
        view->getCamera().lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
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
        engine->destroy(app.opaqueMaterial);
        engine->destroy(app.vb);
        engine->destroy(app.ib);
    };

    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {
        auto& tcm = engine->getTransformManager();
        if (app.oitEnabled != view->isOitEnabled()) {
            view->setOitEnabled(app.oitEnabled);
        }
        view->setOitType(
                app.useMBOIT ? View::OitType::MOMENT_BASED : View::OitType::WEIGHTED_BLENDED);
        OitOptions options = view->getOitOptions();
        options.momentBias = app.momentBias;
        options.overestimation = app.overestimation;
        view->setOitOptions(options);

        // Update transparent monkeys based on GUI controls
        for (int i = 0; i < 2; ++i) {
            // materialInstances[0] is left, materialInstances[1] is right
            auto mi = app.materialInstances[i];
            const auto& controls = app.monkeyControls[i];

            mi->setParameter("baseColor", controls.color);
            mi->setParameter("alpha", controls.alpha);
            mi->setParameter("metallic", 0.0f);
            mi->setParameter("roughness", 0.2f);
            mi->setParameter("reflectance", 0.5f);

            mat4f transform = mat4f::translation(controls.position) *
                              mat4f::eulerYXZ(
                                      controls.rotation.y * M_PI / 180.0f,
                                      controls.rotation.x * M_PI / 180.0f,
                                      controls.rotation.z * M_PI / 180.0f) *
                              mat4f::scaling(float3(controls.scale));
            tcm.setTransform(app.transparentMonkeyTransforms[i], transform);
        }
    });

    auto gui = [&app](Engine* engine, View* view) {
        ImGui::Begin("Controls");
        ImGui::Checkbox("Enable OIT", &app.oitEnabled);
        ImGui::Checkbox("Use MBOIT", &app.useMBOIT);
        if (app.useMBOIT) {
            ImGui::SliderFloat("Moment Bias", &app.momentBias, 0.0f, 0.01f, "%.8f");
            ImGui::SliderFloat("Overestimation", &app.overestimation, 0.0f, 1.0f);
        }
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Left Monkey")) {
            ImGui::ColorEdit3("Color##Left", &app.monkeyControls[0].color.r);
            ImGui::SliderFloat("Alpha##Left", &app.monkeyControls[0].alpha, 0.0f, 1.0f);
            ImGui::SliderFloat3("Position##Left", &app.monkeyControls[0].position.x, -5.0f, 5.0f);
            ImGui::SliderFloat("Scale##Left", &app.monkeyControls[0].scale, 0.1f, 5.0f);
            ImGui::SliderFloat3("Rotation##Left", &app.monkeyControls[0].rotation.x, -180.0f, 180.0f);
        }

        if (ImGui::CollapsingHeader("Right Monkey")) {
            ImGui::ColorEdit3("Color##Right", &app.monkeyControls[1].color.r);
            ImGui::SliderFloat("Alpha##Right", &app.monkeyControls[1].alpha, 0.0f, 1.0f);
            ImGui::SliderFloat3("Position##Right", &app.monkeyControls[1].position.x, -5.0f, 5.0f);
            ImGui::SliderFloat("Scale##Right", &app.monkeyControls[1].scale, 0.1f, 5.0f);
            ImGui::SliderFloat3("Rotation##Right", &app.monkeyControls[1].rotation.x, -180.0f, 180.0f);
        }

        ImGui::End();
    };

    FilamentApp::get().run(app.config, setup, cleanup, gui);

    return 0;
}
