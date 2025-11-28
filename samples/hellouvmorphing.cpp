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

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/MorphTargetBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>

#include <getopt/getopt.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <cmath>

#include "generated/resources/resources.h"

using namespace filament;
using namespace filament::math;
using utils::Entity;
using utils::EntityManager;
using utils::Path;

struct App {
    VertexBuffer* vb = nullptr;
    IndexBuffer* ib = nullptr;
    Material* mat = nullptr;
    MaterialInstance* mi = nullptr;
    Texture* baseColorMap = nullptr;
    Texture* uvMorphTexture = nullptr;
    Camera* cam = nullptr;
    Entity camera;
    Skybox* skybox = nullptr;
    Entity renderable;
    MorphTargetBuffer* mtb = nullptr;
};

struct Vertex {
    float3 position;
    float2 uv;
};

// A simple quad, UVs are set up to span the texture horizontally.
static const Vertex QUAD_VERTICES[4] = {
    {{-1.0f, -0.25f, 0.0f}, {0.00f, 0.5f}},
    {{ 1.0f, -0.25f, 0.0f}, {1.00f, 0.5f}},
    {{-1.0f,  0.25f, 0.0f}, {0.00f, 0.5f}},
    {{ 1.0f,  0.25f, 0.0f}, {1.00f, 0.5f}},
};

static constexpr uint16_t QUAD_INDICES[6] = {0, 1, 2, 3, 2, 1};

// We only have one morph target. It adds 1.0 to the U coordinate, causing the texture to scroll.
static const float2 UV_DELTA[4] = {
    {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}
};

static const float3 POS_DELTA[4] = {
    {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}
};

int main(int argc, char** argv) {
    Config config;
    config.title = "hellouvmorphing";
    config.backend = Engine::Backend::OPENGL;

    App app;
    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        app.skybox = Skybox::Builder().color({0.1, 0.125, 0.25, 1.0}).build(*engine);
        scene->setSkybox(app.skybox);
        view->setPostProcessingEnabled(false);

        app.vb = VertexBuffer::Builder()
                .vertexCount(4)
                .bufferCount(1)
                .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, 0, sizeof(Vertex))
                .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, sizeof(float3), sizeof(Vertex))
                .build(*engine);
        app.vb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(QUAD_VERTICES, sizeof(QUAD_VERTICES)));

        app.ib = IndexBuffer::Builder()
                .indexCount(6)
                .bufferType(IndexBuffer::IndexType::USHORT)
                .build(*engine);
        app.ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(QUAD_INDICES, sizeof(QUAD_INDICES)));

        // Create a 4x1 "rainbow" texture: Red, Green, Blue, Yellow
        static const uint32_t rainbow[] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xff00ffff};
        app.baseColorMap = Texture::Builder()
                .width(4).height(1)
                .levels(1)
                .format(Texture::InternalFormat::RGBA8)
                .sampler(Texture::Sampler::SAMPLER_2D)
                .build(*engine);
        Texture::PixelBufferDescriptor pbd(rainbow, sizeof(rainbow),
                Texture::Format::RGBA, Texture::Type::UBYTE);
        app.baseColorMap->setImage(*engine, 0, std::move(pbd));

        // Create the texture that holds our UV morph data.
        app.uvMorphTexture = Texture::Builder()
                .width(4).height(1).depth(1) // 4 vertices, 1 target
                .levels(1)
                .format(Texture::InternalFormat::RG32F) // 2 floats for UV
                .sampler(Texture::Sampler::SAMPLER_2D_ARRAY)
                .build(*engine);
        Texture::PixelBufferDescriptor uvDelta(UV_DELTA, sizeof(UV_DELTA),
                Texture::Format::RG, Texture::Type::FLOAT);
        app.uvMorphTexture->setImage(*engine, 0, 0, 0, 0, 4, 1, 1, std::move(uvDelta));

        // Create the material.
        app.mat = Material::Builder()
                .package(RESOURCES_UVMORPH_DATA, RESOURCES_UVMORPH_SIZE)
                .build(*engine);
        app.mi = app.mat->createInstance();

        // Set the sampler for the base color map to use REPEAT wrap mode.
        TextureSampler colorSampler(TextureSampler::MinFilter::NEAREST, TextureSampler::MagFilter::NEAREST, TextureSampler::WrapMode::REPEAT);
        app.mi->setParameter("baseColor", app.baseColorMap, colorSampler);

        // Set the sampler for the morph data texture. It MUST use NEAREST filtering.
        TextureSampler dataSampler(TextureSampler::MinFilter::NEAREST, TextureSampler::MagFilter::NEAREST);
        app.mi->setParameter("uv0_morph", app.uvMorphTexture, dataSampler);

        // Create the MorphTargetBuffer. It enables the morphing pipeline and provides the target count.
        app.mtb = MorphTargetBuffer::Builder()
                .vertexCount(4)
                .count(1)
                .withPositions(true)
                .build(*engine);

        app.mtb->setPositionsAt(*engine, 0, POS_DELTA, 4, 0);

        // Create the renderable
        app.renderable = EntityManager::get().create();
        RenderableManager::Builder(1)
                .boundingBox({{-1, -1, -1}, {1, 1, 1}})
                .material(0, app.mi)
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib)
                .culling(false)
                .morphing(app.mtb)
                .build(*engine, app.renderable);
        scene->addEntity(app.renderable);

        // Setup the camera
        app.camera = utils::EntityManager::get().create();
        app.cam = engine->createCamera(app.camera);
        view->setCamera(app.cam);
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.skybox);
        engine->destroy(app.renderable);
        engine->destroy(app.mi);
        engine->destroy(app.mat);
        engine->destroy(app.baseColorMap);
        engine->destroy(app.uvMorphTexture);
        engine->destroy(app.vb);
        engine->destroy(app.ib);
        engine->destroy(app.mtb);
        engine->destroyCameraComponent(app.camera);
        EntityManager::get().destroy(app.camera);
    };

    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {
        constexpr float ZOOM = 1.5f;
        const uint32_t w = view->getViewport().width;
        const uint32_t h = view->getViewport().height;
        const float aspect = (float)w / h;
        app.cam->setProjection(Camera::Projection::ORTHO, -aspect * ZOOM, aspect * ZOOM, -ZOOM, ZOOM, 0, 1);

        // Animate the morph weight in a ping-pong fashion from 0.0 to 1.0
        auto& rm = engine->getRenderableManager();
        float weight = abs(sin(now * 0.5));
        rm.setMorphWeights(rm.getInstance(app.renderable), &weight, 1);
    });

    FilamentApp::get().run(config, setup, cleanup);

    return 0;
}
