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

package com.google.android.filament.hellouvmorphing

import android.app.Activity
import android.os.Bundle
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceView
import com.google.android.filament.*
import com.google.android.filament.android.DisplayHelper
import com.google.android.filament.android.FilamentHelper
import com.google.android.filament.android.UiHelper
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.Channels
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

class MainActivity : Activity() {
    companion object {
        init {
            Filament.init()
        }
        private const val GRID_SIZE = 20
        private const val GRID_SCALE = 2.0f
    }

    private lateinit var surfaceView: SurfaceView
    private lateinit var choreographer: Choreographer
    private lateinit var displayHelper: DisplayHelper
    private lateinit var uiHelper: UiHelper

    private lateinit var engine: Engine
    private lateinit var renderer: Renderer
    private lateinit var scene: Scene
    private lateinit var view: View
    private lateinit var camera: Camera

    private var swapChain: SwapChain? = null
    private var skybox: Skybox? =
        null

    // Filament entity representing a renderable object
    @Entity private var renderable = 0

    private var vertexBuffer: VertexBuffer? = null
    private var indexBuffer: IndexBuffer? = null
    private var material: Material? = null
    private var materialInstance: MaterialInstance? = null
    private var baseColorMap: Texture? = null
    private var uvMorphTexture: Texture? = null
    private var morphTargetBuffer: MorphTargetBuffer? = null

    private val frameScheduler = FrameCallback()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        surfaceView = SurfaceView(this)
        setContentView(surfaceView)

        choreographer = Choreographer.getInstance()
        displayHelper = DisplayHelper(this)

        setupSurfaceView()
        setupFilament()
        setupView()
        setupScene()
    }

    private fun setupSurfaceView() {
        uiHelper = UiHelper(UiHelper.ContextErrorPolicy.DONT_CHECK)
        uiHelper.renderCallback = SurfaceCallback()
        uiHelper.attachTo(surfaceView)
    }

    private fun setupFilament() {
        engine = Engine.create()
        renderer = engine.createRenderer()
        scene = engine.createScene()
        view = engine.createView()
        camera = engine.createCamera(engine.entityManager.create())
    }

    private fun setupView() {
        skybox = Skybox.Builder().color(0.1f, 0.125f, 0.25f, 1.0f).build(engine)
        scene.skybox = skybox

        view.camera = camera
        view.scene = scene
        view.isPostProcessingEnabled = false
    }

    private fun setupScene() {
        createMesh()
        createTextures()
        loadMaterial()

        renderable = EntityManager.get().create()
        RenderableManager.Builder(1)
            .boundingBox(Box(0.0f, 0.0f, 0.0f, GRID_SCALE, GRID_SCALE, 1.0f))
            .material(0, materialInstance!!)
            .geometry(0, RenderableManager.PrimitiveType.TRIANGLES, vertexBuffer!!, indexBuffer!!)
            .culling(false)
            .morphing(morphTargetBuffer!!)
            .build(engine, renderable)

        scene.addEntity(renderable)
    }

    private fun createMesh() {
        val vertexCount = GRID_SIZE * GRID_SIZE
        val triangleCount = (GRID_SIZE - 1) * (GRID_SIZE - 1) * 2

        // Vertices
        val vertexData = ByteBuffer.allocateDirect(vertexCount * 5 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()

        for (y in 0 until GRID_SIZE) {
            for (x in 0 until GRID_SIZE) {
                val u = x.toFloat() / (GRID_SIZE - 1)
                val v = y.toFloat() / (GRID_SIZE - 1)
                val px = (u * 2.0f - 1.0f) * GRID_SCALE
                val py = (v * 2.0f - 1.0f) * GRID_SCALE
                val pz = 0.0f
                vertexData.put(px).put(py).put(pz).put(u).put(v)
            }
        }
        vertexData.flip()

        // Indices
        val indexData = ByteBuffer.allocateDirect(triangleCount * 3 * 2).order(ByteOrder.nativeOrder()).asShortBuffer()
        for (y in 0 until GRID_SIZE - 1) {
            for (x in 0 until GRID_SIZE - 1) {
                val tl = (y * GRID_SIZE + x).toShort()
                val tr = (tl + 1).toShort()
                val bl = ((y + 1) * GRID_SIZE + x).toShort()
                val br = (bl + 1).toShort()

                indexData.put(tl).put(tr).put(bl)
                indexData.put(tr).put(br).put(bl)
            }
        }
        indexData.flip()

        vertexBuffer = VertexBuffer.Builder()
            .vertexCount(vertexCount)
            .bufferCount(1)
            .attribute(VertexBuffer.VertexAttribute.POSITION, 0, VertexBuffer.AttributeType.FLOAT3, 0, 5 * 4)
            .attribute(VertexBuffer.VertexAttribute.UV0, 0, VertexBuffer.AttributeType.FLOAT2, 3 * 4, 5 * 4)
            .build(engine)
        vertexBuffer!!.setBufferAt(engine, 0, vertexData)

        indexBuffer = IndexBuffer.Builder()
            .indexCount(triangleCount * 3)
            .bufferType(IndexBuffer.Builder.IndexType.USHORT)
            .build(engine)
        indexBuffer!!.setBuffer(engine, indexData)

        // Morph Targets
        val center = 0.5f
        val uvMorphData = ByteBuffer.allocateDirect(vertexCount * 2 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()

        for (y in 0 until GRID_SIZE) {
            for (x in 0 until GRID_SIZE) {
                val u = x.toFloat() / (GRID_SIZE - 1)
                val v = y.toFloat() / (GRID_SIZE - 1)

                val toCenterX = u - center
                val toCenterY = v - center
                val dist = sqrt(toCenterX * toCenterX + toCenterY * toCenterY)
                val angle = (1.0f - dist) * 3.14159f * 4.0f
                val s = sin(angle)
                val c = cos(angle)

                val rotatedX = toCenterX * c - toCenterY * s
                val rotatedY = toCenterX * s + toCenterY * c
                val newU = rotatedX + center
                val newV = rotatedY + center

                uvMorphData.put(newU - u).put(newV - v)
            }
        }
        uvMorphData.flip()

        morphTargetBuffer = MorphTargetBuffer.Builder()
            .vertexCount(vertexCount)
            .count(1)
            .withPositions(false)
            .withTangents(false)
            .enableCustomMorphing(true)
            .build(engine)

        // We reuse uvMorphData for the texture later, but we don't set it to MTB here because we use texture
    }

    private fun createTextures() {
        // Rainbow texture
        val rainbowBuffer = ByteBuffer.allocateDirect(4 * 4)
        rainbowBuffer.put(0xFF.toByte()).put(0x00.toByte()).put(0x00.toByte()).put(0xFF.toByte())
        rainbowBuffer.put(0x00.toByte()).put(0xFF.toByte()).put(0x00.toByte()).put(0xFF.toByte())
        rainbowBuffer.put(0x00.toByte()).put(0x00.toByte()).put(0xFF.toByte()).put(0xFF.toByte())
        rainbowBuffer.put(0xFF.toByte()).put(0x00.toByte()).put(0xFF.toByte()).put(0xFF.toByte())
        rainbowBuffer.flip()

        baseColorMap = Texture.Builder()
            .width(4)
            .height(1)
            .levels(0xff)
            .format(Texture.InternalFormat.RGBA8)
            .sampler(Texture.Sampler.SAMPLER_2D)
            .build(engine)
        baseColorMap!!.setImage(engine, 0, Texture.PixelBufferDescriptor(rainbowBuffer, Texture.Format.RGBA, Texture.Type.UBYTE))

        // Create UV morph texture from previously generated data logic?
        // We need to regenerate or store it. Let's regenerate for simplicity in this function split.
        val vertexCount = GRID_SIZE * GRID_SIZE
        val center = 0.5f
        val uvMorphData = ByteBuffer.allocateDirect(vertexCount * 2 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
        for (y in 0 until GRID_SIZE) {
            for (x in 0 until GRID_SIZE) {
                val u = x.toFloat() / (GRID_SIZE - 1)
                val v = y.toFloat() / (GRID_SIZE - 1)
                val toCenterX = u - center
                val toCenterY = v - center
                val dist = sqrt(toCenterX * toCenterX + toCenterY * toCenterY)
                val angle = (1.0f - dist) * 3.14159f * 4.0f
                val s = sin(angle)
                val c = cos(angle)
                val rotatedX = toCenterX * c - toCenterY * s
                val rotatedY = toCenterX * s + toCenterY * c
                val newU = rotatedX + center
                val newV = rotatedY + center
                uvMorphData.put(newU - u).put(newV - v)
            }
        }
        uvMorphData.flip()

        uvMorphTexture = Texture.Builder()
            .width(vertexCount)
            .height(1)
            .depth(1)
            .levels(1)
            .format(Texture.InternalFormat.RG32F)
            .sampler(Texture.Sampler.SAMPLER_2D_ARRAY)
            .build(engine)

        uvMorphTexture!!.setImage(engine, 0, 0, 0, 0, vertexCount, 1, 1,
            Texture.PixelBufferDescriptor(uvMorphData, Texture.Format.RG, Texture.Type.FLOAT))
    }

    private fun loadMaterial() {
        readUncompressedAsset("materials/uvmorph.filamat").let {
            material = Material.Builder().payload(it, it.remaining()).build(engine)
        }

        materialInstance = material!!.createInstance()

        val colorSampler = TextureSampler(TextureSampler.MinFilter.LINEAR_MIPMAP_LINEAR, TextureSampler.MagFilter.LINEAR, TextureSampler.WrapMode.REPEAT)
        materialInstance!!.setParameter("baseColor", baseColorMap!!, colorSampler)

        // Uncomment when material is fixed
        val dataSampler = TextureSampler(TextureSampler.MinFilter.NEAREST, TextureSampler.MagFilter.NEAREST, TextureSampler.WrapMode.REPEAT)
        materialInstance!!.setParameter("uv0_morph", uvMorphTexture!!, dataSampler)
    }

    override fun onResume() {
        super.onResume()
        choreographer.postFrameCallback(frameScheduler)
    }

    override fun onPause() {
        super.onPause()
        choreographer.removeFrameCallback(frameScheduler)
    }

    override fun onDestroy() {
        super.onDestroy()
        choreographer.removeFrameCallback(frameScheduler)
        uiHelper.detach()

        engine.destroyEntity(renderable)
        EntityManager.get().destroy(renderable)

        engine.destroyRenderer(renderer)
        engine.destroyView(view)
        engine.destroyScene(scene)
        skybox?.let { engine.destroySkybox(it) }
        materialInstance?.let { engine.destroyMaterialInstance(it) }
        material?.let { engine.destroyMaterial(it) }
        baseColorMap?.let { engine.destroyTexture(it) }
        uvMorphTexture?.let { engine.destroyTexture(it) }
        vertexBuffer?.let { engine.destroyVertexBuffer(it) }
        indexBuffer?.let { engine.destroyIndexBuffer(it) }
        morphTargetBuffer?.let { engine.destroyMorphTargetBuffer(it) }
        engine.destroyCameraComponent(camera.entity)
        EntityManager.get().destroy(camera.entity)

        engine.destroy()
    }

    inner class FrameCallback : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            choreographer.postFrameCallback(this)


            // Animate
            val time = frameTimeNanos.toDouble() / 1_000_000_000.0
            val weight = (sin(time * 2.0) + 1.0f) * 0.5f
            val rm = engine.renderableManager
            rm.setMorphWeights(rm.getInstance(renderable), floatArrayOf(weight.toFloat()), 0)

            if (swapChain != null && renderer.beginFrame(swapChain!!, frameTimeNanos)) {
                renderer.render(view)
                renderer.endFrame()
            }

        }
    }

    inner class SurfaceCallback : UiHelper.RendererCallback {
        override fun onNativeWindowChanged(surface: Surface) {
            if (::engine.isInitialized) {
                swapChain?.let { engine.destroySwapChain(it) }
                swapChain = engine.createSwapChain(surface)
                displayHelper.attach(renderer, surfaceView.display)
            }
        }

        override fun onDetachedFromSurface() {
            if (::engine.isInitialized) {
                displayHelper.detach()
                swapChain?.let {
                    engine.destroySwapChain(it)
                    engine.flushAndWait()
                }
                swapChain = null
            }
        }

        override fun onResized(width: Int, height: Int) {
            val aspect = width.toDouble() / height.toDouble()
            val zoom = 2.5
            camera.setProjection(Camera.Projection.ORTHO, -aspect * zoom, aspect * zoom, -zoom, zoom, 0.0, 1.0)
            view.viewport = Viewport(0, 0, width, height)
            FilamentHelper.synchronizePendingFrames(engine)
        }
    }

    private fun readUncompressedAsset(assetName: String): ByteBuffer {
        assets.openFd(assetName).use { fd ->
            val input = fd.createInputStream()
            val dst = ByteBuffer.allocate(fd.length.toInt())
            val src = Channels.newChannel(input)
            src.read(dst)
            src.close()
            return dst.apply { rewind() }
        }
    }
}
