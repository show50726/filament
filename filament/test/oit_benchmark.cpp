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
#include <filament/SwapChain.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>

#include <filamat/MaterialBuilder.h>

#if defined(_WIN32)
#include <backend/platforms/PlatformWGL.h>
// PlatformWGL includes windows.h; remove its macros before the utility headers.
#include <utils/unwindows.h>
#endif

#include <utils/EntityManager.h>

#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

#include <stdint.h>

using namespace filament;
using namespace filament::math;

#if defined(_WIN32)
// The hidden WGL surface still swaps buffers by default. Exclude presentation
// waits from this offscreen benchmark's GPU timer without changing the engine.
class BenchmarkPlatform final : public backend::PlatformWGL {
    void commit(backend::Platform::SwapChain*) noexcept override {}
};
#endif

// This file also builds against 66ee6ab77 without FILAMENT_OIT_HAS_STATUS.
// Keep the workload and timing API identical when measuring that baseline.
int main(int argc, char** argv) {
    if (argc != 8) {
        std::cerr << "Usage: benchmark_oit WIDTH HEIGHT LAYERS COVERAGE_PERCENT FRAMES OIT LIT\n";
        return 2;
    }
    int values[7];
    for (size_t i = 0; i < 7; ++i) {
        char* end = nullptr;
        long const value = std::strtol(argv[i + 1], &end, 10);
        if (!end || *end || value < 0 || value > 8192) return 2;
        values[i] = int(value);
    }
    auto const [width, height, layers, coverage, frames, oit, lit] = values;
    if (!width || !height || !layers || layers > 1024 || !coverage || coverage > 100 ||
            frames < 16 || oit > 1 || lit > 1) return 2;
    filamat::MaterialBuilder::init();
#if defined(_WIN32)
    BenchmarkPlatform platform;
    auto* engine = Engine::create(Engine::Backend::OPENGL, &platform);
#else
    auto* engine = Engine::create(Engine::Backend::OPENGL);
#endif
    if (!engine) return 1;
    auto* surface = engine->createSwapChain(uint32_t(width), uint32_t(height));
    auto* renderer = engine->createRenderer();
    renderer->setClearOptions({.clearColor = {0, 0, 0, 1}, .clear = true});
    auto* scene = engine->createScene();
    auto cameraEntity = utils::EntityManager::get().create();
    auto* camera = engine->createCamera(cameraEntity);
    camera->setProjection(Camera::Projection::ORTHO, -1, 1, -1, 1, 0.1, 10);
    camera->lookAt({0, 0, 3}, {0, 0, 0});
    auto* view = engine->createView();
    view->setViewport({0, 0, uint32_t(width), uint32_t(height)});
    view->setCamera(camera);
    view->setScene(scene);
    view->setOitEnabled(oit != 0);
    view->setMultiSampleAntiAliasingOptions({.enabled = false});
    view->setAntiAliasing(View::AntiAliasing::NONE);
    view->setDithering(View::Dithering::NONE);
    filamat::MaterialBuilder builder;
    builder.name("OitBenchmark").shading(lit ? Shading::LIT : Shading::UNLIT)
            .platform(filamat::MaterialBuilder::Platform::ALL)
            .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
            .blending(BlendingMode::TRANSPARENT)
            .parameter("color", filamat::MaterialBuilder::UniformType::FLOAT4)
            .material("void material(inout MaterialInputs m) { prepareMaterial(m);"
                      "m.baseColor = materialParams.color; }");
    auto package = builder.build(engine->getJobSystem());
    if (!package.isValid()) return 1;
    auto* material = Material::Builder().package(package.getData(), package.getSize()).build(*engine);
    static float3 const positions[] = {{-1, -1, 0}, {3, -1, 0}, {-1, 3, 0}};
    static float4 const tangents[] = {{0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}};
    static uint16_t const triangles[] = {0, 1, 2};
    auto* vertices = VertexBuffer::Builder().vertexCount(3).bufferCount(2)
            .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
            .attribute(VertexAttribute::TANGENTS, 1, VertexBuffer::AttributeType::FLOAT4).build(*engine);
    vertices->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(positions, sizeof(positions)));
    vertices->setBufferAt(*engine, 1, VertexBuffer::BufferDescriptor(tangents, sizeof(tangents)));
    auto* indices = IndexBuffer::Builder().indexCount(3)
            .bufferType(IndexBuffer::IndexType::USHORT).build(*engine);
    indices->setBuffer(*engine, IndexBuffer::BufferDescriptor(triangles, sizeof(triangles)));
    std::vector<utils::Entity> objects;
    std::vector<MaterialInstance*> instances;
    auto& transforms = engine->getTransformManager();
    float const coverageScale = std::sqrt(float(coverage) / 100.0f);
    uint32_t const cw = std::max(1u, uint32_t(float(width) * coverageScale));
    uint32_t const ch = std::max(1u, uint32_t(float(height) * coverageScale));
    for (int i = 0; i < layers; ++i) {
        auto* mi = material->createInstance();
        mi->setParameter("color", float4{0.01f * float(i % 3 + 1), 0.02f, 0.04f, 0.1f});
        mi->setScissor((uint32_t(width) - cw) / 2, (uint32_t(height) - ch) / 2, cw, ch);
        auto entity = utils::EntityManager::get().create();
        RenderableManager::Builder(1).boundingBox({{0, 0, 0}, {3, 3, 1}}).culling(false)
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vertices, indices)
                .material(0, mi).build(*engine, entity);
        transforms.setTransform(transforms.getInstance(entity),
                mat4f::translation(float3{0, 0, float(i) / float(layers)}));
        scene->addEntity(entity);
        objects.push_back(entity);
        instances.push_back(mi);
    }
    auto light = utils::EntityManager::get().create();
    if (lit) {
        LightManager::Builder(LightManager::Type::DIRECTIONAL).direction({0, 0, -1})
                .intensity(10000).build(*engine, light);
        scene->addEntity(light);
    }
    auto draw = [&] {
        int retries = 0;
        while (!renderer->beginFrame(surface)) {
            engine->flushAndWait();
            if (++retries == 1000) {
                std::cerr << "Unable to begin a benchmark frame\n";
                std::exit(1);
            }
        }
        auto const start = std::chrono::steady_clock::now();
        renderer->render(view);
        auto const end = std::chrono::steady_clock::now();
        renderer->endFrame();
        return std::chrono::duration<double, std::milli>(end - start).count();
    };
    for (int i = 0; i < 60; ++i) draw();
    engine->flushAndWait();
    uint32_t firstFrame = 0;
    for (auto const& info : renderer->getFrameInfoHistory(renderer->getMaxFrameHistorySize())) {
        firstFrame = std::max(firstFrame, info.frameId);
    }
    std::map<uint32_t, double> gpu;
    std::vector<double> cpu;
    auto collect = [&] {
        for (auto const& info : renderer->getFrameInfoHistory(renderer->getMaxFrameHistorySize())) {
            if (info.frameId > firstFrame && info.gpuFrameDuration > 0) {
                gpu[info.frameId] = double(info.gpuFrameDuration) / 1e6;
            }
        }
    };
    for (int i = 0; i < frames; ++i) {
        cpu.push_back(draw());
        collect();
    }
    engine->flushAndWait();
    collect();
    std::sort(cpu.begin(), cpu.end());
    std::vector<double> gpuTimes;
    for (auto const& entry : gpu) gpuTimes.push_back(entry.second);
    std::sort(gpuTimes.begin(), gpuTimes.end());
    int status = -1; // Legacy build has no effective-state API.
#if defined(FILAMENT_OIT_HAS_STATUS)
    status = int(view->getOitStatus());
#endif
    std::cout << "OIT_CSV,width,height,layers,coverage,oit,lit,status,cpu_render_p50_ms,"
                 "cpu_render_p95_ms,gpu_samples,gpu_p50_ms,gpu_p95_ms\n";
    std::cout << "OIT_CSV," << width << ',' << height << ',' << layers << ',' << coverage << ','
              << oit << ',' << lit << ',' << status << ',' << cpu[cpu.size() / 2] << ','
              << cpu[cpu.size() * 95 / 100] << ',' << gpuTimes.size() << ',';
    if (gpuTimes.empty()) std::cout << "NA,NA\n";
    else std::cout << gpuTimes[gpuTimes.size() / 2] << ',' << gpuTimes[gpuTimes.size() * 95 / 100] << '\n';
    for (auto entity : objects) { engine->destroy(entity); utils::EntityManager::get().destroy(entity); }
    for (auto* mi : instances) engine->destroy(mi);
    engine->destroy(light);
    utils::EntityManager::get().destroy(light);
    engine->destroy(material);
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
    return 0;
}
