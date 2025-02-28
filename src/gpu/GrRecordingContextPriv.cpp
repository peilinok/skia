/*
 * Copyright 2020 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/GrRecordingContextPriv.h"

#include "src/gpu/GrCaps.h"
#include "src/gpu/GrDrawingManager.h"

#if SK_GPU_V1
#include "src/gpu/SkGpuDevice.h"
#endif
#if SK_GPU_V2
#include "src/gpu/SkGpuDevice_nga.h"
#endif

sk_sp<SkBaseGpuDevice> GrRecordingContextPriv::createDevice(GrColorType colorType,
                                                            sk_sp<GrSurfaceProxy> proxy,
                                                            sk_sp<SkColorSpace> colorSpace,
                                                            GrSurfaceOrigin origin,
                                                            const SkSurfaceProps& props,
                                                            SkBaseGpuDevice::InitContents init) {
#if GR_TEST_UTILS
    if (this->options().fUseNGA == GrContextOptions::Enable::kYes) {
#if SK_GPU_V2
        return SkGpuDevice_nga::Make(fContext, colorType, std::move(proxy), std::move(colorSpace),
                                     origin, props, init);
#else
        return nullptr;
#endif
    } else
#endif
    {
#if SK_GPU_V1
        return SkGpuDevice::Make(fContext, colorType, std::move(proxy), std::move(colorSpace),
                                 origin, props, init);
#else
        return nullptr;
#endif
    }
}

sk_sp<SkBaseGpuDevice> GrRecordingContextPriv::createDevice(SkBudgeted budgeted,
                                                            const SkImageInfo& ii,
                                                            SkBackingFit fit,
                                                            int sampleCount,
                                                            GrMipmapped mipmapped,
                                                            GrProtected isProtected,
                                                            GrSurfaceOrigin origin,
                                                            const SkSurfaceProps& props,
                                                            SkBaseGpuDevice::InitContents init) {
#if GR_TEST_UTILS
    if (this->options().fUseNGA == GrContextOptions::Enable::kYes) {
#if SK_GPU_V2
        return SkGpuDevice_nga::Make(fContext, budgeted, ii, fit, sampleCount,
                                     mipmapped, isProtected, origin, props, init);
#else
        return nullptr;
#endif
    } else
#endif
    {
#if SK_GPU_V1
        return SkGpuDevice::Make(fContext, budgeted, ii, fit, sampleCount,
                                 mipmapped, isProtected, origin, props, init);
#else
        return nullptr;
#endif
    }
}

void GrRecordingContextPriv::moveRenderTasksToDDL(SkDeferredDisplayList* ddl) {
    fContext->drawingManager()->moveRenderTasksToDDL(ddl);
}

GrSDFTControl GrRecordingContextPriv::getSDFTControl(bool useSDFTForSmallText) const {
    return GrSDFTControl{
            this->caps()->shaderCaps()->supportsDistanceFieldText(),
            useSDFTForSmallText,
            this->options().fMinDistanceFieldFontSize,
            this->options().fGlyphsAsPathsFontSize};
}
