/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "BackendTest.h"
#include "BackendTestUtils.h"
#include "Lifetimes.h"

#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

using namespace filament;
using namespace filament::backend;

namespace test {

class UpdateImageBenchmark : public BackendTest {
public:
    static constexpr uint32_t kTexSize = 2048; // Stress test with 2K texture
    static constexpr uint32_t kSubSize = kTexSize / 2;

    void SetUp() override {
        BackendTest::SetUp();
        auto& api = getDriverApi();
        mSwapChain = addCleanup(createSwapChain());
        api.makeCurrent(mSwapChain, mSwapChain);
    }

    void TearDown() override {
        BackendTest::TearDown();
    }

    using BackendTest::getDriverApi;
    using BackendTest::addCleanup;
    using BackendTest::flushAndWait;

    // Required by testing::Test base class for concrete fixture instantiation outside of TEST_F macro
    void TestBody() override {}

    Handle<HwSwapChain> mSwapChain;

    // Helper to allocate mock buffer contents without influencing benchmark timing
    PixelBufferDescriptor createDummyBuffer(uint32_t size, PixelDataFormat pFormat, PixelDataType pType) {
        size_t bytesPerPixel = PixelBufferDescriptor::computePixelSize(pFormat, pType);
        size_t totalBytes = size * size * bytesPerPixel;
        void* buffer = malloc(totalBytes);
        return PixelBufferDescriptor(buffer, totalBytes, pFormat, pType, 1, 0, 0, size,
                [](void* buf, size_t, void*) { free(buf); }, nullptr);
    }
};

// ============================================================================
// Case 1 & 2 (Single Upload): Matched vs Mismatched (Format Conversion)
// ============================================================================
void BM_UpdateImage_SingleUpload(benchmark::State& state, TextureFormat tFormat, PixelDataFormat pFormat, PixelDataType pType) {
    UpdateImageBenchmark bm;
    bm.SetUp();
    auto& api = bm.getDriverApi();

    auto usage = TextureUsage::SAMPLEABLE | TextureUsage::UPLOADABLE | TextureUsage::COLOR_ATTACHMENT;
    Handle<HwTexture> texture = bm.addCleanup(api.createTexture(
            SamplerType::SAMPLER_2D, 1, tFormat, 1, bm.kTexSize, bm.kTexSize, 1, usage));

    for (auto _ : state) {
        state.PauseTiming();
        PixelBufferDescriptor pb = bm.createDummyBuffer(bm.kTexSize, pFormat, pType);
        state.ResumeTiming();

        api.beginFrame(0, 0, 0);
        api.update3DImage(texture, 0, 0, 0, 0, bm.kTexSize, bm.kTexSize, 1, std::move(pb));
        api.commit(bm.mSwapChain);
        api.endFrame(0);

        // Flush asynchronous queue to measure actual driver execution time
        bm.flushAndWait();
    }
    bm.TearDown();
}

// ============================================================================
// Case 3 (Multi-tile Upload): 2x2 grid subregion upload overhead evaluation
// ============================================================================
void BM_UpdateImage_SubregionUpload(benchmark::State& state, TextureFormat tFormat, PixelDataFormat pFormat, PixelDataType pType) {
    UpdateImageBenchmark bm;
    bm.SetUp();
    auto& api = bm.getDriverApi();

    auto usage = TextureUsage::SAMPLEABLE | TextureUsage::UPLOADABLE | TextureUsage::COLOR_ATTACHMENT;
    Handle<HwTexture> texture = bm.addCleanup(api.createTexture(
            SamplerType::SAMPLER_2D, 1, tFormat, 1, bm.kTexSize, bm.kTexSize, 1, usage));

    for (auto _ : state) {
        state.PauseTiming();
        PixelBufferDescriptor pb0 = bm.createDummyBuffer(bm.kSubSize, pFormat, pType);
        PixelBufferDescriptor pb1 = bm.createDummyBuffer(bm.kSubSize, pFormat, pType);
        PixelBufferDescriptor pb2 = bm.createDummyBuffer(bm.kSubSize, pFormat, pType);
        PixelBufferDescriptor pb3 = bm.createDummyBuffer(bm.kSubSize, pFormat, pType);
        state.ResumeTiming();

        api.beginFrame(0, 0, 0);
        // Upload 4 continuous distinct subregions
        api.update3DImage(texture, 0, 0,           0,           0, bm.kSubSize, bm.kSubSize, 1, std::move(pb0));
        api.update3DImage(texture, 0, bm.kSubSize, 0,           0, bm.kSubSize, bm.kSubSize, 1, std::move(pb1));
        api.update3DImage(texture, 0, 0,           bm.kSubSize, 0, bm.kSubSize, bm.kSubSize, 1, std::move(pb2));
        api.update3DImage(texture, 0, bm.kSubSize, bm.kSubSize, 0, bm.kSubSize, bm.kSubSize, 1, std::move(pb3));
        api.commit(bm.mSwapChain);
        api.endFrame(0);

        bm.flushAndWait();
    }
    bm.TearDown();
}

// ============================================================================
// Google Benchmark Scenario Definitions
// ============================================================================

// 1. Matched Format (Direct Copy) - Single vs Multiple Subregion Uploads
BENCHMARK_CAPTURE(BM_UpdateImage_SingleUpload, MatchFormat_Single,
        TextureFormat::RGBA8, PixelDataFormat::RGBA, PixelDataType::UBYTE)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UpdateImage_SubregionUpload, MatchFormat_Multi,
        TextureFormat::RGBA8, PixelDataFormat::RGBA, PixelDataType::UBYTE)->Unit(benchmark::kMillisecond);

// 2. Mismatched Format (Staging buffer + Blit/Reshape) - Single vs Multiple Subregion Uploads
BENCHMARK_CAPTURE(BM_UpdateImage_SingleUpload, MismatchedFormat_Single,
        TextureFormat::RGBA16F, PixelDataFormat::RGBA, PixelDataType::FLOAT)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UpdateImage_SubregionUpload, MismatchedFormat_Multi,
        TextureFormat::RGBA16F, PixelDataFormat::RGBA, PixelDataType::FLOAT)->Unit(benchmark::kMillisecond);

// ============================================================================
// Case 4: Custom Resolutions (4K High Resolution vs NPOT 1920x1080)
// ============================================================================
void BM_UpdateImage_CustomResolution(benchmark::State& state, uint32_t width, uint32_t height, TextureFormat tFormat, PixelDataFormat pFormat, PixelDataType pType) {
    UpdateImageBenchmark bm;
    bm.SetUp();
    auto& api = bm.getDriverApi();

    auto usage = TextureUsage::SAMPLEABLE | TextureUsage::UPLOADABLE | TextureUsage::COLOR_ATTACHMENT;
    Handle<HwTexture> texture = bm.addCleanup(api.createTexture(
            SamplerType::SAMPLER_2D, 1, tFormat, 1, width, height, 1, usage));

    for (auto _ : state) {
        state.PauseTiming();
        size_t bytesPerPixel = PixelBufferDescriptor::computePixelSize(pFormat, pType);
        size_t totalBytes = width * height * bytesPerPixel;
        void* buffer = malloc(totalBytes);
        PixelBufferDescriptor pb(buffer, totalBytes, pFormat, pType, 1, 0, 0, width,
                [](void* buf, size_t, void*) { free(buf); }, nullptr);
        state.ResumeTiming();

        api.beginFrame(0, 0, 0);
        api.update3DImage(texture, 0, 0, 0, 0, width, height, 1, std::move(pb));
        api.commit(bm.mSwapChain);
        api.endFrame(0);

        bm.flushAndWait();
    }
    bm.TearDown();
}

// ============================================================================
// Case 5: High-Frequency Micro Subregion Updates (Simulating font/dynamic atlas)
// ============================================================================
void BM_UpdateImage_HighFrequencyMicro(benchmark::State& state, uint32_t tileCount, uint32_t tileSize, TextureFormat tFormat, PixelDataFormat pFormat, PixelDataType pType) {
    UpdateImageBenchmark bm;
    bm.SetUp();
    auto& api = bm.getDriverApi();

    uint32_t fullSize = 2048;
    auto usage = TextureUsage::SAMPLEABLE | TextureUsage::UPLOADABLE | TextureUsage::COLOR_ATTACHMENT;
    Handle<HwTexture> texture = bm.addCleanup(api.createTexture(
            SamplerType::SAMPLER_2D, 1, tFormat, 1, fullSize, fullSize, 1, usage));

    for (auto _ : state) {
        state.PauseTiming();
        std::vector<PixelBufferDescriptor> pbs;
        pbs.reserve(tileCount);
        for (uint32_t i = 0; i < tileCount; ++i) {
            pbs.emplace_back(bm.createDummyBuffer(tileSize, pFormat, pType));
        }
        state.ResumeTiming();

        api.beginFrame(0, 0, 0);
        for (uint32_t i = 0; i < tileCount; ++i) {
            uint32_t xoffset = (i * tileSize) % fullSize;
            uint32_t yoffset = ((i * tileSize) / fullSize) * tileSize;
            api.update3DImage(texture, 0, xoffset, yoffset, 0, tileSize, tileSize, 1, std::move(pbs[i]));
        }
        api.commit(bm.mSwapChain);
        api.endFrame(0);

        bm.flushAndWait();
    }
    bm.TearDown();
}

// ============================================================================
// Case 6: Texture 2D Array / Multi-slice Volume Updates
// ============================================================================
void BM_UpdateImage_Texture2DArray(benchmark::State& state, uint32_t size, uint32_t depth, TextureFormat tFormat, PixelDataFormat pFormat, PixelDataType pType) {
    UpdateImageBenchmark bm;
    bm.SetUp();
    auto& api = bm.getDriverApi();

    auto usage = TextureUsage::SAMPLEABLE | TextureUsage::UPLOADABLE | TextureUsage::COLOR_ATTACHMENT;
    Handle<HwTexture> texture = bm.addCleanup(api.createTexture(
            SamplerType::SAMPLER_2D_ARRAY, 1, tFormat, 1, size, size, depth, usage));

    for (auto _ : state) {
        state.PauseTiming();
        size_t bytesPerPixel = PixelBufferDescriptor::computePixelSize(pFormat, pType);
        size_t totalBytes = size * size * depth * bytesPerPixel;
        void* buffer = malloc(totalBytes);
        PixelBufferDescriptor pb(buffer, totalBytes, pFormat, pType, 1, 0, 0, size,
                [](void* buf, size_t, void*) { free(buf); }, nullptr);
        state.ResumeTiming();

        api.beginFrame(0, 0, 0);
        api.update3DImage(texture, 0, 0, 0, 0, size, size, depth, std::move(pb));
        api.commit(bm.mSwapChain);
        api.endFrame(0);

        bm.flushAndWait();
    }
    bm.TearDown();
}

// ============================================================================
// Case 7: Multi-Mipmap Level Hierarchy Updates
// ============================================================================
void BM_UpdateImage_MultiMipLevel(benchmark::State& state, uint32_t baseSize, uint32_t mipLevels, TextureFormat tFormat, PixelDataFormat pFormat, PixelDataType pType) {
    UpdateImageBenchmark bm;
    bm.SetUp();
    auto& api = bm.getDriverApi();

    auto usage = TextureUsage::SAMPLEABLE | TextureUsage::UPLOADABLE | TextureUsage::COLOR_ATTACHMENT;
    Handle<HwTexture> texture = bm.addCleanup(api.createTexture(
            SamplerType::SAMPLER_2D, mipLevels, tFormat, 1, baseSize, baseSize, 1, usage));

    for (auto _ : state) {
        state.PauseTiming();
        std::vector<PixelBufferDescriptor> pbs;
        pbs.reserve(mipLevels);
        for (uint32_t level = 0; level < mipLevels; ++level) {
            uint32_t size = std::max(1u, baseSize >> level);
            pbs.emplace_back(bm.createDummyBuffer(size, pFormat, pType));
        }
        state.ResumeTiming();

        api.beginFrame(0, 0, 0);
        for (uint32_t level = 0; level < mipLevels; ++level) {
            uint32_t size = std::max(1u, baseSize >> level);
            api.update3DImage(texture, level, 0, 0, 0, size, size, 1, std::move(pbs[level]));
        }
        api.commit(bm.mSwapChain);
        api.endFrame(0);

        bm.flushAndWait();
    }
    bm.TearDown();
}

// 3. Custom Resolutions (HighRes 4K & NPOT 1920x1080)
BENCHMARK_CAPTURE(BM_UpdateImage_CustomResolution, MatchFormat_4K, 4096, 4096,
        TextureFormat::RGBA8, PixelDataFormat::RGBA, PixelDataType::UBYTE)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UpdateImage_CustomResolution, MismatchedFormat_4K, 4096, 4096,
        TextureFormat::RGBA16F, PixelDataFormat::RGBA, PixelDataType::FLOAT)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UpdateImage_CustomResolution, MatchFormat_NPOT, 1920, 1080,
        TextureFormat::RGBA8, PixelDataFormat::RGBA, PixelDataType::UBYTE)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UpdateImage_CustomResolution, MismatchedFormat_NPOT, 1920, 1080,
        TextureFormat::RGBA16F, PixelDataFormat::RGBA, PixelDataType::FLOAT)->Unit(benchmark::kMillisecond);

// 4. High-Frequency Micro Subregion Updates (32 chunks of 64x64)
BENCHMARK_CAPTURE(BM_UpdateImage_HighFrequencyMicro, MatchFormat_MicroUpdates, 32, 64,
        TextureFormat::RGBA8, PixelDataFormat::RGBA, PixelDataType::UBYTE)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UpdateImage_HighFrequencyMicro, MismatchedFormat_MicroUpdates, 32, 64,
        TextureFormat::RGBA16F, PixelDataFormat::RGBA, PixelDataType::FLOAT)->Unit(benchmark::kMillisecond);

// 5. Texture 2D Array / Volume (1024x1024x16 layers)
BENCHMARK_CAPTURE(BM_UpdateImage_Texture2DArray, MatchFormat_2DArray, 1024, 16,
        TextureFormat::RGBA8, PixelDataFormat::RGBA, PixelDataType::UBYTE)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UpdateImage_Texture2DArray, MismatchedFormat_2DArray, 1024, 16,
        TextureFormat::RGBA16F, PixelDataFormat::RGBA, PixelDataType::FLOAT)->Unit(benchmark::kMillisecond);

// 6. Multi-Mipmap Level Hierarchy (3 levels on 2048x2048)
BENCHMARK_CAPTURE(BM_UpdateImage_MultiMipLevel, MatchFormat_MultiMip, 2048, 3,
        TextureFormat::RGBA8, PixelDataFormat::RGBA, PixelDataType::UBYTE)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_UpdateImage_MultiMipLevel, MismatchedFormat_MultiMip, 2048, 3,
        TextureFormat::RGBA16F, PixelDataFormat::RGBA, PixelDataType::FLOAT)->Unit(benchmark::kMillisecond);

} // namespace test
