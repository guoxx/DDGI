#include "HostDeviceSharedMacros.h"

__import Helpers;
__import BRDF;

cbuffer PerFrameCB
{
    float4x4 gInvProjMat;
    float4x4 gViewMat;
    float4x4 gProjToPixelMat;

    float4 gZBufferSize;
    float4 gHZBSize;

    float gNearZ;
    float gFarZ;
    float gHZBMipCount;

    float gFrameCount;
    float gZThickness;
    float gRayStrideDDA;
    float gJitterFraction;
    float gMaxRayTraceDistance;
    float gMaxStepsHZB;
    float gMaxStepsDDA;
};

Texture2D gColorTex;
Texture2D gHZBTex;
Texture2D gNormalTex;
Texture2D gDiffuseOpacityTex;
Texture2D gSpecRoughTex;
Texture2D gHistoryTex;

SamplerState gPointSampler;

//#define DEBUG_INTERSECTION_RESULT
//#define DEBUG_RAYMARCH_CNT
//#define DEBUG_PIXEL

#ifdef DEBUG_PIXEL
cbuffer DebugCB
{
    int2 gDebugCoord;
};
RWTexture2D gDebugTex;

struct DEBUG_DATA {
float2 P;
float2 P_prime;
float2 hitPixel;
float rayStartZ;
float rayEndZ;
float sceneZMax;
float sceneZMin;
float rayStepSize;
float2 rayStepSizeXy;
float t;
float mipLevel;
};

RWStructuredBuffer<DEBUG_DATA> gDebugData;
#endif


float squaredDistance(float2 p0, float2 p1)
{
    float2 v = p0 - p1;
    return dot(v, v);
}

void swap(in out float a, in out float b)
{
    float tmp = a;
    a = b;
    b = tmp;
}

float3 reflect(float3 Wi, float3 N)
{
    return 2 * dot(Wi, N) * N - Wi;
}

bool isEqual(int2 a, int2 b)
{
    return a.x == b.x && a.y == b.y;
}

bool isEqualToFarPlane(float z)
{
    return abs(z - gFarZ) < 1e-6;
}

float clipRayByZPlane(float3 origin, float3 dir, float rayMaxDist, float zPlane)
{
    return ((origin.z + dir.z * rayMaxDist) > zPlane) ? (zPlane - origin.z) / dir.z : rayMaxDist;
}

// http://jcgt.org/published/0003/04/04/
/**
    \param csOrigin Camera-space ray origin, which must be within the view volume and must have z < -0.01 and project within the valid screen rectangle
    \param csDirection Unit length camera-space ray direction
    \param projectToPixelMatrix A projection matrix that maps to pixel coordinates (not [-1, +1] normalized device coordinates)
    \param HZBTex Camera-space linear Z buffer, negative value
    \param zBufferSize Dimensions of actually depth buffer, NOT the size of HZB
    \param csZThickness Camera space thickness to ascribe to each pixel in the depth buffer
    \param nearPlaneZ Negative number
    \param stride Step in horizontal or vertical pixels between samples. This is a float because integer math is slow on GPUs, but should be set to an integer >= 1
    \param jitterFraction  Number between 0 and 1 for how far to bump the ray in stride units to conceal banding artifacts, plus the stride ray offset. It is recommended to set this to at least 1.0 to avoid self-intersection artifacts. Using 1 + float((int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 1) * 0.5 gives a nice dither pattern when stride is > 1.0;
    \param maxSteps Maximum number of iterations. Higher gives better images but may be slow
    \param maxRayTraceDistance Maximum camera-space distance to trace before returning a miss
    \param hitPixel Pixel coordinates of the first intersection with the scene
    \param csHitPoint Camera space location of the ray hit
*/
bool traceScreenSpaceRay(float3 csOrigin, 
    float3 csDirection,
    float4x4 projectToPixelMatrix,
    Texture2D HZBTex,
    SamplerState pointSampler,
    float2 zBufferSize,
    float csZThickness,
    float nearPlaneZ,
    float stride,
    float jitterFraction,
    float maxSteps,
    float maxRayTraceDistance,
    out float2 hitPixel,
    out float3 csHitPoint,
    out float3 debugColor) {

    // Initialize to off screen
    hitPixel = float2(-1.0, -1.0);
    csHitPoint = float3(0, 0, 0);
    debugColor = 0;

    // Clip ray to a near plane in 3D (doesn't have to be *the* near plane, although that would be a good idea)
    float rayLength = clipRayByZPlane(csOrigin, csDirection, maxRayTraceDistance, nearPlaneZ);
    float3 csEndPoint = csDirection * rayLength + csOrigin;

    // Project into screen space
    float4 H0 = mul(float4(csOrigin, 1.0), projectToPixelMatrix);
    float4 H1 = mul(float4(csEndPoint, 1.0), projectToPixelMatrix);

    // There are a lot of divisions by w that can be turned into multiplications
    // at some minor precision loss...and we need to interpolate these 1/w values
    // anyway.
    //
    // Because the caller was required to clip to the near plane,
    // this homogeneous division (projecting from 4D to 2D) is guaranteed 
    // to succeed. 
    float k0 = 1.0 / H0.w;
    float k1 = 1.0 / H1.w;

    // Switch the original points to values that interpolate linearly in 2D
    float3 Q0 = csOrigin * k0; 
    float3 Q1 = csEndPoint * k1;

    // Screen-space endpoints
    float2 P0 = H0.xy * k0;
    float2 P1 = H1.xy * k1;

#ifdef DEBUG_PIXEL
    bool bDebugPixel = isEqual(gDebugCoord, int2(P0));
#endif

    // If the line is degenerate, make it cover at least one pixel
    // to avoid handling zero-pixel extent as a special case later
    P1 += float2((squaredDistance(P0, P1) < 0.0001) ? 0.01 : 0.0);

    // [Optional clipping to frustum sides here]
    float alpha = clipLineByRect(P0, P1, float4(0.5, 0.5, zBufferSize.x-0.5, zBufferSize.y-0.5));
    P1 = lerp(P0, P1, alpha);
    k1 = lerp(k0, k1, alpha);
    Q1 = lerp(Q0, Q1, alpha);

    // Calculate difference between P1 and P0
    float2 delta = P1 - P0;

    // Permute so that the primary iteration is in x to reduce
    // large branches later
    bool permute = (abs(delta.x) < abs(delta.y));
    if (permute) {
        // More-vertical line. Create a permutation that swaps x and y in the output
        // by directly swizzling the inputs.
        delta = delta.yx;
        P1 = P1.yx;
        P0 = P0.yx;        
    }
    
    // From now on, "x" is the primary iteration direction and "y" is the secondary one
    float stepDirection = sign(delta.x);
    float invdx = stepDirection / delta.x;
    float2 dP = float2(stepDirection, invdx * delta.y);

    // Track the derivatives of Q and k
    float3 dQ = (Q1 - Q0) * invdx;
    float  dk = (k1 - k0) * invdx;
    
    // Because we test 1/2 a texel forward along the ray, on the very last iteration
    // the interpolation can go past the end of the ray. Use these bounds to clamp it.
    float zMin = min(csEndPoint.z, csOrigin.z);
    float zMax = max(csEndPoint.z, csOrigin.z);

    // Scale derivatives by the desired pixel stride
    dP *= stride; dQ *= stride; dk *= stride;

    // Offset the starting values by the jitter fraction
    P0 += dP * jitterFraction; Q0 += dQ * jitterFraction; k0 += dk * jitterFraction;

    // Slide P from P0 to P1, (now-homogeneous) Q from Q0 to Q1, and k from k0 to k1
    float3 Q = Q0;
    float  k = k0;

    // We track the ray depth at +/- 1/2 pixel to treat pixels as clip-space solid 
    // voxels. Because the depth at -1/2 for a given pixel will be the same as at 
    // +1/2 for the previous iteration, we actually only have to compute one value 
    // per iteration.
    float prevZMaxEstimate = csOrigin.z;
    float stepCount = 0.0;
    float rayZMax = prevZMaxEstimate, rayZMin = prevZMaxEstimate;
    float sceneZMax = rayZMin - 1e7;

    // P1.x is never modified after this point, so pre-scale it by 
    // the step direction for a signed comparison
    float end = P1.x * stepDirection;

    // We only advance the z field of Q in the inner loop, since
    // Q.xy is never used until after the loop terminates.

    float2 P;
    for (P = P0;
        ((P.x * stepDirection) <= end) && 
        (stepCount < maxSteps) &&
        // make sure there is no occluder in front of the ray path
        (rayZMin >= sceneZMax) &&
        // ray intersection test
        ((rayZMax < sceneZMax - csZThickness) || (rayZMin > sceneZMax)) &&
        // if ray hit the far plane
        (!isEqualToFarPlane(sceneZMax));
        P += dP, Q.z += dQ.z, k += dk, stepCount += 1.0) {

        // The depth range that the ray covers within this loop
        // iteration.  Assume that the ray is moving in increasing z
        // and swap if backwards.  Because one end of the interval is
        // shared between adjacent iterations, we track the previous
        // value and then swap as needed to ensure correct ordering
        rayZMin = prevZMaxEstimate;

        // Compute the value at 1/2 step into the future
        rayZMax = (dQ.z * 0.5 + Q.z) / (dk * 0.5 + k);
        rayZMax = clamp(rayZMax, zMin, zMax);
        prevZMaxEstimate = rayZMax;

        // Since we don't know if the ray is stepping forward or backward in depth,
        // maybe swap. Note that we preserve our original z "max" estimate first.
        if (rayZMin > rayZMax) { swap(rayZMin, rayZMax); }

        // Camera-space z of the background
        hitPixel = permute ? P.yx : P;
        sceneZMax = HZBTex[int2(hitPixel)].r;

#ifdef DEBUG_PIXEL
        if (bDebugPixel) {
            gDebugTex[hitPixel] = float4(1, 0, 0, 1);
        }
#endif

    } // pixel on ray

#if defined(DEBUG_INTERSECTION_RESULT)
    // It's important to put those code before we undo the last increment
    // Support debugging. This will compile away if debugColor is unused
    if ((P.x * stepDirection) > end) {
        // Hit the max ray distance -> blue
        debugColor = float3(0,0,1);
    } else if (stepCount >= maxSteps) {
        // Ran out of steps -> red
        debugColor = float3(1,0,0);
    } else if (isEqualToFarPlane(sceneZMax)) {
        // Went off screen -> yellow
        debugColor = float3(1,1,0);
    } else {
        bool hit = (rayZMax >= sceneZMax - csZThickness) && (rayZMin <= sceneZMax);
        if (hit) {
            // Encountered a valid hit -> green
            debugColor = float3(0,1,0);
        } else {
            // unknow case -> white
            debugColor = float3(1,1,1);
        }
    }
#endif

    // Undo the last increment, which ran after the test variables
    // were set up.
    P -= dP; Q.z -= dQ.z; k -= dk; stepCount -= 1.0;

    bool hit = (rayZMax >= sceneZMax - csZThickness) && (rayZMin <= sceneZMax);

    // If using non-unit stride and we hit a depth surface...
    if ((stride > 1) && hit) {
        // Refine the hit point within the last large-stride step
        
        // Retreat one whole stride step from the previous loop so that
        // we can re-run that iteration at finer scale
        P -= dP; Q.z -= dQ.z; k -= dk; stepCount -= 1.0;

        // Take the derivatives back to single-pixel stride
        float invStride = 1.0 / stride;
        dP *= invStride; dQ.z *= invStride; dk *= invStride;

        // For this test, we don't bother checking thickness or passing the end, since we KNOW there will
        // be a hit point. As soon as
        // the ray passes behind an object, call it a hit. Advance (stride + 1) steps to fully check this 
        // interval (we could skip the very first iteration, but then we'd need identical code to prime the loop)
        float refinementStepCount = 0;

        // This is the current sample point's z-value, taken back to camera space
        prevZMaxEstimate = Q.z / k;
        rayZMin = prevZMaxEstimate;

        // Ensure that the FOR-loop test passes on the first iteration since we
        // won't have a valid value of sceneZMax to test.
        sceneZMax = rayZMin - 1e7;

        for (;
            (refinementStepCount <= stride*1.4) &&
            (rayZMin > sceneZMax) && (!isEqualToFarPlane(sceneZMax));
            P += dP, Q.z += dQ.z, k += dk, refinementStepCount += 1.0) {

            rayZMin = prevZMaxEstimate;

            // Compute the ray camera-space Z value at 1/2 fine step (pixel) into the future
            rayZMax = (dQ.z * 0.5 + Q.z) / (dk * 0.5 + k);
            rayZMax = clamp(rayZMax, zMin, zMax);

            prevZMaxEstimate = rayZMax;
            rayZMin = min(rayZMax, rayZMin);

            hitPixel = permute ? P.yx : P;
            sceneZMax = HZBTex[int2(hitPixel)].r;
        }

        // Undo the last increment, which happened after the test variables were set up
        Q.z -= dQ.z; refinementStepCount -= 1;

        // Count the refinement steps as fractions of the original stride. Save a register
        // by not retaining invStride until here
        stepCount += refinementStepCount / stride;
      //  debugColor = float3(refinementStepCount / stride);
    } // refinement

    Q.xy += dQ.xy * stepCount;
    csHitPoint = Q * (1.0 / k);

#if defined(DEBUG_RAYMARCH_CNT)
    debugColor = float3(stepCount,0,0);
#endif

    // Does the last point discovered represent a valid hit?
    return hit;
}

bool traceHiZ(float3 csOrigin, 
    float3 csDirection,
    float4x4 projectToPixelMatrix,
    Texture2D HZBTex,
    SamplerState pointSampler,
    float4 zBufferSize,
    float4 HZBSize,
    float csZThickness,
    float nearPlaneZ,
    float jitterFraction,
    float maxSteps,
    float maxRayTraceDistance,
    out float2 hitPixel,
    out float3 csHitPoint,
    out float3 debugColor) {

    // Initialize to off screen
    hitPixel = float2(-1.0, -1.0);
    csHitPoint = float3(0, 0, 0);
    debugColor = 0;

    // Clip ray to a near plane in 3D (doesn't have to be *the* near plane, although that would be a good idea)
    float rayLength = clipRayByZPlane(csOrigin, csDirection, maxRayTraceDistance, nearPlaneZ);
    float3 csEndPoint = csDirection * rayLength + csOrigin;

    // Project into screen space
    float4 H0 = mul(float4(csOrigin, 1.0), projectToPixelMatrix);
    float4 H1 = mul(float4(csEndPoint, 1.0), projectToPixelMatrix);

    // Because the caller was required to clip to the near plane,
    // this homogeneous division (projecting from 4D to 2D) is guaranteed 
    // to succeed. 
    float k0 = 1.0 / H0.w;
    float k1 = 1.0 / H1.w;

    // Switch the original points to values that interpolate linearly in 2D
    float3 Q0 = csOrigin * k0; 
    float3 Q1 = csEndPoint * k1;

    // Screen-space endpoints
    float2 P0 = H0.xy * k0;
    float2 P1 = H1.xy * k1;

#ifdef DEBUG_PIXEL
    bool bDebugPixel = isEqual(gDebugCoord, int2(P0));
#endif

    // If the line is degenerate, make it cover at least one pixel
    // to avoid handling zero-pixel extent as a special case later
    P1 += float2((squaredDistance(P0, P1) < 0.0001) ? 0.01 : 0.0);

    // Clipping to frustum sides
    float alpha = clipLineByRect(P0, P1, float4(0, 0, zBufferSize.x, zBufferSize.y));
    P1 = lerp(P0, P1, alpha);
    k1 = lerp(k0, k1, alpha);
    Q1 = lerp(Q0, Q1, alpha);

    // Calculate difference between P1 and P0
    float2 delta = P1 - P0;

    // Permute so that the primary iteration is in x to reduce
    // large branches later
    bool permute = (abs(delta.x) < abs(delta.y));
    if (permute) {
        // More-vertical line. Create a permutation that swaps x and y in the output
        // by directly swizzling the inputs.
        delta = delta.yx;
        P1 = P1.yx;
        P0 = P0.yx;        
    }
    
    // From now on, "x" is the primary iteration direction and "y" is the secondary one
    const float3 stepDirection = sign(float3(delta, csDirection.z));
    const float invdx = stepDirection.x / delta.x;
    const float2 dP = float2(stepDirection.x, invdx * delta.y);

    // Track the derivatives of Q and k
    const float3 dQ = (Q1 - Q0) * invdx;
    const float  dk = (k1 - k0) * invdx;

    // Offset the starting values by the jitter fraction
    P0 += dP * jitterFraction; Q0 += dQ * jitterFraction; k0 += dk * jitterFraction;

    // Slide P from P0 to P1, (now-homogeneous) Q from Q0 to Q1, and k from k0 to k1
    float2 P = P0;
    float3 Q = Q0;
    float  k = k0;

    // mip information that used for HiZ ray traverse, cellSize equal to 2^mipLevel
    float mipLevel = 2;
    float cellSize = 4.0;

    float rayStartZ = csOrigin.z;
    float rayEndZ = rayStartZ;
    float sceneZMax = 0;
    float stepCount = 0;

#ifdef DEBUG_PIXEL
    if (bDebugPixel) {
        gDebugTex[gDebugCoord] = float4(1, stepCount + 1, 0, 1);

        DEBUG_DATA debugData;
        debugData.P = P0;
        debugData.rayStartZ = rayStartZ;
        debugData.rayEndZ = rayEndZ;
        debugData.rayStepSizeXy = dP;
        debugData.P_prime = 0;
        debugData.sceneZMax = 0;
        debugData.rayStepSize = 0;
        debugData.mipLevel = 0;
        gDebugData[stepCount] = debugData;
    }
#endif

    while (true) {

        stepCount += 1;

        // Make sure ray hit the cell boundary
        float2 signedCellSize = cellSize * stepDirection.xy;
        float2 rayStepSizeXy = 1 - frac(P.xy / signedCellSize);
        // Handle degenerate case: P is located on edge of the cell, move to next cell, pesudo code: stepSize < 1e-6 ? 1.0 : stepSize
        rayStepSizeXy = lerp(rayStepSizeXy, 1.0, step(rayStepSizeXy, 1e-6));
        rayStepSizeXy *= signedCellSize / dP;

        // Step to nearest edge of the cell
        // Clamp to the end of the ray in order to make sure ray stay in front of the near plane
        // so that homogeneous division is guaranteed to succeed. 
        float rayStepSize = min(rayStepSizeXy.x, rayStepSizeXy.y);
        rayStepSize = min(rayStepSize, (P1.x  -  P.x) * stepDirection.x);

        // Early out since ray already reach the end
        if (rayStepSize <= 0) {
            break;
        }

        float2 P_prime = P + dP * rayStepSize;
        float Q_prime_z = Q.z + dQ.z * rayStepSize;
        float k_prime = k + dk * rayStepSize;

        rayStartZ = Q.z / k;
        rayEndZ = Q_prime_z / k_prime;

        float rayZMin = min(rayStartZ, rayEndZ);
        float rayZMax = max(rayStartZ, rayEndZ);

        // Camera-space z of the background
        // In case that both P and P' are located on the edge of the cell, so in order
        // to sample depth correctly, we want to use P for forwad marching, P' for backward marching
        // The situation is demostrated in below, we want to sample the depth cell covered by P->P'
        // Forward:     P----  P'---
        // Backward:    P'---  P----
        // Use mid-point of P and P' so that we can always sample the correct depth of cell in mip
        hitPixel = (P + P_prime) / 2;
        hitPixel = permute ? hitPixel.yx : hitPixel;
        sceneZMax = HZBTex.SampleLevel(gPointSampler, hitPixel*HZBSize.zw, int(mipLevel)).r;

        float stepSize = 0;
        float stepSizeToZPlane = (k * sceneZMax - Q.z) / (dQ.z - dk * sceneZMax);
        if (rayZMin > sceneZMax)
            stepSize = rayStepSize;
        else if (stepDirection.z < 0 && stepSizeToZPlane > 0)
            stepSize = min(rayStepSize, stepSizeToZPlane);

#ifdef DEBUG_PIXEL
        if (bDebugPixel) {
            gDebugTex[hitPixel] = float4(1, stepCount, 0, 1);

            DEBUG_DATA debugData;
            debugData.P = P;
            debugData.P_prime = P_prime;
            debugData.hitPixel = hitPixel;
            debugData.rayStartZ = rayStartZ;
            debugData.rayEndZ = rayEndZ;
            debugData.sceneZMax = sceneZMax;
            debugData.sceneZMin = 0;
            debugData.rayStepSize = rayStepSize;
            debugData.rayStepSizeXy = rayStepSizeXy;
            debugData.mipLevel = mipLevel;
            debugData.t = stepSizeToZPlane;
            gDebugData[stepCount] = debugData;
        }
#endif

        P = P + dP * stepSize;
        Q.z = Q.z + dQ.z * stepSize;
        k = k + dk * stepSize;

        float levelUp = step(sceneZMax, rayZMin);
        mipLevel += 2*levelUp - 1;
        cellSize *= max(0.5, 2*levelUp);

        if (mipLevel < 0 || mipLevel > gHZBMipCount) {
            break;
        }

        if (any(hitPixel.xy < 0) || any(hitPixel.xy > zBufferSize.xy)) {
            break;
        }

        if (isEqualToFarPlane(sceneZMax)) {
            break;
        }

        if (stepCount >= maxSteps) {
            break;
        }
    }

    float rayZMin = min(rayStartZ, rayEndZ);
    float rayZMax = max(rayStartZ, rayEndZ);

#if defined(DEBUG_INTERSECTION_RESULT)
    // Support debugging. This will compile away if debugColor is unused
    if ((P.x - P1.x) * stepDirection.x > 0) {
        // Hit the max ray distance -> blue
        debugColor = float3(0,0,1);
    } else if (stepCount >= maxSteps) {
        // Ran out of steps -> red
        debugColor = float3(1,0,0);
    } else if (isEqualToFarPlane(sceneZMax)) {
        // Went off screen -> yellow
        debugColor = float3(1,1,0);
    } else {
        bool hit = (rayZMax >= sceneZMax - csZThickness) && (rayZMin <= sceneZMax);
        if (hit) {
            // Encountered a valid hit -> green
            debugColor = float3(0,1,0);
        } else {
            // unknow case -> white
            debugColor = float3(1,1,1);
        }
    }
#elif defined(DEBUG_RAYMARCH_CNT)
    debugColor = float3(stepCount,0,0);
#endif

    float stepSize = (P.x - P0.x) * stepDirection.x;
    Q.xy += dQ.xy * stepSize;
    csHitPoint = Q * (1.0 / k);

    // Does the last point discovered represent a valid hit?
    bool hit = (rayZMax >= sceneZMax - csZThickness) && (rayZMin <= sceneZMax);
    return hit && mipLevel < 0;
}

bool rayTrace(float3 rayOrigin, float3 rayDir,
              out float2 hitPixel, out float3 hitPosCS, float3 debugColor)
{
#ifdef HIZ_RAY_TRACE
    bool bHit = traceHiZ(rayOrigin, 
        rayDir,
        gProjToPixelMat,
        gHZBTex,
        gPointSampler,
        gZBufferSize,
        gHZBSize,
        gZThickness,
        gNearZ,
        gJitterFraction,
        gMaxStepsHZB,
        gMaxRayTraceDistance,
        hitPixel,
        hitPosCS,
        debugColor);
#else
    bool bHit = traceScreenSpaceRay(rayOrigin, 
        rayDir,
        gProjToPixelMat,
        gHZBTex,
        gPointSampler,
        gZBufferSize.xy,
        gZThickness,
        gNearZ,
        gRayStrideDDA,
        gJitterFraction,
        gMaxStepsDDA,
        gMaxRayTraceDistance,
        hitPixel,
        hitPosCS,
        debugColor);
#endif

    if (bHit)
    {
        float2 uv = hitPixel * gZBufferSize.zw;
        float3 N = gNormalTex.SampleLevel(gPointSampler, uv, 0).xyz * 2 - 1;
        N = mul(float4(N, 0), gViewMat).xyz;
        // Ray hit upper hemisphere
        return dot(N, rayDir) < 0;
    }

    return false;
}

struct VsOut
{
    float2 texC : TEXCOORD;
    float3 rayDir : TEXCOORD1;
    float4 pos : SV_POSITION;
};

VsOut vsMain(uint id: SV_VertexID)
{
    VsOut vOut; 
	vOut.texC = float2((id & 0x02) * 1.0, (id & 0x01) * 2.0);
	vOut.pos = float4(vOut.texC * float2(2, -2) + float2(-1, 1), 0, 1);

    float4 posCS = mul(vOut.pos, gInvProjMat);
    posCS /= posCS.w;
    vOut.rayDir = posCS.xyz;

    return vOut;
}

float4 psMain(VsOut pIn) : SV_TARGET0
{
    float linearZ = gHZBTex[pIn.pos.xy].r;
    if (isEqualToFarPlane(linearZ))
        return float4(0, 0, 0, 1);

    float3 normal = gNormalTex.SampleLevel(gPointSampler, pIn.texC, 0).xyz * 2 - 1;
    normal = mul(float4(normal, 0), gViewMat).xyz;

    const float3 posCS = pIn.rayDir / pIn.rayDir.z * linearZ;
    const float3 Wo = -normalize(posCS);

    float4 specRough = gSpecRoughTex.SampleLevel(gPointSampler, pIn.texC, 0);
    float3 specularColor = specRough.rgb;
    float linearRoughness = specRough.a;
    float alpha = linearRoughness * linearRoughness;

    uint rndSeed = rand_init(asuint(pIn.pos.x * gFrameCount), asuint(pIn.pos.y * gFrameCount));

    float3 result = 0;
    int iterCnt = 4;
    for (int i = 0; i < iterCnt; ++i)
    {
        float2 rndVal = float2(rand_next(rndSeed), rand_next(rndSeed));
        float3 Wh = getGGXMicrofacet(rndVal, normal, alpha);
        const float3 Wi = reflect(Wo, Wh);
        if (dot(Wi, normal) <= 0) {
            continue;
        }

        float2 hitPixel;
        float3 hitPosCS;
        float3 debugColor;
        if (rayTrace(posCS, Wi, hitPixel, hitPosCS, debugColor))
        {
            float NdotL = saturate(dot(normal, Wi));
            float NdotV = saturate(dot(normal, Wo));
            float NdotH = saturate(dot(normal, Wh));
            float LdotH = saturate(dot(Wi, Wh));

            // Probability in terms of Wi vector, NDF term was cancelled by specular BRDF
            float pdf = NdotH / (4 * LdotH);

            float G = evalSmithGGX(NdotL, NdotV, alpha);
            float3 F = fresnelSchlick(specularColor, 1, LdotH);

            // 4 * NdotL * NdotV cancelled by G term
            // NDF cancelled by pdf
            float3 specularBrdf = G * F;
            float3 sampleWeight = 1.0/iterCnt;

            float2 uv = hitPixel * gZBufferSize.zw;
            float3 Li = gColorTex.SampleLevel(gPointSampler, uv, 0).xyz;

            result.xyz += Li * specularBrdf * NdotL / pdf * sampleWeight;
        }

#if defined(DEBUG_INTERSECTION_RESULT)
        return float4(debugColor, 1);
#elif  defined(DEBUG_RAYMARCH_CNT)
        return float4(debugColor, 1);
#endif
    }

    //float3 historyResult = gHistoryTex.SampleLevel(gPointSampler, pIn.texC, 0).xyz;
    result += gColorTex.SampleLevel(gPointSampler, pIn.texC, 0).xyz;
    //result = result / gFrameCount + historyResult * (1 - 1.0/gFrameCount);
    return float4(result, 1);
}
