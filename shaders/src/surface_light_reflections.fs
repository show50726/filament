//------------------------------------------------------------------------------
// Screen-space reflections
//------------------------------------------------------------------------------

#if defined(MATERIAL_HAS_REFLECTIONS)

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

// Note: McGuire and Mara use the "cs" prefix to stand for "camera space", equivalent to Filament's
// "view space". "cs" has been replaced with "vs" to avoid confusion.
bool traceScreenSpaceRayLinear(const highp vec3 vsOrigin, const highp vec3 vsDirection,
        highp_mat4 uvFromViewMatrix, const highp sampler2D vsZBuffer,
        const float vsZThickness, const highp float nearPlaneZ, const float stride,
        const float jitterFraction, const highp float maxSteps, const float maxRayTraceDistance,
        out highp vec2 hitPixel, out highp vec3 vsHitPoint) {
    // Clip ray to a near plane in 3D (doesn't have to be *the* near plane, although that would be a
    // good idea)
    highp float rayLength = ((vsOrigin.z + vsDirection.z * maxRayTraceDistance) > nearPlaneZ) ?
            (nearPlaneZ - vsOrigin.z) / vsDirection.z : maxRayTraceDistance;
    highp vec3 vsEndPoint = vsDirection * rayLength + vsOrigin;

    // Project into screen space
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

    // Screen-space endpoints
    highp vec2 P0 = H0.xy * k0;
    highp vec2 P1 = H1.xy * k1;

    // TODO:
    // [Optional clipping to frustum sides here]

    // Initialize to off screen
    hitPixel = vec2(-1.0, -1.0);

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
    highp float prevZMaxEstimate = vsOrigin.z;
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

bool traceScreenSpaceRayHierarchical(const highp vec3 vsOrigin, const highp vec3 vsDirection,
        highp_mat4 uvFromViewMatrix, const highp sampler2D vsZBuffer,
        const float vsZThickness, const highp float nearPlaneZ, const float jitterFraction,
        const highp float maxSteps, const float maxRayTraceDistance,
        out highp vec2 hitPixel, out highp vec3 vsHitPoint) {
    highp float rayLength = ((vsOrigin.z + vsDirection.z * maxRayTraceDistance) > nearPlaneZ) ?
            (nearPlaneZ - vsOrigin.z) / vsDirection.z : maxRayTraceDistance;
    highp vec3 vsEndPoint = vsDirection * rayLength + vsOrigin;

    highp vec4 H0 = mulMat4x4Float3(uvFromViewMatrix, vsOrigin);
    highp vec4 H1 = mulMat4x4Float3(uvFromViewMatrix, vsEndPoint);
    highp float k0 = 1.0 / H0.w;
    highp float k1 = 1.0 / H1.w;
    highp vec3 Q0 = vsOrigin * k0;
    highp vec3 Q1 = vsEndPoint * k1;
    highp vec2 P0 = H0.xy * k0;
    highp vec2 P1 = H1.xy * k1;

    hitPixel = vec2(-1.0);
    P1 += vec2((distanceSquared(P0, P1) < 0.0001) ? 0.01 : 0.0);

    highp vec2 delta = P1 - P0;
    bool permute = false;
    if (abs(delta.x) < abs(delta.y)) {
        permute = true;
        delta = delta.yx;
        P1 = P1.yx;
        P0 = P0.yx;
    }

    float stepDirection = sign(delta.x);
    highp float invdx = stepDirection / delta.x;
    highp vec2 dP = vec2(stepDirection, invdx * delta.y);
    highp vec3 dQ = (Q1 - Q0) * invdx;
    highp float dk = (k1 - k0) * invdx;

    P0 += dP * jitterFraction;
    Q0 += dQ * jitterFraction;
    k0 += dk * jitterFraction;

    highp vec2 P = P0;
    highp vec3 Q = Q0;
    highp float k = k0;
    highp float end = P1.x * stepDirection;

    highp vec2 resolution = vec2(textureSize(vsZBuffer, 0));
    int maxMip = min(7, int(floor(log2(max(resolution.x, resolution.y)))));
    int mip = min(2, maxMip);
    highp float iterationLimit = maxSteps * float(maxMip + 2);

    for (highp float iteration = 0.0;
            iteration < iterationLimit && P.x * stepDirection <= end;
            iteration += 1.0) {
        highp vec2 pixel = permute ? P.yx : P;
        highp vec2 pixelStep = permute ? dP.yx : dP;
        highp vec2 samplePixel = pixel + sign(pixelStep) * 0.01;
        if (any(lessThan(samplePixel, vec2(0.0))) ||
                any(greaterThanEqual(samplePixel, resolution))) {
            break;
        }

        highp ivec2 levelSize = textureSize(vsZBuffer, mip);
        highp ivec2 cell = ivec2(floor(samplePixel * vec2(levelSize) / resolution));
        cell = clamp(cell, ivec2(0), levelSize - 1);

        highp vec2 cellMin = vec2(cell) * resolution / vec2(levelSize);
        highp vec2 cellMax = vec2(cell + 1) * resolution / vec2(levelSize);
        highp vec2 boundary = mix(cellMin, cellMax, greaterThan(pixelStep, vec2(0.0)));
        highp float tx = abs(pixelStep.x) > 0.00001 ?
                (boundary.x - pixel.x) / pixelStep.x : 1e20;
        highp float ty = abs(pixelStep.y) > 0.00001 ?
                (boundary.y - pixel.y) / pixelStep.y : 1e20;
        highp float advance = max(min(tx, ty) + 0.01, 0.01);
        highp float remaining = max((P1.x - P.x) / dP.x, 0.0);
        advance = min(advance, remaining);
        if (advance <= 0.0) {
            break;
        }

        highp vec3 nextQ = Q + dQ * advance;
        highp float nextK = k + dk * advance;
        highp float rayZ0 = Q.z / k;
        highp float rayZ1 = nextQ.z / nextK;
        highp float rayZMin = min(rayZ0, rayZ1);
        highp float rayZMax = max(rayZ0, rayZ1);
        highp float sceneZMax = linearizeDepth(texelFetch(vsZBuffer, cell, mip).r);

        if (mip > 0 && rayZMin <= sceneZMax) {
            mip--;
            continue;
        }

        if (mip == 0 && rayZMax >= sceneZMax - vsZThickness && rayZMin <= sceneZMax) {
            highp float rayZDelta = rayZ1 - rayZ0;
            highp float hitFraction = abs(rayZDelta) > 0.00001 ?
                    saturate((sceneZMax - rayZ0) / rayZDelta) : 0.0;
            highp vec3 hitQ = mix(Q, nextQ, hitFraction);
            highp float hitK = mix(k, nextK, hitFraction);
            vsHitPoint = hitQ * (1.0 / hitK);
            highp vec2 nextPixel = pixel + pixelStep * advance;
            hitPixel = mix(pixel, nextPixel, hitFraction);
            return true;
        }

        P += dP * advance;
        Q = nextQ;
        k = nextK;
        mip = min(mip + 1, maxMip);
    }

    vsHitPoint = Q * (1.0 / k);
    return false;
}

bool traceScreenSpaceRay(const highp vec3 vsOrigin, const highp vec3 vsDirection,
        highp_mat4 uvFromViewMatrix, const highp sampler2D vsZBuffer,
        const float vsZThickness, const highp float nearPlaneZ, const float stride,
        const float jitterFraction, const highp float maxSteps, const float maxRayTraceDistance,
        out highp vec2 hitPixel, out highp vec3 vsHitPoint) {
    if (frameUniforms.ssrTracingMode == 1u) {
        return traceScreenSpaceRayHierarchical(vsOrigin, vsDirection, uvFromViewMatrix, vsZBuffer,
                vsZThickness, nearPlaneZ, jitterFraction, maxSteps, maxRayTraceDistance,
                hitPixel, vsHitPoint);
    }
    return traceScreenSpaceRayLinear(vsOrigin, vsDirection, uvFromViewMatrix, vsZBuffer,
            vsZThickness, nearPlaneZ, stride, jitterFraction, maxSteps, maxRayTraceDistance,
            hitPixel, vsHitPoint);
}

highp mat4 scaleMatrix(const highp float x, const highp float y) {
    mat4 m = mat4(1.0);
    m[0].x = x;
    m[1].y = y;
    m[2].z = 1.0;
    m[3].w = 1.0;
    return m;
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
    highp mat4 uvFromViewMatrix =
        scaleMatrix(res.x, res.y) *
        frameUniforms.ssrUvFromViewMatrix;

    highp float maxSteps = float(max(res.x, res.y));

    // Outputs from the traceScreenSpaceRay function.
    highp vec2 hitPixel;  // not currently used
    highp vec3 vsHitPoint;

    if (traceScreenSpaceRay(vsOrigin, vsDirection, uvFromViewMatrix, sampler0_structure,
            vsZThickness, nearPlaneZ, stride, jitterFraction, maxSteps,
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
