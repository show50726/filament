/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/TransformManager.h>
#include <filament/RenderableManager.h>
#include <filament/Material.h>
#include <filament/Texture.h>
#include <filament/RenderTarget.h>
#include <filament/Camera.h>
#include <filament/Viewport.h>
#include <filament/Skybox.h>

#include <backend/PixelBufferDescriptor.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>

#include <filameshio/MeshReader.h>

#include <filamentapp/Config.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "generated/resources/resources.h"
#include "generated/resources/monkey.h"

using namespace filament;
using namespace filament::math;

int main(int argc, char** argv) {
    Config config;
    config.title = "standalone_view";

    Engine* engine = Engine::create(config.backend);
    Renderer* renderer = engine->createRenderer();
    Scene* scene = engine->createScene();
    View* view = engine->createView();

    const uint32_t width = 512;
    const uint32_t height = 512;

    auto& em = utils::EntityManager::get();
    auto cameraEntity = em.create();
    Camera* camera = engine->createCamera(cameraEntity);
    view->setCamera(camera);
    camera->setLensProjection(45.0, (double)width / height, 0.1, 100.0);
    camera->lookAt({0, 0, 4}, {0, 0, 0});

    view->setScene(scene);
    view->setViewport(Viewport{0, 0, width, height});
    view->setPostProcessingEnabled(false);

    Skybox* skybox = Skybox::Builder()
            .color({0.1f, 0.1f, 0.1f, 1.0f})
            .build(*engine);
    scene->setSkybox(skybox);

    auto material = Material::Builder()
            .package(RESOURCES_SANDBOXUNLIT_DATA, RESOURCES_SANDBOXUNLIT_SIZE).build(*engine);
    auto materialInstance = material->createInstance();
    materialInstance->setParameter("baseColor", sRGBColor(0.8f, 0.8f, 0.8f));

    auto mesh = filamesh::MeshReader::loadMeshFromBuffer(engine, MONKEY_SUZANNE_DATA, nullptr,
            nullptr, materialInstance);
    scene->addEntity(mesh.renderable);

    // Create a color texture for the RenderTarget
    Texture* colorTexture = Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::BLIT_SRC)
            .format(Texture::InternalFormat::RGBA8)
            .build(*engine);

    // Create a depth texture for the RenderTarget
    Texture* depthTexture = Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .usage(Texture::Usage::DEPTH_ATTACHMENT)
            .format(Texture::InternalFormat::DEPTH24) // A common depth format
            .build(*engine);

    // Create the RenderTarget with both color and depth attachments
    RenderTarget* renderTarget = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR, colorTexture)
            .texture(RenderTarget::AttachmentPoint::DEPTH, depthTexture)
            .build(*engine);

    view->setRenderTarget(renderTarget);

    renderer->renderStandaloneView(view);

    size_t size = width * height * 4;
    void* buffer = malloc(size);

    // To resolve the ambiguity of calling the constructor with nullptr,
    // we explicitly cast nullptr to the callback function pointer type.
    backend::PixelBufferDescriptor bufferDescriptor(buffer, size,
            backend::PixelBufferDescriptor::PixelDataFormat::RGBA,
            backend::PixelBufferDescriptor::PixelDataType::UBYTE,
            static_cast<backend::BufferDescriptor::Callback>(nullptr),
            nullptr);

    renderer->readPixels(renderTarget, 0, 0, width, height, std::move(bufferDescriptor));

    // This blocks until the GPU has finished all commands, including the readPixels command.
    // After this returns, 'buffer' is guaranteed to be filled with data.
    engine->flushAndWait();

    // Now that we are sure the data is in 'buffer', we can write it to a file.
    // stbi_flip_vertically_on_write(true);
    stbi_write_png("standalone_view_output.png", width, height, 4, buffer, width * 4);

    // And now we can safely free the buffer.
    free(buffer);

    // Cleanup
    engine->destroy(mesh.renderable);
    engine->destroy(materialInstance);
    engine->destroy(material);
    engine->destroy(colorTexture);
    engine->destroy(depthTexture);
    engine->destroy(renderTarget);
    engine->destroy(cameraEntity);
    engine->destroy(skybox);
    engine->destroy(view);
    engine->destroy(scene);
    engine->destroy(renderer);
    Engine::destroy(&engine);

    return 0;
}
