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

/*
 * This is our implementation of GTAO -- it's not standalone because it uses materialParams
 * directly. Therefore it must be included in *.mat file that has all these parameters.
 * The main reason for using a separate file is to be able to have several version of the
 * code with only minor changes.
 */

#include "../utils/geometry.fs"

#define rsqrt inversesqrt
#define SECTOR_COUNT 32u

#ifndef COMPUTE_BENT_NORMAL
#error COMPUTE_BENT_NORMAL must be set
#endif

const float kLog2LodRate = 3.0;

// Ambient Occlusion, largely inspired from:
// "Practical Real-Time Strategies for Accurate Indirect Occlusion" by Jimenez et al.
// https://github.com/GameTechDev/XeGTAO
// https://github.com/MaxwellGengYF/Unity-Ground-Truth-Ambient-Occlusion

highp vec3 getViewSpacePosition(vec2 uv, float level) {
    highp float depth = sampleDepthLinear(materialParams_depth, uv, level);
    return computeViewSpacePositionFromDepth(uv, depth, materialParams.positionParams);
}

float integrateArcCosWeight(float h, float n) {
    float arc = -cos(2.0 * h - n) + cos(n) + 2.0 * h * sin(n);
    return 0.25 * arc;
}

// https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf slide 93
float spatialDirectionNoise(float2 uv) {
    int2 position = int2(uv * materialParams.resolution.xy);
	return (1.0/16.0) * (float(((position.x + position.y) & 3) << 2) + float(position.x & 3));
}

// https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf slide 93
float spatialOffsetsNoise(float2 uv) {
	int2 position = int2(uv * materialParams.resolution.xy);
	return 0.25 * float((position.y - position.x) & 3);
}

// If the new sample value is greater then the current one, update the value with some fallOff.
// Otherwise, apply thicknessHeuristic.
float updateHorizon(float sampleHorizonCos, float currentHorizonCos, float fallOff) {
    return sampleHorizonCos > currentHorizonCos
        ? mix(sampleHorizonCos, currentHorizonCos, fallOff)
        : mix(currentHorizonCos, sampleHorizonCos, materialParams.thicknessHeuristic);
}

float calculateHorizonCos(highp vec3 sampleDelta, highp vec3 viewDir, float horizonCos) {
    highp float sqSampleDist = dot(sampleDelta, sampleDelta);
    float invSampleDist = rsqrt(sqSampleDist);

    // Use the view space radius to calculate the fallOff
    float fallOff = saturate(sqSampleDist * materialParams.invRadiusSquared * 2.0);

    // sample horizon cos
    float shc = dot(sampleDelta, viewDir) * invSampleDist;

    return updateHorizon(shc, horizonCos, fallOff);
}

// https://cdrinmatane.github.io/posts/ssaovb-code/
// https://github.com/cdrinmatane/SSRT3/blob/main/HDRP/Shaders/Resources/SSRTCS.compute
highp uint updateSectors(float minHorizon, float maxHorizon, highp uint globalOccludedBitfield) {
    highp uint startHorizonInt = uint(minHorizon * float(SECTOR_COUNT));
    highp uint angleHorizonInt = uint(ceil(saturate(maxHorizon-minHorizon) * float(SECTOR_COUNT)));
    highp uint angleHorizonBitfield = angleHorizonInt > 0u ? (0xFFFFFFFFu >> (SECTOR_COUNT-angleHorizonInt)) : 0u;
    highp uint currentOccludedBitfield = angleHorizonBitfield << startHorizonInt;
    return globalOccludedBitfield | currentOccludedBitfield;
}

// https://cdrinmatane.github.io/posts/ssaovb-code/
// https://github.com/cdrinmatane/SSRT3/blob/main/HDRP/Shaders/Resources/SSRTCS.compute
// The visibility bitmask method replaces the traditional two horizon angles with a bitmask
// for each slice. This bitmask flags whether each sector is occluded or not,
// which enables surfaces to be modeled with constant thickness, overcoming the limitation
// of treating them as a simple height field.
highp uint calculateVisibilityMask(highp vec3 deltaPos, highp vec3 viewDir, float samplingDirection,
    highp uint globalOccludedBitfield, float n, highp vec3 origin) {
    vec2 frontBackHorizon;
    float linearThicknessMultiplier = materialConstants_linearThickness
        ? saturate(origin.z * materialParams.invFarPlane) * 100.0
        : 1.0;
    vec3 deltaPosBackface = deltaPos - viewDir * materialParams.constThickness * linearThicknessMultiplier;

    // Project sample onto the unit circle and compute the angle relative to V
    frontBackHorizon.x = dot(normalize(deltaPos), viewDir);
    frontBackHorizon.y = dot(normalize(deltaPosBackface), viewDir);

    frontBackHorizon.x = acosFast(frontBackHorizon.x);
    frontBackHorizon.y = acosFast(frontBackHorizon.y);

    // Shift sample from V to normal, clamp in [0..1]
    frontBackHorizon = clamp(((samplingDirection * -frontBackHorizon) + n + HALF_PI) / PI, 0.0, 1.0);

    // Sampling direction inverts min/max angles
    frontBackHorizon = samplingDirection >= 0.0 ? frontBackHorizon.yx : frontBackHorizon.xy;

    return updateSectors(frontBackHorizon.x, frontBackHorizon.y, globalOccludedBitfield);
}

struct GtaoSlice {
    vec2 omega;
    vec3 orthoDirection;
    float projectedNormalLength;
    float normalAngle;
    float horizonCos0;
    float horizonCos1;
    highp uint occludedBitfield;
};

GtaoSlice createGtaoSlice(vec2 omega, highp vec3 viewDir, vec3 normal) {
    vec3 direction = vec3(omega, 0.0);
    vec3 orthoDirection = normalize(direction - dot(direction, viewDir) * viewDir);
    vec3 axis = cross(orthoDirection, viewDir);
    vec3 projectedNormal = normal - axis * dot(normal, axis);

    float projectedNormalLength = length(projectedNormal);
    float signNorm = sign(dot(orthoDirection, projectedNormal));
    float cosNorm = saturate(dot(projectedNormal, viewDir) / projectedNormalLength);

    GtaoSlice slice;
    slice.omega = omega;
    slice.orthoDirection = orthoDirection;
    slice.projectedNormalLength = projectedNormalLength;
    slice.normalAngle = signNorm * acosFast(cosNorm);
    slice.horizonCos0 = -1.0;
    slice.horizonCos1 = -1.0;
    slice.occludedBitfield = 0u;
    return slice;
}

void traceGtaoSliceStep(inout GtaoSlice slice, highp vec2 uv, float sampleDistance,
        float level, highp vec3 origin, highp vec3 viewDir) {
    vec2 uvSampleOffset = sampleDistance * slice.omega * materialParams.resolution.zw;

    highp vec3 samplePos0 = getViewSpacePosition(uv + uvSampleOffset, level);
    highp vec3 samplePos1 = getViewSpacePosition(uv - uvSampleOffset, level);

    highp vec3 sampleDelta0 = samplePos0 - origin;
    highp vec3 sampleDelta1 = samplePos1 - origin;

    if (materialConstants_useVisibilityBitmasks) {
        slice.occludedBitfield = calculateVisibilityMask(sampleDelta0, viewDir, 1.0,
                slice.occludedBitfield, slice.normalAngle, origin);
        slice.occludedBitfield = calculateVisibilityMask(sampleDelta1, viewDir, -1.0,
                slice.occludedBitfield, slice.normalAngle, origin);
    } else {
        slice.horizonCos0 = calculateHorizonCos(sampleDelta0, viewDir, slice.horizonCos0);
        slice.horizonCos1 = calculateHorizonCos(sampleDelta1, viewDir, slice.horizonCos1);
    }
}

float integrateGtaoSlice(GtaoSlice slice, highp vec3 viewDir, inout vec3 bentNormal) {
    if (materialConstants_useVisibilityBitmasks) {
        return 1.0 - float(bitCount(slice.occludedBitfield)) / float(SECTOR_COUNT);
    }

    float h0 = -acosFast(slice.horizonCos1);
    float h1 = acosFast(slice.horizonCos0);
    h0 = slice.normalAngle + clamp(h0 - slice.normalAngle, -HALF_PI, HALF_PI);
    h1 = slice.normalAngle + clamp(h1 - slice.normalAngle, -HALF_PI, HALF_PI);

#if COMPUTE_BENT_NORMAL
    float angle = 0.5 * (h0 + h1);
    bentNormal += viewDir * cos(angle) - slice.orthoDirection * sin(angle);
#endif

    return slice.projectedNormalLength *
            (integrateArcCosWeight(h0, slice.normalAngle) +
                    integrateArcCosWeight(h1, slice.normalAngle));
}

void groundTruthAmbientOcclusion(out float obscurance, out vec3 bentNormal,
        highp vec2 uv, highp vec3 origin, vec3 normal) {
    highp vec3 viewDir = normalize(-origin);
    highp float ssRadius = -(materialParams.projectionScaleRadius / origin.z);

    float noiseOffset = spatialOffsetsNoise(uv);
    float noiseDirection = spatialDirectionNoise(uv);

    float initialRayStep = fract(noiseOffset);

    // The distance we want to move forward for each step
    float stepRadius = ssRadius / (materialParams.stepsPerSlice + 1.0);

    float kRadialWarp = materialParams.distributionType == 0 ? 0.0 : materialParams.distributionType == 1 ? 0.5 : 1.0;
    float maxSampleIndex = max(materialParams.stepsPerSlice - 1.0 + initialRayStep, 1.0);
    float maxSampleRadius = maxSampleIndex * stepRadius;

    float visibility = 0.0;
    bool traceFullCross = materialParams.traceFullCross != 0 &&
            mod(materialParams.sliceCount.x, 2.0) == 0.0;
    if (traceFullCross) {
        float slicePairCount = materialParams.sliceCount.x * 0.5;
        for (float i = 0.0; i < slicePairCount; i += 1.0) {
            float slice = (i + noiseDirection) * materialParams.sliceCount.y;
            float phi = slice * PI;
            vec2 omega = vec2(cos(phi), sin(phi));

            GtaoSlice slice0 = createGtaoSlice(omega, viewDir, normal);
            GtaoSlice slice1 = createGtaoSlice(vec2(-omega.y, omega.x), viewDir, normal);

            for (float j = 0.0; j < materialParams.stepsPerSlice; j += 1.0) {
                float sampleIndex = j + initialRayStep;
                float t = sampleIndex / maxSampleIndex;
                float warpedT = mix(t, t * t, kRadialWarp);
                float sampleDistance = max(warpedT * maxSampleRadius, 1.0 + j);
                float sampleOffsetLength = length(sampleDistance * omega);
                float level = clamp(floor(log2(sampleOffsetLength)) - kLog2LodRate,
                        0.0, float(materialParams.maxLevel));

                traceGtaoSliceStep(slice0, uv, sampleDistance, level, origin, viewDir);
                traceGtaoSliceStep(slice1, uv, sampleDistance, level, origin, viewDir);
            }

            visibility += integrateGtaoSlice(slice0, viewDir, bentNormal);
            visibility += integrateGtaoSlice(slice1, viewDir, bentNormal);
        }
    } else {
        for (float i = 0.0; i < materialParams.sliceCount.x; i += 1.0) {
            float slice = (i + noiseDirection) * materialParams.sliceCount.y;
            float phi = slice * PI;
            vec2 omega = vec2(cos(phi), sin(phi));
            GtaoSlice gtaoSlice = createGtaoSlice(omega, viewDir, normal);

            for (float j = 0.0; j < materialParams.stepsPerSlice; j += 1.0) {
                float sampleIndex = j + initialRayStep;
                float t = sampleIndex / maxSampleIndex;
                float warpedT = mix(t, t * t, kRadialWarp);
                float sampleDistance = max(warpedT * maxSampleRadius, 1.0 + j);
                float sampleOffsetLength = length(sampleDistance * omega);
                float level = clamp(floor(log2(sampleOffsetLength)) - kLog2LodRate,
                        0.0, float(materialParams.maxLevel));

                traceGtaoSliceStep(gtaoSlice, uv, sampleDistance, level, origin, viewDir);
            }

            visibility += integrateGtaoSlice(gtaoSlice, viewDir, bentNormal);
        }
    }

    obscurance = 1.0 - saturate(visibility * materialParams.sliceCount.y);

#if COMPUTE_BENT_NORMAL
    bentNormal = normalize(bentNormal);
#endif
}
