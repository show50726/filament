//------------------------------------------------------------------------------
// Hi-Z screen-space reflection tracing
// Reference: Sugu Lee, "Screen Space Reflections: Implementation and
// optimization - Part 2: HI-Z Tracing Method"
//------------------------------------------------------------------------------

#if defined(MATERIAL_HAS_REFLECTIONS)

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
    highp vec2 index = cell + crossStep;
    highp vec2 boundary = index / cellCount;
    boundary += crossOffset;

    highp vec2 delta = boundary - o.xy;
    delta.x = abs(d.x) > 1e-8 ? (delta.x / d.x) : 1e6;
    delta.y = abs(d.y) > 1e-8 ? (delta.y / d.y) : 1e6;
    highp float t = min(delta.x, delta.y);

    return intersectDepthPlane(o, d, t);
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
        const highp float maxSteps, out highp vec2 hitPixel, out highp vec3 vsHitPoint,
        out highp uint traceStatus, out highp float traceHitDistance) {
    highp vec3 vReflDirInTS = reflPosInTS - samplePosInTS;

    // Compute the maximum distance to trace before the ray goes outside of the visible area [0, 1].
    highp vec3 dist = vec3(1.0);
    if (abs(vReflDirInTS.x) > 1e-6) {
        dist.x = (vReflDirInTS.x >= 0.0) ?
                ((1.0 - samplePosInTS.x) / vReflDirInTS.x) : (-samplePosInTS.x / vReflDirInTS.x);
    }
    if (abs(vReflDirInTS.y) > 1e-6) {
        dist.y = (vReflDirInTS.y >= 0.0) ?
                ((1.0 - samplePosInTS.y) / vReflDirInTS.y) : (-samplePosInTS.y / vReflDirInTS.y);
    }
    if (abs(vReflDirInTS.z) > 1e-6) {
        dist.z = (vReflDirInTS.z >= 0.0) ?
                ((1.0 - samplePosInTS.z) / vReflDirInTS.z) : (-samplePosInTS.z / vReflDirInTS.z);
    }
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
    highp float lastCellMinZ = 1.0;
    bool leftViewport = false;

    while (level >= stopLevel &&
           (ray.z * rayDir) <= (maxZ * rayDir) &&
           stepCount < maxSteps) {
        if (any(lessThan(ray.xy, vec2(0.0))) || any(greaterThan(ray.xy, vec2(1.0)))) {
            leftViewport = true;
            break;
        }

        highp vec2 cellCount = getCellCount(vsZBuffer, level);
        highp vec2 oldCellIdx = getCell(ray.xy, cellCount);

        highp float cellMinZ = getMinimumDepthPlane(vsZBuffer, ray.xy, level, cellCount);
        lastCellMinZ = cellMinZ;
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

    bool intersected = (level < stopLevel) &&
            ((ray.z * rayDir) <= (maxZ * rayDir)) &&
            (lastCellMinZ < 1.0);
    traceStatus = intersected ? SSR_TRACE_STATUS_HIT :
            leftViewport ? SSR_TRACE_STATUS_OFFSCREEN :
            stepCount >= maxSteps ? SSR_TRACE_STATUS_MAX_STEPS :
            lastCellMinZ >= 1.0 ? SSR_TRACE_STATUS_NO_DEPTH : SSR_TRACE_STATUS_RAY_END;
    hitPixel = ray.xy * res;

    // Reconstruct the 3D view-space hit point using the shared homogeneous endpoints (Q0, k0) and
    // (Q1, k1).
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
    // Hi-Z returns a candidate at the traversed texture-space position. Ask the common validation
    // path to compare it against the reconstructed depth surface in three dimensions.
    traceHitDistance = -1.0;

    return intersected;
}

#endif // MATERIAL_HAS_REFLECTIONS
