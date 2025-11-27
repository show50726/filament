/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef TNT_FILAMENT_MORPHTARGETBUFFER_H
#define TNT_FILAMENT_MORPHTARGETBUFFER_H

#include <filament/FilamentAPI.h>

#include <filament/Engine.h>

#include <utils/compiler.h>
#include <utils/StaticString.h>

#include <math/mathfwd.h>

#include <stddef.h>

namespace filament {

/**
 * A container for vertex morphing data.
 *
 * A MorphTargetBuffer is a container for a set of morph targets. Each target is a set of vertex
 * attributes (e.g. positions, tangents). A MorphTargetBuffer is created using a builder, which
 * allows specifying the number of vertices and the number of morph targets.
 *
 * By default, a MorphTargetBuffer is an empty object. The builder can be used to enable built-in
 * support for position and tangent/normal morphing, which provides an easy-to-use path for these
 * common cases.
 *
 * For custom data morphing (e.g. texture coordinates), a MorphTargetBuffer can be created without
 * enabling the built-in attributes. The user is then responsible for managing their own data
 * textures and applying the morphing logic inside the material's vertex shader.
 *
 * @see RenderableManager
 */
class UTILS_PUBLIC MorphTargetBuffer : public FilamentAPI {
    struct BuilderDetails;

public:
    class Builder : public BuilderBase<BuilderDetails>, public BuilderNameMixin<Builder> {
        friend struct BuilderDetails;
    public:
        Builder() noexcept;
        Builder(Builder const& rhs) noexcept;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder const& rhs) noexcept;
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * Sets the number of vertices in this morph target buffer.
         * @param vertexCount Number of vertices.
         * @return A reference to this Builder for chaining calls.
         */
        Builder& vertexCount(size_t vertexCount) noexcept;

        /**
         * Sets the number of morph targets in this buffer.
         * @param count Number of targets.
         * @return A reference to this Builder for chaining calls.
         */
        Builder& count(size_t count) noexcept;

        /**
         * Enables and allocates the built-in buffer for position morphing.
         *
         * If enabled, `setPositionsAt` can be called to set the position data for each target.
         * The morphing calculation can then be performed in the vertex shader by calling
         * `morphPosition()`.
         *
         * @param enable true to enable, false to disable. Default is false.
         * @return A reference to this Builder for chaining calls.
         */
        Builder& withPositions(bool enable = true) noexcept;

        /**
         * Enables and allocates the built-in buffer for tangent/normal morphing.
         *
         * If enabled, `setTangentsAt` can be called to set the tangent data for each target.
         * The morphing calculation can then be performed in the vertex shader by calling
         * `morphNormal()`.
         *
         * @param enable true to enable, false to disable. Default is false.
         * @return A reference to this Builder for chaining calls.
         */
        Builder& withTangents(bool enable = true) noexcept;

        /**
         * Creates the MorphTargetBuffer object and returns a pointer to it.
         *
         * @param engine Reference to the filament::Engine to associate this MorphTargetBuffer with.
         *
         * @return pointer to the newly created object.
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         */
        MorphTargetBuffer* UTILS_NONNULL build(Engine& engine);
    private:
        friend class FMorphTargetBuffer;
    };

    /**
     * Updates positions for the given morph target.
     *
     * This method can only be called if the MorphTargetBuffer was built with `withPositions(true)`.
     * This is equivalent to the float4 method, but uses 1.0 for the 4th component.
     *
     * @param engine Reference to the filament::Engine associated with this MorphTargetBuffer.
     * @param targetIndex the index of morph target to be updated.
     * @param positions pointer to at least "count" positions
     * @param count number of float3 vectors in positions
     * @param offset offset into the target buffer, expressed as a number of float3 vectors
     */
    void setPositionsAt(Engine& engine, size_t targetIndex,
            math::float3 const* UTILS_NONNULL positions, size_t count, size_t offset = 0);

    /**
     * Updates positions for the given morph target.
     *
     * This method can only be called if the MorphTargetBuffer was built with `withPositions(true)`.
     *
     * @param engine Reference to the filament::Engine associated with this MorphTargetBuffer.
     * @param targetIndex the index of morph target to be updated.
     * @param positions pointer to at least "count" positions
     * @param count number of float4 vectors in positions
     * @param offset offset into the target buffer, expressed as a number of float4 vectors
     */
    void setPositionsAt(Engine& engine, size_t targetIndex,
            math::float4 const* UTILS_NONNULL positions, size_t count, size_t offset = 0);

    /**
     * Updates tangents for the given morph target.
     *
     * This method can only be called if the MorphTargetBuffer was built with `withTangents(true)`.
     * These quaternions must be represented as signed shorts, where real numbers in the [-1,+1]
     * range are multiplied by 32767.
     *
     * @param engine Reference to the filament::Engine associated with this MorphTargetBuffer.
     * @param targetIndex the index of morph target to be updated.
     * @param tangents pointer to at least "count" tangents
     * @param count number of short4 quaternions in tangents
     * @param offset offset into the target buffer, expressed as a number of short4 vectors
     */
    void setTangentsAt(Engine& engine, size_t targetIndex,
            math::short4 const* UTILS_NONNULL tangents, size_t count, size_t offset = 0);

    /**
     * Returns the vertex count of this MorphTargetBuffer.
     * @return The number of vertices the MorphTargetBuffer holds.
     */
    size_t getVertexCount() const noexcept;

    /**
     * Returns the target count of this MorphTargetBuffer.
     * @return The number of targets the MorphTargetBuffer holds.
     */
    size_t getCount() const noexcept;

    /**
     * Returns true if this MorphTargetBuffer has a position buffer.
     * @see Builder::withPositions
     */
    bool hasPositions() const noexcept;

    /**
     * Returns true if this MorphTargetBuffer has a tangent buffer.
     * @see Builder::withTangents
     */
    bool hasTangents() const noexcept;

protected:
    // prevent heap allocation
    ~MorphTargetBuffer() = default;
};

} // namespace filament

#endif //TNT_FILAMENT_MORPHTARGETBUFFER_H
