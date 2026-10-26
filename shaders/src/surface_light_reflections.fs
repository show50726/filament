//------------------------------------------------------------------------------
// Screen-space reflections
//------------------------------------------------------------------------------

#if defined(MATERIAL_HAS_REFLECTIONS)

#define SSR_TRACING_MODE_LINEAR 0u
#define SSR_TRACING_MODE_HI_Z   1u

#define SSR_TRACE_STATUS_HIT       0u
#define SSR_TRACE_STATUS_MAX_STEPS 1u
#define SSR_TRACE_STATUS_OFFSCREEN 2u
#define SSR_TRACE_STATUS_RAY_END   3u
#define SSR_TRACE_STATUS_NO_DEPTH  4u
#define SSR_TRACE_STATUS_BACKFACE  5u
#define SSR_TRACE_STATUS_ROUGHNESS 6u

vec4 getSsrTraceStatusColor(highp uint status) {
    if (status == SSR_TRACE_STATUS_HIT) {
        return vec4(0.0, 1.0, 0.0, 1.0);
    }
    if (status == SSR_TRACE_STATUS_MAX_STEPS) {
        return vec4(1.0, 0.0, 0.0, 1.0);
    }
    if (status == SSR_TRACE_STATUS_OFFSCREEN) {
        return vec4(0.0, 0.0, 1.0, 1.0);
    }
    if (status == SSR_TRACE_STATUS_RAY_END) {
        return vec4(1.0, 1.0, 0.0, 1.0);
    }
    if (status == SSR_TRACE_STATUS_NO_DEPTH) {
        return vec4(0.0, 1.0, 1.0, 1.0);
    }
    if (status == SSR_TRACE_STATUS_BACKFACE) {
        return vec4(1.0, 0.0, 1.0, 1.0);
    }
    return vec4(0.5, 0.5, 0.5, 1.0);
}

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

bool traceScreenSpaceRayLinear(highp vec2 P0, highp vec2 P1, highp vec3 Q0, highp vec3 Q1,
        highp float k0, highp float k1, const highp float vsOriginZ,
        const highp sampler2D vsZBuffer, const float vsZThickness, const float stride,
        const float jitterFraction, const highp float maxSteps,
        out highp vec2 hitPixel, out highp vec3 vsHitPoint, out highp uint traceStatus,
        out highp float traceHitDistance);

bool traceScreenSpaceRayHiZ(const highp vec3 samplePosInTS, const highp vec3 reflPosInTS,
        const highp vec3 Q0, const highp vec3 Q1, const highp float k0, const highp float k1,
        const highp sampler2D vsZBuffer, const highp vec2 res, const float vsZThickness,
        const highp float maxSteps, out highp vec2 hitPixel, out highp vec3 vsHitPoint,
        out highp uint traceStatus, out highp float traceHitDistance);

// Note: McGuire and Mara use the "cs" prefix to stand for "camera space", equivalent to Filament's
// "view space". "cs" has been replaced with "vs" to avoid confusion.
bool traceScreenSpaceRay(const highp vec3 vsOrigin, const highp vec3 vsDirection,
        highp_mat4 uvFromViewMatrix, const highp sampler2D vsZBuffer, const highp vec2 res,
        const float vsZThickness, const highp float nearPlaneZ, const float stride,
        const float jitterFraction, const highp float maxSteps, const float maxRayTraceDistance,
        out highp vec2 hitPixel, out highp vec3 vsHitPoint, out highp uint traceStatus,
        out highp float traceHitDistance) {
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
                vsZBuffer, res, vsZThickness, maxSteps, hitPixel, vsHitPoint, traceStatus,
                traceHitDistance);
    }

    // Screen-space endpoints in pixels
    highp vec2 P0 = H0.xy * k0 * res;
    highp vec2 P1 = H1.xy * k1 * res;
    return traceScreenSpaceRayLinear(P0, P1, Q0, Q1, k0, k1, vsOrigin.z,
            vsZBuffer, vsZThickness, stride, jitterFraction, maxSteps, hitPixel, vsHitPoint,
            traceStatus, traceHitDistance);
}

highp vec3 reconstructViewPositionFromDepth(highp float depth, highp vec2 uv) {
    // hitPixel is in render-target texture space. This conversion is its own inverse and returns
    // the canonical UV orientation expected by getViewFromClipMatrix().
    uv = uvToRenderTargetUV(uv);
    highp vec4 position = getViewFromClipMatrix() * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return position.xyz * (1.0 / position.w);
}

highp vec3 reconstructViewPosition(const highp sampler2D depthBuffer, highp ivec2 pixel,
        const highp vec2 resolution) {
    highp ivec2 size = ivec2(resolution);
    pixel = clamp(pixel, ivec2(0), size - 1);
    highp float depth = texelFetch(depthBuffer, pixel, 0).r;
    highp vec2 uv = (vec2(pixel) + 0.5) / resolution;
    return reconstructViewPositionFromDepth(depth, uv);
}

bool validateHitNormal(const highp sampler2D depthBuffer, const highp vec2 hitPixel,
        const highp vec2 rayOriginPixel, const highp vec2 resolution,
        const highp vec3 vsRayDirection,
        out highp float facing, out highp vec3 vsSurfacePoint) {
    highp ivec2 centerPixel = clamp(ivec2(hitPixel), ivec2(0), ivec2(resolution) - 1);
    highp float centerDepth = texelFetch(depthBuffer, centerPixel, 0).r;
    highp vec2 centerUv = (vec2(centerPixel) + 0.5) / resolution;
    highp vec3 center = reconstructViewPositionFromDepth(centerDepth, centerUv);
    highp vec2 hitUv = clamp(hitPixel / resolution, vec2(0.0), vec2(1.0));
    vsSurfacePoint = reconstructViewPositionFromDepth(centerDepth, hitUv);

    // Match Godot's hit validation policy: only use a hit normal to reject immediate self-hits.
    // A depth-derived normal is not reliable enough at thin silhouettes to reject distant hits.
    facing = 1.0;
    if (any(greaterThanEqual(abs(hitPixel - rayOriginPixel), vec2(2.0)))) {
        return true;
    }

    highp ivec2 size = ivec2(resolution);
    highp ivec2 left1Pixel = clamp(centerPixel + ivec2(-1, 0), ivec2(0), size - 1);
    highp ivec2 left2Pixel = clamp(centerPixel + ivec2(-2, 0), ivec2(0), size - 1);
    highp ivec2 right1Pixel = clamp(centerPixel + ivec2(1, 0), ivec2(0), size - 1);
    highp ivec2 right2Pixel = clamp(centerPixel + ivec2(2, 0), ivec2(0), size - 1);
    highp ivec2 bottom1Pixel = clamp(centerPixel + ivec2(0, -1), ivec2(0), size - 1);
    highp ivec2 bottom2Pixel = clamp(centerPixel + ivec2(0, -2), ivec2(0), size - 1);
    highp ivec2 top1Pixel = clamp(centerPixel + ivec2(0, 1), ivec2(0), size - 1);
    highp ivec2 top2Pixel = clamp(centerPixel + ivec2(0, 2), ivec2(0), size - 1);

    highp float left1Depth = texelFetch(depthBuffer, left1Pixel, 0).r;
    highp float left2Depth = texelFetch(depthBuffer, left2Pixel, 0).r;
    highp float right1Depth = texelFetch(depthBuffer, right1Pixel, 0).r;
    highp float right2Depth = texelFetch(depthBuffer, right2Pixel, 0).r;
    highp float bottom1Depth = texelFetch(depthBuffer, bottom1Pixel, 0).r;
    highp float bottom2Depth = texelFetch(depthBuffer, bottom2Pixel, 0).r;
    highp float top1Depth = texelFetch(depthBuffer, top1Pixel, 0).r;
    highp float top2Depth = texelFetch(depthBuffer, top2Pixel, 0).r;

    // Compare the second-order depth error on both sides of each axis. This selects the side that
    // most likely belongs to the center surface instead of merely selecting the closest 3D point.
    bool useLeft = abs(2.0 * left1Depth - left2Depth - centerDepth) <
            abs(2.0 * right1Depth - right2Depth - centerDepth);
    bool useBottom = abs(2.0 * bottom1Depth - bottom2Depth - centerDepth) <
            abs(2.0 * top1Depth - top2Depth - centerDepth);
    highp ivec2 horizontalPixel = useLeft ? left1Pixel : right1Pixel;
    highp ivec2 verticalPixel = useBottom ? bottom1Pixel : top1Pixel;
    highp vec3 horizontal = reconstructViewPosition(depthBuffer, horizontalPixel, resolution);
    highp vec3 vertical = reconstructViewPosition(depthBuffer, verticalPixel, resolution);
    highp vec3 horizontalDerivative = (useLeft ? -1.0 : 1.0) * (horizontal - center);
    highp vec3 verticalDerivative = (useBottom ? -1.0 : 1.0) * (vertical - center);
    highp vec3 normal = cross(horizontalDerivative, verticalDerivative);
    highp float normalLengthSquared = dot(normal, normal);
    if (normalLengthSquared <= 1e-12) {
        return true;
    }
    normal *= inversesqrt(normalLengthSquared);

    // Visible geometry is front-facing, so orient the reconstructed normal toward the camera.
    normal *= dot(normal, -center) < 0.0 ? -1.0 : 1.0;
    facing = dot(normal, -vsRayDirection);
    return facing >= 0.0;
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
    if (frameUniforms.ssrTracingMode == SSR_TRACING_MODE_HI_Z) {
        maxSteps = min(maxSteps, float(frameUniforms.ssrMaxSteps));
    }

    // Outputs from the traceScreenSpaceRay function.
    highp vec2 hitPixel;
    highp vec3 vsHitPoint;
    highp uint traceStatus;
    highp float traceHitDistance;

    bool intersected = traceScreenSpaceRay(
            vsOrigin, vsDirection, frameUniforms.ssrUvFromViewMatrix,
            sampler0_structure, res, vsZThickness, nearPlaneZ, stride, jitterFraction, maxSteps,
            maxRayTraceDistance, hitPixel, vsHitPoint, traceStatus, traceHitDistance);
    if (!intersected) {
        return frameUniforms.ssrDebugTraceStatus != 0u ?
                getSsrTraceStatusColor(traceStatus) : Fr;
    }

    highp float hitFacing;
    highp vec3 vsSurfacePoint;
    highp vec4 rayOriginInUv = frameUniforms.ssrUvFromViewMatrix * vec4(vsOrigin, 1.0);
    highp vec2 rayOriginPixel = rayOriginInUv.xy * (1.0 / rayOriginInUv.w) * res;
    if (!validateHitNormal(
            sampler0_structure, hitPixel, rayOriginPixel, res, vsDirection, hitFacing,
            vsSurfacePoint)) {
        return frameUniforms.ssrDebugTraceStatus != 0u ?
                getSsrTraceStatusColor(SSR_TRACE_STATUS_BACKFACE) : Fr;
    }
    if (frameUniforms.ssrDebugTraceStatus != 0u) {
        return getSsrTraceStatusColor(hitFacing > 0.0 ?
                SSR_TRACE_STATUS_HIT : SSR_TRACE_STATUS_BACKFACE);
    }
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

    // Convert the distance between the ray candidate and the actual depth surface into a smooth
    // hit confidence. This retains thickness for traversal robustness without treating every
    // candidate inside that interval as an equally reliable hit.
    highp float hitDistance = traceHitDistance >= 0.0 ?
            traceHitDistance : distance(vsSurfacePoint, vsHitPoint);
    float hitConfidence = 1.0 - smoothstep(
            0.0, max(vsZThickness, 1e-5), hitDistance);
    fade *= hitConfidence * hitConfidence;

    // we output a premultiplied alpha color because this is going to be mipmapped
    Fr = vec4(textureLod(sampler0_ssr, reprojected.xy, 0.0).rgb * fade, fade);
    return Fr;
}

#endif // MATERIAL_HAS_REFLECTIONS
