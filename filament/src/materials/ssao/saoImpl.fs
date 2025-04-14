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
 * This is our implementation of SAO -- it's not standalone because it uses materialParams
 * directly. Therefore it must be included in *.mat file that has all these parameters.
 * The main reason for using a separate file is to be able to have several version of the
 * code with only minor changes.
 */

#include "../utils/geometry.fs"

#ifndef COMPUTE_BENT_NORMAL
#error COMPUTE_BENT_NORMAL must be set
#endif

// fallOffRange.x --> Distance to start fallOff
// fallOffRange.y --> Intensity of the fallOff
// float computeDistanceFade(const float distance) {
//     return saturate(max(0, distance - materialParams.fallOffRange.x) * materialParams.fallOffRange.y);
// }

float integrateArcCosWeight(float h, float n) {
    float Arc = -cos(2.0 * h - n) + cos(n) + 2.0 * h * sin(n);
    return 0.25 * Arc;
}

void groundTruthAmbientOcclusion(out float obscurance, out vec3 bentNormal,
        highp vec2 uv, highp vec3 origin, vec3 normal) {
    vec2 uvSamplePos = uv;
    if (sampleDepthLinear(materialParams_depth, uvSamplePos, 0.0) <= 1e-7){
        obscurance = 0.0;
        return;
    }

    vec3 viewDir = normalize(-origin);
    float ssRadius = -(materialParams.projectionScaleRadius / origin.z);

    float occlusion = 0.0;
    // TODO: Expose this to the parameters
    const float thicknessAttenuation = 0.04;
    for (float i = 0.0; i <= materialParams.sliceCount; i+=1.0) {
        float slice = i / materialParams.sliceCount;
        float phi = slice * PI;
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);
        vec2 omega = vec2(cosPhi, -sinPhi);

        omega *= ssRadius;
        vec3 directionV = vec3(cosPhi, sinPhi, 0.0);
        vec3 orthoDirectionV = directionV - (dot(directionV, viewDir)*viewDir);
        vec3 axisV = normalize(cross(directionV, viewDir));
        vec3 projNormalV = normal - axisV * dot(normal, axisV);

        float signNorm = sign(dot(orthoDirectionV, projNormalV));
        float projNormalLength = length(projNormalV);
        float cosNorm = saturate(dot(projNormalV, viewDir) / projNormalLength);

        // TODO: Implement fast acos and fast sqrt
        float n = signNorm * acos(cosNorm);

        float horizonCos0 = -1.0;
        float horizonCos1 = -1.0;
        for (float j = 0.0; j < materialParams.stepsPerSlice; j+=1.0) {
            float step = j / materialParams.stepsPerSlice;
            vec2 sampleOffset = step * omega;

            // TODO: sample Hi-Z
            float2 sampleScreenPos0 = uv + sampleOffset;
            highp float sampleDepth0 = sampleDepthLinear(materialParams_depth, sampleScreenPos0, 0.0);
            highp vec3 samplePos0 = computeViewSpacePositionFromDepth(sampleScreenPos0, sampleDepth0,
                materialParams.positionParams);

            float2 sampleScreenPos1 = uv - sampleOffset;
            highp float sampleDepth1 = sampleDepthLinear(materialParams_depth, sampleScreenPos1, 0.0);
            highp vec3 samplePos1 = computeViewSpacePositionFromDepth(sampleScreenPos1, sampleDepth1,
                materialParams.positionParams);

            float3 sampleHorizonV0 = normalize(samplePos0 - origin);
            float3 sampleHorizonV1 = normalize(samplePos1 - origin);

            horizonCos0 = max(horizonCos0, dot(sampleHorizonV0, viewDir));
            horizonCos1 = max(horizonCos1, dot(sampleHorizonV1, viewDir));
        }

        float h0 = -acos(horizonCos1);
        float h1 = acos(horizonCos0);
        h0 = n + clamp(h0-n, -PI * 0.5, PI * 0.5);
        h1 = n + clamp(h1-n, -PI * 0.5, PI * 0.5);

        occlusion += projNormalLength * (integrateArcCosWeight(h0, n) + integrateArcCosWeight(h1, n));
    }

    occlusion = saturate(pow(occlusion / materialParams.sliceCount, materialParams.power));
    obscurance = occlusion;

#if COMPUTE_BENT_NORMAL
    bentNormal = normalize(bentNormal);
#endif
}
