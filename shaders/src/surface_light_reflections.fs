//------------------------------------------------------------------------------
// Screen-space reflections
//------------------------------------------------------------------------------

#if defined(MATERIAL_HAS_REFLECTIONS)

#define SSR_TRACING_MODE_LINEAR 0u
#define SSR_TRACING_MODE_HI_Z   1u

// Copied from depthUtils.fs
highp float linearizeDepth(highp float depth) {
    // Our far plane is at infinity, which causes a division by zero below, which in turn
    // causes some issues on some GPU. We workaround it by replacing "infinity" by the closest
    // value representable in  a 24 bit depth buffer.
    const highp float preventDiv0 = 1.0 / 16777216.0;
    highp mat4 p = getViewFromClipMatrix();
    // this works with perspective and ortho projections, for a perspective projection
    // this resolves to -near/depth, for an ortho projection this resolves to depth*(far - near) - far
    return (depth * p[2].z + p[3].z) / max(depth * p[2].w + p[3].w, preventDiv0);
}

// Code adapted from McGuire and Mara, Efficient GPU Screen-Space Ray Tracing, Journal of Computer
// Graphics Techniques, 2014
//
// Copyright (c) 2014, Morgan McGuire and Michael Mara
// All rights reserved.
//
// This software is open source under the "BSD 2-clause license":
//
//    Redistribution and use in source and binary forms, with or without modification, are permitted
//    provided that the following conditions are met:
//
//    1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
//    2. Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
//    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
//    AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
//    CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
//    OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//    POSSIBILITY OF SUCH DAMAGE.

void swap(inout highp float a, inout highp float b) {
     highp float temp = a;
     a = b;
     b = temp;
}

highp float distanceSquared(highp vec2 a, highp vec2 b) {
    a -= b;
    return dot(a, a);
}

bool traceScreenSpaceRayLinear(highp vec2 P0, highp vec2 P1, highp vec3 Q0, highp vec3 Q1,
        highp float k0, highp float k1, const highp float vsOriginZ,
        const highp sampler2D vsZBuffer, const float vsZThickness, const float stride,
        const float jitterFraction, const highp float maxSteps,
        out highp vec2 hitPixel, out highp vec3 vsHitPoint) {
    // If the line is degenerate, make it cover at least one pixel to avoid handling zero-pixel
    // extent as a special case later
    P1 += vec2((distanceSquared(P0, P1) < 0.0001) ? 0.01 : 0.0);

    highp vec2 delta = P1 - P0;

    // Permute so that the primary iteration is in x to reduce large branches later
    bool permute = false;
    if (abs(delta.x) < abs(delta.y)) {
        // More-vertical line. Create a permutation that swaps x and y in the output
        permute = true;
        // Directly swizzle the inputs
        delta = delta.yx;
        P1 = P1.yx;
        P0 = P0.yx;
    }

    // From now on, "x" is the primary iteration direction and "y" is the secondary one

    float stepDirection = sign(delta.x);
    highp float invdx = stepDirection / delta.x;
    highp vec2 dP = vec2(stepDirection, invdx * delta.y);

    // Track the derivatives of Q and k
    highp vec3  dQ = (Q1 - Q0) * invdx;
    highp float dk = (k1 - k0) * invdx;

    // Scale derivatives by the desired pixel stride
    dP *= stride; dQ *= stride; dk *= stride;

    // Offset the starting values by the jitter fraction
    P0 += dP * jitterFraction; Q0 += dQ * jitterFraction; k0 += dk * jitterFraction;

    // Slide P from P0 to P1, (now-homogeneous) Q from Q0 to Q1, and k from k0 to k1
    highp vec3  Q = Q0;
    highp float k = k0;

    // We track the ray depth at +/- 1/2 pixel to treat pixels as clip-space solid voxels. Because
    // the depth at -1/2 for a given pixel will be the same as at +1/2 for the previous iteration,
    // we actually only have to compute one value per iteration.
    highp float prevZMaxEstimate = vsOriginZ;
    highp float stepCount = 0.0;
    highp float rayZMax = prevZMaxEstimate;
    highp float rayZMin = prevZMaxEstimate;
    highp float sceneZMax = rayZMax + 1e4;

    // P1.x is never modified after this point, so pre-scale it by the step direction for a signed
    // comparison
    highp float end = P1.x * stepDirection;

    // We only advance the z field of Q in the inner loop, since Q.xy is never used until after the
    // loop terminates.

    for (highp vec2 P = P0;
        ((P.x * stepDirection) <= end) &&
        (stepCount < maxSteps) &&
        ((rayZMax < sceneZMax - vsZThickness) ||
            (rayZMin > sceneZMax)) &&
        (sceneZMax != 0.0);
        P += dP, Q.z += dQ.z, k += dk, stepCount += 1.0) {

        hitPixel = permute ? P.yx : P;

        // The depth range that the ray covers within this loop iteration.  Assume that the ray is
        // moving in increasing z and swap if backwards.  Because one end of the interval is shared
        // between adjacent iterations, we track the previous value and then swap as needed to
        // ensure correct ordering
        rayZMin = prevZMaxEstimate;

        // Compute the value at 1/2 pixel into the future
        rayZMax = (dQ.z * 0.5 + Q.z) / (dk * 0.5 + k);
        prevZMaxEstimate = rayZMax;
        if (rayZMin > rayZMax) { swap(rayZMin, rayZMax); }

        // View-space z of the background
        // TODO: explore if we can avoid the linearization of the depth sample here.
        sceneZMax = linearizeDepth(texelFetch(vsZBuffer, int2(hitPixel), 0).r);
    } // pixel on ray

    Q.xy += dQ.xy * stepCount;
    vsHitPoint = Q * (1.0 / k);
    // Matches the new loop condition:
    return (rayZMax >= sceneZMax - vsZThickness) && (rayZMin <= sceneZMax);
}

// -- end "BSD 2-clause license" -------------------------------------------------------------------

//------------------------------------------------------------------------------
// Hi-Z ray tracing
// Reference: Sugu Lee, "Screen Space Reflections: Implementation and
// optimization - Part 2: HI-Z Tracing Method"
//------------------------------------------------------------------------------

highp vec2 getCellCount(const highp sampler2D vsZBuffer, int mipLevel) {
    return vec2(textureSize(vsZBuffer, mipLevel));
}

highp vec2 getCell(highp vec2 pos, highp vec2 cellCount) {
    return floor(pos * cellCount);
}

highp vec3 intersectDepthPlane(highp vec3 o, highp vec3 d, highp float t) {
    return o + d * t;
}

highp vec3 intersectCellBoundary(highp vec3 o, highp vec3 d, highp vec2 cell,
        highp vec2 cellCount, highp vec2 crossStep, highp vec2 crossOffset) {
    highp vec2 cellSize = 1.0 / cellCount;
    highp vec2 planes = cell / cellCount + cellSize * crossStep;

    highp vec2 solutions = (planes - o) / d.xy;
    highp vec3 intersectionPos = o + d * min(solutions.x, solutions.y);

    intersectionPos.xy += (solutions.x < solutions.y) ? vec2(crossOffset.x, 0.0) : vec2(0.0, crossOffset.y);
    return intersectionPos;

    //highp vec2 index = cell + crossStep;
    //highp vec2 boundary = index / cellCount;
    //boundary += crossOffset;

    //highp vec2 delta = boundary - o.xy;
    //delta.x = abs(d.x) > 1e-8 ? (delta.x / d.x) : 1e6;
    //delta.y = abs(d.y) > 1e-8 ? (delta.y / d.y) : 1e6;
    //highp float t = min(delta.x, delta.y);

    //return intersectDepthPlane(o, d, t);
}

highp float getMinimumDepthPlane(const highp sampler2D vsZBuffer, highp vec2 p, int mipLevel,
        highp vec2 cellCount) {
    // Filament uses a reversed-Z depth buffer (1.0 at near plane, 0.0 at far plane).
    // Invert to [0, 1] (0.0 at near, 1.0 at far) so that closer surfaces have smaller Z values.
    return 1.0 - texelFetch(vsZBuffer, int2(p * cellCount), mipLevel).r;
}

bool crossedCellBoundary(highp vec2 oldCellIdx, highp vec2 newCellIdx) {
    return (oldCellIdx.x != newCellIdx.x) || (oldCellIdx.y != newCellIdx.y);
}

bool traceScreenSpaceRayHiZ(const highp vec3 samplePosInTS, const highp vec3 reflPosInTS,
        const highp vec3 Q0, const highp vec3 Q1, const highp float k0, const highp float k1,
        const highp sampler2D vsZBuffer, const highp vec2 res, const float vsZThickness,
        const highp float maxSteps, out highp vec2 hitPixel, out highp vec3 vsHitPoint) {
    highp vec3 vReflDirInTS = reflPosInTS - samplePosInTS;

    // Compute the maximum distance to trace before the ray goes outside of the visible area [0, 1].
    highp vec3 dist = vec3(1.0);
    dist.x = (vReflDirInTS.x >= 0.0) ?
                ((1.0 - samplePosInTS.x) / vReflDirInTS.x) : (-samplePosInTS.x / vReflDirInTS.x);
    dist.y = (vReflDirInTS.y >= 0.0) ?
                ((1.0 - samplePosInTS.y) / vReflDirInTS.y) : (-samplePosInTS.y / vReflDirInTS.y);
    dist.z = (vReflDirInTS.z >= 0.0) ?
                ((1.0 - samplePosInTS.z) / vReflDirInTS.z) : (-samplePosInTS.z / vReflDirInTS.z);
    highp float maxTraceDistance = clamp(min(min(dist.x, dist.y), dist.z), 0.0, 1.0);

    // Match the structure buffer mipmap count in PostProcessManager::structure().
    int maxLevel = clamp(int(floor(log2(max(res.x, res.y)))) - 5, 0, 7);

    highp vec2 crossStep = vec2(vReflDirInTS.x >= 0.0 ? 1.0 : -1.0,
                                vReflDirInTS.y >= 0.0 ? 1.0 : -1.0);
    highp vec2 crossOffset = crossStep / res / 128.0;
    crossStep = saturate(crossStep);

    highp vec3 ray = samplePosInTS;
    highp float minZ = ray.z;
    highp float maxZ = ray.z + vReflDirInTS.z * maxTraceDistance;
    highp float deltaZ = maxZ - minZ;

    highp vec3 o = ray;
    highp vec3 d = vReflDirInTS * maxTraceDistance;

    int startLevel = min(2, maxLevel);
    int stopLevel = 0;
    highp vec2 startCellCount = getCellCount(vsZBuffer, startLevel);

    highp vec2 rayCell = getCell(ray.xy, startCellCount);
    ray = intersectCellBoundary(o, d, rayCell, startCellCount, crossStep, crossOffset);

    int level = startLevel;
    highp float stepCount = 0.0;
    bool isBackwardRay = vReflDirInTS.z < 0.0;
    highp float rayDir = isBackwardRay ? -1.0 : 1.0;
    //highp float lastCellMinZ = 1.0;

    while (level >= stopLevel &&
           (ray.z * rayDir) <= (maxZ * rayDir) &&
           stepCount < maxSteps) {
        //if (any(lessThan(ray.xy, vec2(0.0))) || any(greaterThan(ray.xy, vec2(1.0)))) {
        //    break;
        //}

        highp vec2 cellCount = getCellCount(vsZBuffer, level);
        highp vec2 oldCellIdx = getCell(ray.xy, cellCount);

        highp float cellMinZ = getMinimumDepthPlane(vsZBuffer, ray.xy, level, cellCount);
        //lastCellMinZ = cellMinZ;
        highp vec3 tmpRay = ((cellMinZ > ray.z) && !isBackwardRay && (deltaZ > 1e-7)) ?
                intersectDepthPlane(o, d, (cellMinZ - minZ) / deltaZ) : ray;

        highp vec2 newCellIdx = getCell(tmpRay.xy, cellCount);

        highp float thickness = (level == 0) ?
                (linearizeDepth(1.0 - cellMinZ) - linearizeDepth(1.0 - ray.z)) : 0.0;
        bool crossed = ((isBackwardRay || (deltaZ <= 1e-7)) && (cellMinZ > ray.z)) ||
                (thickness > vsZThickness) ||
                crossedCellBoundary(oldCellIdx, newCellIdx);

        ray = crossed ?
                intersectCellBoundary(o, d, oldCellIdx, cellCount, crossStep, crossOffset) : tmpRay;
        level = crossed ? min(maxLevel, level + 1) : (level - 1);

        stepCount += 1.0;
    }

    bool intersected = (level < stopLevel);
//            ((ray.z * rayDir) <= (maxZ * rayDir)) &&
//            (lastCellMinZ < 1.0);
    hitPixel = ray.xy * res;

    // Reconstruct the 3D view-space hit point using the shared homogeneous endpoints (Q0, k0) and (Q1, k1).
    highp vec3 absDir = abs(vReflDirInTS);
    highp float alpha = 0.0;
    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        alpha = (absDir.x > 1e-6) ? ((ray.x - samplePosInTS.x) / vReflDirInTS.x) : 0.0;
    } else if (absDir.y >= absDir.z) {
        alpha = (absDir.y > 1e-6) ? ((ray.y - samplePosInTS.y) / vReflDirInTS.y) : 0.0;
    } else {
        alpha = (absDir.z > 1e-6) ? ((ray.z - samplePosInTS.z) / vReflDirInTS.z) : 0.0;
    }
    alpha = clamp(alpha, 0.0, 1.0);
    vsHitPoint = mix(Q0, Q1, alpha) * (1.0 / mix(k0, k1, alpha));

    return intersected;
}

// Note: McGuire and Mara use the "cs" prefix to stand for "camera space", equivalent to Filament's
// "view space". "cs" has been replaced with "vs" to avoid confusion.
bool traceScreenSpaceRay(const highp vec3 vsOrigin, const highp vec3 vsDirection,
        highp_mat4 uvFromViewMatrix, const highp sampler2D vsZBuffer, const highp vec2 res,
        const float vsZThickness, const highp float nearPlaneZ, const float stride,
        const float jitterFraction, const highp float maxSteps, const float maxRayTraceDistance,
        out highp vec2 hitPixel, out highp vec3 vsHitPoint) {
    // Clip ray to a near plane in 3D (doesn't have to be *the* near plane, although that would be a
    // good idea)
    highp float rayLength = ((vsOrigin.z + vsDirection.z * maxRayTraceDistance) > nearPlaneZ) ?
            (nearPlaneZ - vsOrigin.z) / vsDirection.z : maxRayTraceDistance;
    highp vec3 vsEndPoint = vsDirection * rayLength + vsOrigin;

    // Project into texture space (xy in [0, 1] UV, z in [1, 0] reversed-Z depth)
    highp vec4 H0 = mulMat4x4Float3(uvFromViewMatrix, vsOrigin);
    highp vec4 H1 = mulMat4x4Float3(uvFromViewMatrix, vsEndPoint);

    // There are a lot of divisions by w that can be turned into multiplications at some minor
    // precision loss...and we need to interpolate these 1/w values anyway.
    //
    // Because the caller was required to clip to the near plane, this homogeneous division
    // (projecting from 4D to 2D) is guaranteed to succeed.
    highp float k0 = 1.0 / H0.w;
    highp float k1 = 1.0 / H1.w;

    // Switch the original points to values that interpolate linearly in 2D
    highp vec3 Q0 = vsOrigin * k0;
    highp vec3 Q1 = vsEndPoint * k1;

    // Initialize to off screen
    hitPixel = vec2(-1.0, -1.0);

    if (frameUniforms.ssrTracingMode == SSR_TRACING_MODE_HI_Z) {
        highp vec3 samplePosInTS = vec3(H0.xy * k0, 1.0 - H0.z * k0);
        highp vec3 reflPosInTS = vec3(H1.xy * k1, 1.0 - H1.z * k1);
        return traceScreenSpaceRayHiZ(samplePosInTS, reflPosInTS, Q0, Q1, k0, k1,
                vsZBuffer, res, vsZThickness, maxSteps, hitPixel, vsHitPoint);
    }

    // Screen-space endpoints in pixels
    highp vec2 P0 = H0.xy * k0 * res;
    highp vec2 P1 = H1.xy * k1 * res;
    return traceScreenSpaceRayLinear(P0, P1, Q0, Q1, k0, k1, vsOrigin.z,
            vsZBuffer, vsZThickness, stride, jitterFraction, maxSteps, hitPixel, vsHitPoint);
}

/**
 * Evaluates screen-space reflections, returning a color if there's a hit.
 * wsRayDirection is the desired reflected vector.
 *
 * The returned color's alpha is set to a value between [0, 1] representing the "opacity" of the
 * reflection. 1.0 is full screen-space reflection. Values < 1.0 should be blended with the
 * scene's IBL.
 *
 * If there is no hit, the return value is vec4(0).
 */
vec4 evaluateScreenSpaceReflections(const highp vec3 wsRayDirection) {
    vec4 Fr = vec4(0.0);
    highp vec3 wsRayStart = shading_position + frameUniforms.ssrBias * wsRayDirection;

    // ray start/end in view space
    highp vec3 vsOrigin = mulMat4x4Float3(getViewFromWorldMatrix(), wsRayStart).xyz;

    // the view matrix is guaranteed to be a rigid transform
    highp vec3 vsDirection = mulMat3x3Float3(getViewFromWorldMatrix(), wsRayDirection);

    float vsZThickness = frameUniforms.ssrThickness;
    highp float nearPlaneZ = -frameUniforms.nearOverFarMinusNear / frameUniforms.oneOverFarMinusNear;
    float stride = frameUniforms.ssrStride;

    highp vec2 fragCoord = gl_FragCoord.xy;
    fragCoord += vec2(frameUniforms.temporalNoise); // 0 when TAA is not used
    float jitterFraction = interleavedGradientNoise(fragCoord);

    float maxRayTraceDistance = frameUniforms.ssrDistance;

    highp vec2 res = vec2(textureSize(sampler0_structure, 0).xy);

    highp float maxSteps = float(max(res.x, res.y));

    // Outputs from the traceScreenSpaceRay function.
    highp vec2 hitPixel;  // not currently used
    highp vec3 vsHitPoint;

    if (traceScreenSpaceRay(vsOrigin, vsDirection, frameUniforms.ssrUvFromViewMatrix,
            sampler0_structure, res, vsZThickness, nearPlaneZ, stride, jitterFraction, maxSteps,
            maxRayTraceDistance, hitPixel, vsHitPoint)) {
        highp vec4 reprojected = mulMat4x4Float3(frameUniforms.ssrReprojection, vsHitPoint);
        reprojected.xy *= (1.0 / reprojected.w);

        // Compute the screen-space reflection's contribution.

        // TODO: parameterize fadeRate.
        const float fadeRateEdge = 12.0;
        const float fadeRateDistance = 4.0;

        // Fade the reflections out near the edges.
        vec2 edgeFactor = max(fadeRateEdge * abs(reprojected.xy - 0.5) - (fadeRateEdge * 0.5 - 1.0), 0.0);
        float fade = saturate(1.0 - dot(edgeFactor, edgeFactor));

        // Fade the reflections out near maxRayTraceDistance.
        float t = distance(vsOrigin, vsHitPoint) / maxRayTraceDistance;
        fade *= saturate(fadeRateDistance - fadeRateDistance * t);

        // Fade when pointing towards the camera (likely to hit a backface)
        // note: vsDirection.z is the cos(vsDirection, view)
        fade *= (1.0 - max(0.0, vsDirection.z));

        // we output a premultiplied alpha color because this is going to be mipmapped
        Fr = vec4(textureLod(sampler0_ssr, reprojected.xy, 0.0).rgb * fade, fade);
    }
    return Fr;
}

#endif // MATERIAL_HAS_REFLECTIONS
