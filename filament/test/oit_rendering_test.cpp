/*
 * Copyright (C) 2026 The Android Open Source Project
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>

#include <filamat/MaterialBuilder.h>

#include <backend/PixelBufferDescriptor.h>

#include <utils/EntityManager.h>

#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include <stdint.h>

using namespace filament;
using namespace filament::math;

class OitRendering : public testing::Test {
protected:
    Engine* engine = nullptr;
    View* view = nullptr;
    Renderer* renderer = nullptr;
    Scene* scene = nullptr;
    SwapChain* surface = nullptr;
    VertexBuffer* vertices = nullptr;
    IndexBuffer* indices = nullptr;
    utils::Entity cameraEntity;
    std::vector<Material*> materials;
    std::vector<MaterialInstance*> instances;
    std::vector<utils::Entity> objects;
    using Image = std::array<uint8_t, 32 * 32 * 4>;

    void SetUp() override {
        filamat::MaterialBuilder::init();
        engine = Engine::create(Engine::Backend::OPENGL);
        ASSERT_NE(engine, nullptr);
        surface = engine->createSwapChain(32, 32, SwapChain::CONFIG_READABLE);
        renderer = engine->createRenderer();
        renderer->setClearOptions({ .clearColor = {0, 0, 0, 1}, .clear = true });
        scene = engine->createScene();
        cameraEntity = utils::EntityManager::get().create();
        auto* camera = engine->createCamera(cameraEntity);
        camera->setProjection(Camera::Projection::ORTHO, -1, 1, -1, 1, 0.1, 10);
        camera->lookAt({0, 0, 3}, {0, 0, 0});
        view = engine->createView();
        view->setScene(scene);
        view->setCamera(camera);
        view->setViewport({0, 0, 32, 32});
        view->setPostProcessingEnabled(false);
        view->setAntiAliasing(View::AntiAliasing::NONE);
        view->setDithering(View::Dithering::NONE);
        static float3 const positions[] = {{-1, -1, 0}, {3, -1, 0}, {-1, 3, 0}};
        static uint16_t const triangles[] = {0, 1, 2};
        static float4 const tangents[] = {{0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}};
        vertices = VertexBuffer::Builder().vertexCount(3).bufferCount(2)
                .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
                .attribute(VertexAttribute::TANGENTS, 1, VertexBuffer::AttributeType::FLOAT4)
                .build(*engine);
        vertices->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(positions, sizeof(positions)));
        vertices->setBufferAt(*engine, 1, VertexBuffer::BufferDescriptor(tangents, sizeof(tangents)));
        indices = IndexBuffer::Builder().indexCount(3)
                .bufferType(IndexBuffer::IndexType::USHORT).build(*engine);
        indices->setBuffer(*engine, IndexBuffer::BufferDescriptor(triangles, sizeof(triangles)));
    }

    Material* material(BlendingMode blending = BlendingMode::TRANSPARENT, bool lit = false,
            bool refractive = false,
            backend::FeatureLevel featureLevel = backend::FeatureLevel::FEATURE_LEVEL_1) {
        filamat::MaterialBuilder builder;
        builder.name("OitRegression").shading(lit ? Shading::LIT : Shading::UNLIT)
                .featureLevel(featureLevel)
                .platform(filamat::MaterialBuilder::Platform::ALL)
                .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
                .blending(blending)
                .parameter("color", filamat::MaterialBuilder::UniformType::FLOAT4)
                .material("void material(inout MaterialInputs m) { prepareMaterial(m);"
                          "m.baseColor = materialParams.color; }");
        if (refractive) builder.refractionMode(RefractionMode::SCREEN_SPACE);
        auto package = builder.build(engine->getJobSystem());
        EXPECT_TRUE(package.isValid());
        auto* result = Material::Builder().package(package.getData(), package.getSize()).build(*engine);
        materials.push_back(result);
        return result;
    }

    MaterialInstance* add(Material* material, float4 color, float z = 0) {
        auto* mi = material->createInstance();
        mi->setParameter("color", color);
        instances.push_back(mi);
        auto entity = utils::EntityManager::get().create();
        RenderableManager::Builder(1).boundingBox({{0, 0, 0}, {3, 3, 1}})
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vertices, indices)
                .material(0, mi).culling(false).build(*engine, entity);
        auto& transforms = engine->getTransformManager();
        transforms.setTransform(transforms.getInstance(entity), mat4f::translation(float3{0, 0, z}));
        objects.push_back(entity);
        scene->addEntity(entity);
        return mi;
    }

    Image render(bool oit, View* background = nullptr) {
        view->setOitEnabled(oit);
        Image image{};
        bool complete = false;
        // Finish each test frame; a failed beginFrame must not become a false pass.
        EXPECT_TRUE(renderer->beginFrame(surface));
        if (background) renderer->render(background);
        renderer->render(view);
        renderer->readPixels(0, 0, 32, 32, backend::PixelBufferDescriptor(image.data(), image.size(),
                backend::PixelDataFormat::RGBA, backend::PixelDataType::UBYTE,
                [](void*, size_t, void* user) { *static_cast<bool*>(user) = true; }, &complete));
        renderer->endFrame();
        engine->flushAndWait();
        EXPECT_TRUE(complete);
        return image;
    }

    static void compare(Image const& lhs, Image const& rhs, int tolerance = 2) {
        // Compare the complete image, including viewport/scissor edges.
        for (size_t i = 0; i < lhs.size(); ++i) {
            ASSERT_NEAR(int(lhs[i]), int(rhs[i]), tolerance) << "byte " << i;
        }
    }

    void TearDown() override {
        if (!engine) return;
        for (auto entity : objects) {
            engine->destroy(entity);
            utils::EntityManager::get().destroy(entity);
        }
        for (auto* mi : instances) engine->destroy(mi);
        for (auto* ma : materials) engine->destroy(ma);
        engine->destroy(vertices);
        engine->destroy(indices);
        engine->destroy(view);
        engine->destroy(scene);
        engine->destroy(renderer);
        engine->destroy(surface);
        engine->destroyCameraComponent(cameraEntity);
        utils::EntityManager::get().destroy(cameraEntity);
        Engine::destroy(&engine);
        filamat::MaterialBuilder::shutdown();
    }
};

TEST_F(OitRendering, EmptyView) {
    auto off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::NO_TRANSPARENT_OBJECTS);
}

TEST_F(OitRendering, FeatureLevelZeroMaterialStaysInColorPass) {
    add(material(BlendingMode::TRANSPARENT, false, false,
            backend::FeatureLevel::FEATURE_LEVEL_0), {0.25, 0, 0, 0.5});
    auto off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::NO_TRANSPARENT_OBJECTS);
}

TEST_F(OitRendering, SingleLayerAndToggle) {
    add(material(), {0.25, 0.125, 0.0625, 0.5});
    auto off = render(false);
    compare(off, render(true));
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    view->setOitEnabled(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    compare(off, render(false), 0);
    EXPECT_GT(off[16 * 32 * 4 + 16 * 4], 0);
}

TEST_F(OitRendering, NonIndexedGeometryMatchesIndexed) {
    add(material(), {0.25, 0.125, 0.0625, 0.5});
    auto const indexed = render(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    EXPECT_GT(indexed[16 * 32 * 4 + 16 * 4], 0);
    auto& renderables = engine->getRenderableManager();
    renderables.setGeometryAt(renderables.getInstance(objects.back()), 0,
            RenderableManager::PrimitiveType::TRIANGLES, vertices, 0, 3);
    compare(indexed, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    compare(indexed, render(false));
}

TEST_F(OitRendering, MaterialOrderInvariance) {
    auto* ma = material();
    auto* a = add(ma, {0.25, 0, 0, 0.25});
    auto* b = add(ma, {0, 0, 0.5, 0.5});
    auto first = render(true);
    a->setParameter("color", float4{0, 0, 0.5, 0.5});
    b->setParameter("color", float4{0.25, 0, 0, 0.25});
    compare(first, render(true));
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
}

TEST_F(OitRendering, MaskedOccludesTransparent) {
    add(material(), {0, 0, 0.5, 0.5});
    add(material(BlendingMode::MASKED), {1, 0, 0, 1}, 0.5);
    auto off = render(false);
    compare(off, render(true));
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    EXPECT_GT(off[16 * 32 * 4 + 16 * 4], 200);
    EXPECT_EQ(off[16 * 32 * 4 + 16 * 4 + 2], 0);
}

TEST_F(OitRendering, AdditiveStaysInColorPass) {
    auto* additive = add(material(BlendingMode::ADD), {0.25, 0, 0, 1});
    auto* transparent = add(material(), {0, 0, 0.5, 0.5});
    auto mixed = render(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    // The fixed composition must equal transparent source-over the additive background.
    auto& rm = engine->getRenderableManager();
    rm.setBlendOrderAt(rm.getInstance(objects[0]), 0, 0);
    rm.setGlobalBlendOrderEnabledAt(rm.getInstance(objects[0]), 0, true);
    rm.setBlendOrderAt(rm.getInstance(objects[1]), 0, 1);
    rm.setGlobalBlendOrderEnabledAt(rm.getInstance(objects[1]), 0, true);
    compare(mixed, render(false));
    EXPECT_FALSE(additive->isDepthWriteEnabled());
    EXPECT_FALSE(transparent->isDepthWriteEnabled());
}

TEST_F(OitRendering, FadeStaysInColorPass) {
    add(material(BlendingMode::FADE), {0.25, 0, 0, 0.5});
    auto off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::NO_TRANSPARENT_OBJECTS);
    add(material(), {0, 0, 0.5, 0.5});
    auto mixed = render(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    auto& rm = engine->getRenderableManager();
    rm.setBlendOrderAt(rm.getInstance(objects[0]), 0, 0);
    rm.setGlobalBlendOrderEnabledAt(rm.getInstance(objects[0]), 0, true);
    rm.setBlendOrderAt(rm.getInstance(objects[1]), 0, 1);
    rm.setGlobalBlendOrderEnabledAt(rm.getInstance(objects[1]), 0, true);
    compare(mixed, render(false));
}

TEST_F(OitRendering, OitOverridesDepthWriteWithoutChangingMaterial) {
    auto* ma = material();
    auto* front = add(ma, {0.25, 0, 0, 0.5}, 0.5);
    auto* back = add(ma, {0, 0, 0.5, 0.5}, -0.5);
    auto reference = render(true);
    front->setDepthWrite(true);
    back->setDepthWrite(true);
    compare(reference, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    EXPECT_TRUE(front->isDepthWriteEnabled());
    EXPECT_TRUE(back->isDepthWriteEnabled());
    render(false);
    EXPECT_TRUE(front->isDepthWriteEnabled());
}

TEST_F(OitRendering, OrderingAndDepthFallback) {
    auto* mi = add(material(), {0.25, 0, 0, 0.5});
    auto& rm = engine->getRenderableManager();
    auto instance = rm.getInstance(objects[0]);
    rm.setBlendOrderAt(instance, 0, 1);
    auto off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ORDERING);
    rm.setBlendOrderAt(instance, 0, 0);
    mi->setTransparencyMode(TransparencyMode::TWO_PASSES_ONE_SIDE);
    off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::NO_TRANSPARENT_OBJECTS);
}

TEST_F(OitRendering, ParallelEligibilityPreservesFallbackAndResetsEachFrame) {
    view->setPostProcessingEnabled(true);
    auto* ma = material();
    // Exceed the command job splitter's 128-renderable threshold. Distinct depths and
    // colors make damage to the ordinary blending order visible in the fallback comparison.
    for (size_t i = 0; i < 257; ++i) {
        add(ma, i % 2 ? float4{0.01, 0, 0, 0.02} : float4{0, 0, 0.01, 0.02},
                float(i) / 257.0f - 0.5f);
    }
    auto const enabled = render(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    EXPECT_GT(enabled[16 * 32 * 4 + 16 * 4], 0);

    auto& rm = engine->getRenderableManager();
    auto const ordered = rm.getInstance(objects.back());
    rm.setBlendOrderAt(ordered, 0, 1);
    auto off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ORDERING);

    add(material(BlendingMode::OPAQUE, true, true), {0.5, 0.5, 0.5, 1});
    off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::REFRACTION);
    scene->remove(objects.back());
    render(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ORDERING);

    rm.setBlendOrderAt(ordered, 0, 0);
    compare(enabled, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    view->setVisibleLayers(0xff, 0);
    render(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::NO_TRANSPARENT_OBJECTS);
}

TEST_F(OitRendering, AutomaticInstancingAfterOitDecision) {
    auto* ma = material();
    auto* mi = add(ma, {0.01, 0, 0.01, 0.02});
    mi->setDepthWrite(true);
    mi->setTransparencyMode(TransparencyMode::TWO_PASSES_TWO_SIDES);
    auto& rm = engine->getRenderableManager();
    for (size_t i = 1; i < 129; ++i) {
        add(ma, {0, 0, 0, 0}, float(i) / 129.0f);
        // The same instance and geometry allow commands to be merged after OIT sorting.
        rm.setMaterialInstanceAt(rm.getInstance(objects.back()), 0, mi);
    }
    engine->setAutomaticInstancingEnabled(false);
    auto const reference = render(true);
    EXPECT_GT(reference[16 * 32 * 4 + 16 * 4], 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    engine->setAutomaticInstancingEnabled(true);
    compare(reference, render(true), 2);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    EXPECT_TRUE(mi->isDepthWriteEnabled());

    rm.setPriority(rm.getInstance(objects.back()), 5);
    auto const off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ORDERING);
}

TEST_F(OitRendering, OffscreenOrderingDoesNotDisableOit) {
    add(material(), {0, 0, 0.5, 0.5});
    auto expected = render(true);
    add(material(), {0.25, 0, 0, 0.5});
    auto& rm = engine->getRenderableManager();
    auto instance = rm.getInstance(objects[1]);
    rm.setCulling(instance, true);
    rm.setBlendOrderAt(instance, 0, 1);
    auto& tm = engine->getTransformManager();
    tm.setTransform(tm.getInstance(objects[1]), mat4f::translation(float3{1000, 0, 0}));
    compare(expected, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    tm.setTransform(tm.getInstance(objects[1]), mat4f{});
    render(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ORDERING);
}

TEST_F(OitRendering, SpecialDepthStateStaysInColorPass) {
    auto* sharedMaterial = material();
    auto* special = add(sharedMaterial, {0.25, 0, 0, 0.5});
    special->setDepthFunc(MaterialInstance::DepthFunc::A);
    auto off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::NO_TRANSPARENT_OBJECTS);
    // Both programs must coexist in the same material's cache in this frame.
    add(sharedMaterial, {0, 0, 0.5, 0.5}, 0.5f);
    off = render(false);
    compare(off, render(true));
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    EXPECT_EQ(special->getDepthFunc(), MaterialInstance::DepthFunc::A);
}

TEST_F(OitRendering, VisibleRefractionDisablesWholeView) {
    add(material(), {0, 0, 0.5, 0.5});
    add(material(BlendingMode::OPAQUE, true, true), {0.5, 0.5, 0.5, 1});
    auto off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::REFRACTION);
    auto& rm = engine->getRenderableManager();
    rm.setCulling(rm.getInstance(objects[1]), true);
    auto& tm = engine->getTransformManager();
    tm.setTransform(tm.getInstance(objects[1]), mat4f::translation(float3{1000, 0, 0}));
    render(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
}

TEST_F(OitRendering, SkyboxPriorityDoesNotDisableOit) {
    auto* skybox = Skybox::Builder().color({0.1f, 0.2f, 0.3f, 1.0f}).build(*engine);
    scene->setSkybox(skybox);
    add(material(), {0.25, 0, 0, 0.5});
    auto off = render(false);
    compare(off, render(true));
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    auto& rm = engine->getRenderableManager();
    rm.setPriority(rm.getInstance(objects[0]), 7);
    render(true);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ORDERING);
    scene->setSkybox(nullptr);
    engine->destroy(skybox);
}

TEST_F(OitRendering, ViewportAndScissor) {
    auto* mi = add(material(), {0.25, 0, 0, 0.5});
    view->setViewport({4, 4, 24, 24});
    mi->setScissor(2, 3, 12, 14);
    auto off = render(false);
    compare(off, render(true));
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
}

TEST_F(OitRendering, MsaaFallback) {
    add(material(), {0.25, 0, 0, 0.5});
    view->setMultiSampleAntiAliasingOptions({.enabled = true, .sampleCount = 4});
    auto off = render(false);
    compare(off, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::MULTISAMPLE);
}

TEST_F(OitRendering, LightingResourcesAndPostProcessing) {
    view->setPostProcessingEnabled(true);
    view->setAmbientOcclusionOptions({.enabled = true});
    view->setFogOptions({.density = 0.02f, .enabled = true});
    auto light = utils::EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL).direction({0, 0, -1})
            .intensity(10000).castShadows(true).build(*engine, light);
    objects.push_back(light);
    scene->addEntity(light);
    add(material(BlendingMode::OPAQUE, true), {0.2, 0.2, 0.2, 1}, -0.5);
    add(material(BlendingMode::TRANSPARENT, true), {0.1, 0, 0, 0.5});
    auto off = render(false);
    compare(off, render(true), 4);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
}

TEST_F(OitRendering, SecondViewUsesOrdinaryProgram) {
    add(material(), {0.25, 0, 0, 0.5});
    auto off = render(false);
    render(true);
    auto* first = view;
    view = engine->createView();
    view->setScene(scene);
    view->setCamera(&first->getCamera());
    view->setViewport({0, 0, 32, 32});
    view->setPostProcessingEnabled(false);
    compare(off, render(false), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::DISABLED);
    engine->destroy(view);
    view = first;
}

TEST_F(OitRendering, ZeroAlphaAndTwoSided) {
    auto empty = render(false);
    auto* mi = add(material(), {0, 0, 0, 0});
    compare(empty, render(true), 0);
    mi->setParameter("color", float4{0.25, 0, 0, 0.5});
    mi->setTransparencyMode(TransparencyMode::TWO_PASSES_TWO_SIDES);
    auto off = render(false);
    compare(off, render(true));
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
}

TEST_F(OitRendering, NonFiniteCoverageIsEmpty) {
    auto empty = render(false);
    auto* mi = add(material(), {0.25, 0, 0, std::numeric_limits<float>::quiet_NaN()});
    compare(empty, render(true), 0);
    mi->setParameter("color", float4{0.25, 0, 0, std::numeric_limits<float>::infinity()});
    compare(empty, render(true), 0);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
}

TEST_F(OitRendering, TransparentBackgroundPreservesCoverage) {
    // WGL's default window format need not have alpha. Verify the transparent
    // intermediate by compositing it over a preceding blue View instead.
    auto* backgroundScene = engine->createScene();
    auto* background = engine->createView();
    background->setScene(backgroundScene);
    background->setCamera(engine->getCameraComponent(cameraEntity));
    background->setViewport({0, 0, 32, 32});
    background->setPostProcessingEnabled(false);
    renderer->setClearOptions({.clearColor = {0, 0, 1, 1}, .clear = true});
    view->setBlendMode(View::BlendMode::TRANSLUCENT);
    add(material(), {0.25, 0, 0, 0.5});
    auto reference = render(false, background);
    auto oit = render(true, background);
    compare(reference, oit);
    EXPECT_NEAR(int(oit[0]), 64, 2);
    EXPECT_NEAR(int(oit[2]), 128, 2);
    EXPECT_EQ(view->getOitStatus(), View::OitStatus::ENABLED);
    engine->destroy(background);
    engine->destroy(backgroundScene);
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--load-material") {
#if defined(_WIN32)
        // Invalid-package checks intentionally abort. Do not wait for an OS crash dialog.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        std::ifstream stream(argv[2], std::ios::binary);
        if (!stream) return 2;
        std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), {});
        // NOOP deliberately bypasses material version validation.
        auto* engine = Engine::create(Engine::Backend::OPENGL);
        auto* material = Material::Builder().package(bytes.data(), bytes.size()).build(*engine);
        bool const valid = material != nullptr;
        engine->destroy(material);
        Engine::destroy(&engine);
        return valid ? 0 : 1;
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
