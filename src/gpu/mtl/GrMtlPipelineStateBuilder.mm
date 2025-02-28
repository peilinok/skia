/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/mtl/GrMtlPipelineStateBuilder.h"

#include "include/gpu/GrDirectContext.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkTraceEvent.h"
#include "src/core/SkWriteBuffer.h"
#include "src/gpu/GrAutoLocaleSetter.h"
#include "src/gpu/GrDirectContextPriv.h"
#include "src/gpu/GrPersistentCacheUtils.h"
#include "src/gpu/GrRenderTarget.h"
#include "src/gpu/GrShaderUtils.h"

#include "src/gpu/mtl/GrMtlGpu.h"
#include "src/gpu/mtl/GrMtlPipelineState.h"
#include "src/gpu/mtl/GrMtlUtil.h"

#import <simd/simd.h>

#if !__has_feature(objc_arc)
#error This file must be compiled with Arc. Use -fobjc-arc flag
#endif

GR_NORETAIN_BEGIN

GrMtlPipelineState* GrMtlPipelineStateBuilder::CreatePipelineState(
        GrMtlGpu* gpu, const GrProgramDesc& desc, const GrProgramInfo& programInfo,
        const GrMtlPrecompiledLibraries* precompiledLibs) {
    GrAutoLocaleSetter als("C");
    GrMtlPipelineStateBuilder builder(gpu, desc, programInfo);

    if (!builder.emitAndInstallProcs()) {
        return nullptr;
    }
    return builder.finalize(desc, programInfo, precompiledLibs);
}

GrMtlPipelineStateBuilder::GrMtlPipelineStateBuilder(GrMtlGpu* gpu,
                                                     const GrProgramDesc& desc,
                                                     const GrProgramInfo& programInfo)
        : INHERITED(desc, programInfo)
        , fGpu(gpu)
        , fUniformHandler(this)
        , fVaryingHandler(this) {
}

const GrCaps* GrMtlPipelineStateBuilder::caps() const {
    return fGpu->caps();
}

SkSL::Compiler* GrMtlPipelineStateBuilder::shaderCompiler() const {
    return fGpu->shaderCompiler();
}

void GrMtlPipelineStateBuilder::finalizeFragmentOutputColor(GrShaderVar& outputColor) {
    outputColor.addLayoutQualifier("location = 0, index = 0");
}

void GrMtlPipelineStateBuilder::finalizeFragmentSecondaryColor(GrShaderVar& outputColor) {
    outputColor.addLayoutQualifier("location = 0, index = 1");
}

static constexpr SkFourByteTag kMSL_Tag = SkSetFourByteTag('M', 'S', 'L', ' ');
static constexpr SkFourByteTag kSKSL_Tag = SkSetFourByteTag('S', 'K', 'S', 'L');

void GrMtlPipelineStateBuilder::storeShadersInCache(const SkSL::String shaders[],
                                                    const SkSL::Program::Inputs inputs[],
                                                    SkSL::Program::Settings* settings,
                                                    sk_sp<SkData> pipelineData,
                                                    bool isSkSL) {
    sk_sp<SkData> key = SkData::MakeWithoutCopy(this->desc().asKey(),
                                                this->desc().keyLength());
    SkString description = GrProgramDesc::Describe(fProgramInfo, *this->caps());
    // cache metadata to allow for a complete precompile in either case
    GrPersistentCacheUtils::ShaderMetadata meta;
    meta.fSettings = settings;
    meta.fPlatformData = std::move(pipelineData);
    SkFourByteTag tag = isSkSL ? kSKSL_Tag : kMSL_Tag;
    sk_sp<SkData> data = GrPersistentCacheUtils::PackCachedShaders(tag, shaders, inputs,
                                                                   kGrShaderTypeCount, &meta);
    fGpu->getContext()->priv().getPersistentCache()->store(*key, *data, description);
}

id<MTLLibrary> GrMtlPipelineStateBuilder::compileMtlShaderLibrary(
        const SkSL::String& shader, SkSL::Program::Inputs inputs,
        GrContextOptions::ShaderErrorHandler* errorHandler) {
    id<MTLLibrary> shaderLibrary = GrCompileMtlShaderLibrary(fGpu, shader, errorHandler);
    if (shaderLibrary != nil && inputs.fRTHeight) {
        this->addRTHeightUniform(SKSL_RTHEIGHT_NAME);
    }
    return shaderLibrary;
}

static inline MTLVertexFormat attribute_type_to_mtlformat(GrVertexAttribType type) {
    switch (type) {
        case kFloat_GrVertexAttribType:
            return MTLVertexFormatFloat;
        case kFloat2_GrVertexAttribType:
            return MTLVertexFormatFloat2;
        case kFloat3_GrVertexAttribType:
            return MTLVertexFormatFloat3;
        case kFloat4_GrVertexAttribType:
            return MTLVertexFormatFloat4;
        case kHalf_GrVertexAttribType:
            if (@available(macOS 10.13, iOS 11.0, *)) {
                return MTLVertexFormatHalf;
            } else {
                return MTLVertexFormatInvalid;
            }
        case kHalf2_GrVertexAttribType:
            return MTLVertexFormatHalf2;
        case kHalf4_GrVertexAttribType:
            return MTLVertexFormatHalf4;
        case kInt2_GrVertexAttribType:
            return MTLVertexFormatInt2;
        case kInt3_GrVertexAttribType:
            return MTLVertexFormatInt3;
        case kInt4_GrVertexAttribType:
            return MTLVertexFormatInt4;
        case kByte_GrVertexAttribType:
            if (@available(macOS 10.13, iOS 11.0, *)) {
                return MTLVertexFormatChar;
            } else {
                return MTLVertexFormatInvalid;
            }
        case kByte2_GrVertexAttribType:
            return MTLVertexFormatChar2;
        case kByte4_GrVertexAttribType:
            return MTLVertexFormatChar4;
        case kUByte_GrVertexAttribType:
            if (@available(macOS 10.13, iOS 11.0, *)) {
                return MTLVertexFormatUChar;
            } else {
                return MTLVertexFormatInvalid;
            }
        case kUByte2_GrVertexAttribType:
            return MTLVertexFormatUChar2;
        case kUByte4_GrVertexAttribType:
            return MTLVertexFormatUChar4;
        case kUByte_norm_GrVertexAttribType:
            if (@available(macOS 10.13, iOS 11.0, *)) {
                return MTLVertexFormatUCharNormalized;
            } else {
                return MTLVertexFormatInvalid;
            }
        case kUByte4_norm_GrVertexAttribType:
            return MTLVertexFormatUChar4Normalized;
        case kShort2_GrVertexAttribType:
            return MTLVertexFormatShort2;
        case kShort4_GrVertexAttribType:
            return MTLVertexFormatShort4;
        case kUShort2_GrVertexAttribType:
            return MTLVertexFormatUShort2;
        case kUShort2_norm_GrVertexAttribType:
            return MTLVertexFormatUShort2Normalized;
        case kInt_GrVertexAttribType:
            return MTLVertexFormatInt;
        case kUint_GrVertexAttribType:
            return MTLVertexFormatUInt;
        case kUShort_norm_GrVertexAttribType:
            if (@available(macOS 10.13, iOS 11.0, *)) {
                return MTLVertexFormatUShortNormalized;
            } else {
                return MTLVertexFormatInvalid;
            }
        case kUShort4_norm_GrVertexAttribType:
            return MTLVertexFormatUShort4Normalized;
    }
    SK_ABORT("Unknown vertex attribute type");
}

static MTLVertexDescriptor* create_vertex_descriptor(const GrGeometryProcessor& geomProc,
                                                     SkBinaryWriteBuffer* writer) {
    uint32_t vertexBinding = 0, instanceBinding = 0;

    int nextBinding = GrMtlUniformHandler::kLastUniformBinding + 1;
    if (geomProc.hasVertexAttributes()) {
        vertexBinding = nextBinding++;
    }

    if (geomProc.hasInstanceAttributes()) {
        instanceBinding = nextBinding;
    }
    if (writer) {
        writer->writeUInt(vertexBinding);
        writer->writeUInt(instanceBinding);
    }

    auto vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    int attributeIndex = 0;

    int vertexAttributeCount = geomProc.numVertexAttributes();
    if (writer) {
        writer->writeInt(vertexAttributeCount);
    }
    size_t vertexAttributeOffset = 0;
    for (const auto& attribute : geomProc.vertexAttributes()) {
        MTLVertexAttributeDescriptor* mtlAttribute = vertexDescriptor.attributes[attributeIndex];
        MTLVertexFormat format = attribute_type_to_mtlformat(attribute.cpuType());
        SkASSERT(MTLVertexFormatInvalid != format);
        mtlAttribute.format = format;
        mtlAttribute.offset = vertexAttributeOffset;
        mtlAttribute.bufferIndex = vertexBinding;
        if (writer) {
            writer->writeInt(format);
            writer->writeUInt(vertexAttributeOffset);
            writer->writeUInt(vertexBinding);
        }

        vertexAttributeOffset += attribute.sizeAlign4();
        attributeIndex++;
    }
    SkASSERT(vertexAttributeOffset == geomProc.vertexStride());

    if (vertexAttributeCount) {
        MTLVertexBufferLayoutDescriptor* vertexBufferLayout =
                vertexDescriptor.layouts[vertexBinding];
        vertexBufferLayout.stepFunction = MTLVertexStepFunctionPerVertex;
        vertexBufferLayout.stepRate = 1;
        vertexBufferLayout.stride = vertexAttributeOffset;
        if (writer) {
            writer->writeUInt(vertexAttributeOffset);
        }
    }

    int instanceAttributeCount = geomProc.numInstanceAttributes();
    if (writer) {
        writer->writeInt(instanceAttributeCount);
    }
    size_t instanceAttributeOffset = 0;
    for (const auto& attribute : geomProc.instanceAttributes()) {
        MTLVertexAttributeDescriptor* mtlAttribute = vertexDescriptor.attributes[attributeIndex];
        MTLVertexFormat format = attribute_type_to_mtlformat(attribute.cpuType());
        SkASSERT(MTLVertexFormatInvalid != format);
        mtlAttribute.format = format;
        mtlAttribute.offset = instanceAttributeOffset;
        mtlAttribute.bufferIndex = instanceBinding;
        if (writer) {
            writer->writeInt(format);
            writer->writeUInt(instanceAttributeOffset);
            writer->writeUInt(instanceBinding);
        }

        instanceAttributeOffset += attribute.sizeAlign4();
        attributeIndex++;
    }
    SkASSERT(instanceAttributeOffset == geomProc.instanceStride());

    if (instanceAttributeCount) {
        MTLVertexBufferLayoutDescriptor* instanceBufferLayout =
                vertexDescriptor.layouts[instanceBinding];
        instanceBufferLayout.stepFunction = MTLVertexStepFunctionPerInstance;
        instanceBufferLayout.stepRate = 1;
        instanceBufferLayout.stride = instanceAttributeOffset;
        if (writer) {
            writer->writeUInt(instanceAttributeOffset);
        }
    }
    return vertexDescriptor;
}

static MTLBlendFactor blend_coeff_to_mtl_blend(GrBlendCoeff coeff) {
    switch (coeff) {
        case kZero_GrBlendCoeff:
            return MTLBlendFactorZero;
        case kOne_GrBlendCoeff:
            return MTLBlendFactorOne;
        case kSC_GrBlendCoeff:
            return MTLBlendFactorSourceColor;
        case kISC_GrBlendCoeff:
            return MTLBlendFactorOneMinusSourceColor;
        case kDC_GrBlendCoeff:
            return MTLBlendFactorDestinationColor;
        case kIDC_GrBlendCoeff:
            return MTLBlendFactorOneMinusDestinationColor;
        case kSA_GrBlendCoeff:
            return MTLBlendFactorSourceAlpha;
        case kISA_GrBlendCoeff:
            return MTLBlendFactorOneMinusSourceAlpha;
        case kDA_GrBlendCoeff:
            return MTLBlendFactorDestinationAlpha;
        case kIDA_GrBlendCoeff:
            return MTLBlendFactorOneMinusDestinationAlpha;
        case kConstC_GrBlendCoeff:
            return MTLBlendFactorBlendColor;
        case kIConstC_GrBlendCoeff:
            return MTLBlendFactorOneMinusBlendColor;
        case kS2C_GrBlendCoeff:
            if (@available(macOS 10.12, iOS 11.0, *)) {
                return MTLBlendFactorSource1Color;
            } else {
                return MTLBlendFactorZero;
            }
        case kIS2C_GrBlendCoeff:
            if (@available(macOS 10.12, iOS 11.0, *)) {
                return MTLBlendFactorOneMinusSource1Color;
            } else {
                return MTLBlendFactorZero;
            }
        case kS2A_GrBlendCoeff:
            if (@available(macOS 10.12, iOS 11.0, *)) {
                return MTLBlendFactorSource1Alpha;
            } else {
                return MTLBlendFactorZero;
            }
        case kIS2A_GrBlendCoeff:
            if (@available(macOS 10.12, iOS 11.0, *)) {
                return MTLBlendFactorOneMinusSource1Alpha;
            } else {
                return MTLBlendFactorZero;
            }
        case kIllegal_GrBlendCoeff:
            return MTLBlendFactorZero;
    }

    SK_ABORT("Unknown blend coefficient");
}

static MTLBlendOperation blend_equation_to_mtl_blend_op(GrBlendEquation equation) {
    static const MTLBlendOperation gTable[] = {
        MTLBlendOperationAdd,              // kAdd_GrBlendEquation
        MTLBlendOperationSubtract,         // kSubtract_GrBlendEquation
        MTLBlendOperationReverseSubtract,  // kReverseSubtract_GrBlendEquation
    };
    static_assert(SK_ARRAY_COUNT(gTable) == kFirstAdvancedGrBlendEquation);
    static_assert(0 == kAdd_GrBlendEquation);
    static_assert(1 == kSubtract_GrBlendEquation);
    static_assert(2 == kReverseSubtract_GrBlendEquation);

    SkASSERT((unsigned)equation < kGrBlendEquationCnt);
    return gTable[equation];
}

static MTLRenderPipelineColorAttachmentDescriptor* create_color_attachment(
        MTLPixelFormat format, const GrPipeline& pipeline, SkBinaryWriteBuffer* writer) {
    auto mtlColorAttachment = [[MTLRenderPipelineColorAttachmentDescriptor alloc] init];

    // pixel format
    mtlColorAttachment.pixelFormat = format;
    if (writer) {
        writer->writeInt(format);
    }

    // blending
    const GrXferProcessor::BlendInfo& blendInfo = pipeline.getXferProcessor().getBlendInfo();

    GrBlendEquation equation = blendInfo.fEquation;
    GrBlendCoeff srcCoeff = blendInfo.fSrcBlend;
    GrBlendCoeff dstCoeff = blendInfo.fDstBlend;
    bool blendOn = !GrBlendShouldDisable(equation, srcCoeff, dstCoeff);

    mtlColorAttachment.blendingEnabled = blendOn;
    if (writer) {
        writer->writeBool(blendOn);
    }
    if (blendOn) {
        mtlColorAttachment.sourceRGBBlendFactor = blend_coeff_to_mtl_blend(srcCoeff);
        mtlColorAttachment.destinationRGBBlendFactor = blend_coeff_to_mtl_blend(dstCoeff);
        mtlColorAttachment.rgbBlendOperation = blend_equation_to_mtl_blend_op(equation);
        mtlColorAttachment.sourceAlphaBlendFactor = blend_coeff_to_mtl_blend(srcCoeff);
        mtlColorAttachment.destinationAlphaBlendFactor = blend_coeff_to_mtl_blend(dstCoeff);
        mtlColorAttachment.alphaBlendOperation = blend_equation_to_mtl_blend_op(equation);
        if (writer) {
            writer->writeInt(mtlColorAttachment.sourceRGBBlendFactor);
            writer->writeInt(mtlColorAttachment.destinationRGBBlendFactor);
            writer->writeInt(mtlColorAttachment.rgbBlendOperation);
            writer->writeInt(mtlColorAttachment.sourceAlphaBlendFactor);
            writer->writeInt(mtlColorAttachment.destinationAlphaBlendFactor);
            writer->writeInt(mtlColorAttachment.alphaBlendOperation);
        }
    }

    if (blendInfo.fWriteColor) {
        mtlColorAttachment.writeMask = MTLColorWriteMaskAll;
    } else {
        mtlColorAttachment.writeMask = MTLColorWriteMaskNone;
    }
    if (writer) {
        writer->writeBool(blendInfo.fWriteColor);
    }
    return mtlColorAttachment;
}

static uint32_t buffer_size(uint32_t offset, uint32_t maxAlignment) {
    // Metal expects the buffer to be padded at the end according to the alignment
    // of the largest element in the buffer.
    uint32_t offsetDiff = offset & maxAlignment;
    if (offsetDiff != 0) {
        offsetDiff = maxAlignment - offsetDiff + 1;
    }
    return offset + offsetDiff;
}

static MTLRenderPipelineDescriptor* read_pipeline_data(SkReadBuffer* reader) {
    auto pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];

    // set up vertex descriptor
    {
        auto vertexDescriptor = [[MTLVertexDescriptor alloc] init];
        uint32_t vertexBinding = reader->readUInt();
        uint32_t instanceBinding = reader->readUInt();

        int attributeIndex = 0;

        // vertex attributes
        int vertexAttributeCount = reader->readInt();
        for (int i = 0; i < vertexAttributeCount; ++i) {
            MTLVertexAttributeDescriptor* mtlAttribute = vertexDescriptor.attributes[attributeIndex];
            mtlAttribute.format = (MTLVertexFormat) reader->readInt();
            mtlAttribute.offset = reader->readUInt();
            mtlAttribute.bufferIndex = reader->readUInt();
            ++attributeIndex;
        }
        if (vertexAttributeCount) {
            MTLVertexBufferLayoutDescriptor* vertexBufferLayout =
                    vertexDescriptor.layouts[vertexBinding];
            vertexBufferLayout.stepFunction = MTLVertexStepFunctionPerVertex;
            vertexBufferLayout.stepRate = 1;
            vertexBufferLayout.stride = reader->readUInt();
        }

        // instance attributes
        int instanceAttributeCount = reader->readInt();
        for (int i = 0; i < instanceAttributeCount; ++i) {
            MTLVertexAttributeDescriptor* mtlAttribute = vertexDescriptor.attributes[attributeIndex];
            mtlAttribute.format = (MTLVertexFormat) reader->readInt();
            mtlAttribute.offset = reader->readUInt();
            mtlAttribute.bufferIndex = reader->readUInt();
            ++attributeIndex;
        }
        if (instanceAttributeCount) {
            MTLVertexBufferLayoutDescriptor* instanceBufferLayout =
                    vertexDescriptor.layouts[instanceBinding];
            instanceBufferLayout.stepFunction = MTLVertexStepFunctionPerInstance;
            instanceBufferLayout.stepRate = 1;
            instanceBufferLayout.stride = reader->readUInt();
        }
        pipelineDescriptor.vertexDescriptor = vertexDescriptor;
    }

    // set up color attachments
    {
        auto mtlColorAttachment = [[MTLRenderPipelineColorAttachmentDescriptor alloc] init];

        mtlColorAttachment.pixelFormat = (MTLPixelFormat) reader->readInt();
        mtlColorAttachment.blendingEnabled = reader->readBool();
        if (mtlColorAttachment.blendingEnabled) {
            mtlColorAttachment.sourceRGBBlendFactor = (MTLBlendFactor) reader->readInt();
            mtlColorAttachment.destinationRGBBlendFactor = (MTLBlendFactor) reader->readInt();
            mtlColorAttachment.rgbBlendOperation = (MTLBlendOperation) reader->readInt();
            mtlColorAttachment.sourceAlphaBlendFactor = (MTLBlendFactor) reader->readInt();
            mtlColorAttachment.destinationAlphaBlendFactor = (MTLBlendFactor) reader->readInt();
            mtlColorAttachment.alphaBlendOperation = (MTLBlendOperation) reader->readInt();
        }
        if (reader->readBool()) {
            mtlColorAttachment.writeMask = MTLColorWriteMaskAll;
        } else {
            mtlColorAttachment.writeMask = MTLColorWriteMaskNone;
        }

        pipelineDescriptor.colorAttachments[0] = mtlColorAttachment;
    }

    pipelineDescriptor.stencilAttachmentPixelFormat = (MTLPixelFormat) reader->readInt();

    return pipelineDescriptor;
}

GrMtlPipelineState* GrMtlPipelineStateBuilder::finalize(
        const GrProgramDesc& desc, const GrProgramInfo& programInfo,
        const GrMtlPrecompiledLibraries* precompiledLibs) {
    TRACE_EVENT0("skia.shaders", TRACE_FUNC);

    // Geometry shaders are not supported
    SkASSERT(!this->geometryProcessor().willUseGeoShader());

    // Set up for cache if needed
    std::unique_ptr<SkBinaryWriteBuffer> writer;

    sk_sp<SkData> cached;
    auto persistentCache = fGpu->getContext()->priv().getPersistentCache();
    if (persistentCache && !precompiledLibs) {
        sk_sp<SkData> key = SkData::MakeWithoutCopy(desc.asKey(), desc.keyLength());
        cached = persistentCache->load(*key);
    }
    if (persistentCache && !cached) {
        writer.reset(new SkBinaryWriteBuffer());
    }

    // Ordering in how we set these matters. If it changes adjust read_pipeline_data, above.
    auto pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.vertexDescriptor = create_vertex_descriptor(programInfo.geomProc(),
                                                                   writer.get());

    MTLPixelFormat pixelFormat = GrBackendFormatAsMTLPixelFormat(programInfo.backendFormat());
    if (pixelFormat == MTLPixelFormatInvalid) {
        return nullptr;
    }

    pipelineDescriptor.colorAttachments[0] = create_color_attachment(pixelFormat,
                                                                     programInfo.pipeline(),
                                                                     writer.get());
    pipelineDescriptor.sampleCount = programInfo.numSamples();
    GrMtlCaps* mtlCaps = (GrMtlCaps*)this->caps();
    pipelineDescriptor.stencilAttachmentPixelFormat = mtlCaps->getStencilPixelFormat(desc);
    if (writer) {
        writer->writeInt(pipelineDescriptor.stencilAttachmentPixelFormat);
    }
    SkASSERT(pipelineDescriptor.vertexDescriptor);
    SkASSERT(pipelineDescriptor.colorAttachments[0]);

    if (precompiledLibs) {
        SkASSERT(precompiledLibs->fVertexLibrary);
        SkASSERT(precompiledLibs->fFragmentLibrary);
        pipelineDescriptor.vertexFunction =
                [precompiledLibs->fVertexLibrary newFunctionWithName: @"vertexMain"];
        pipelineDescriptor.fragmentFunction =
                [precompiledLibs->fFragmentLibrary newFunctionWithName: @"fragmentMain"];
        SkASSERT(pipelineDescriptor.vertexFunction);
        SkASSERT(pipelineDescriptor.fragmentFunction);
        if (precompiledLibs->fRTHeight) {
            this->addRTHeightUniform(SKSL_RTHEIGHT_NAME);
        }
    } else {
        id<MTLLibrary> shaderLibraries[kGrShaderTypeCount];

        fVS.extensions().appendf("#extension GL_ARB_separate_shader_objects : enable\n");
        fFS.extensions().appendf("#extension GL_ARB_separate_shader_objects : enable\n");
        fVS.extensions().appendf("#extension GL_ARB_shading_language_420pack : enable\n");
        fFS.extensions().appendf("#extension GL_ARB_shading_language_420pack : enable\n");

        this->finalizeShaders();

        SkSL::Program::Settings settings;
        settings.fFlipY = this->origin() != kTopLeft_GrSurfaceOrigin;
        settings.fSharpenTextures = fGpu->getContext()->priv().options().fSharpenMipmappedTextures;
        SkASSERT(!this->fragColorIsInOut());

        SkReadBuffer reader;
        SkFourByteTag shaderType = 0;
        if (persistentCache && cached) {
            reader.setMemory(cached->data(), cached->size());
            shaderType = GrPersistentCacheUtils::GetType(&reader);
        }

        auto errorHandler = fGpu->getContext()->priv().getShaderErrorHandler();
        SkSL::String msl[kGrShaderTypeCount];
        SkSL::Program::Inputs inputs[kGrShaderTypeCount];

        // Unpack any stored shaders from the persistent cache
        if (cached) {
            switch (shaderType) {
                case kMSL_Tag: {
                    GrPersistentCacheUtils::UnpackCachedShaders(&reader, msl, inputs,
                                                                kGrShaderTypeCount);
                    break;
                }

                case kSKSL_Tag: {
                    SkSL::String cached_sksl[kGrShaderTypeCount];
                    if (GrPersistentCacheUtils::UnpackCachedShaders(&reader, cached_sksl, inputs,
                                                                    kGrShaderTypeCount)) {
                        bool success = GrSkSLToMSL(fGpu,
                                                   cached_sksl[kVertex_GrShaderType],
                                                   SkSL::ProgramKind::kVertex,
                                                   settings,
                                                   &msl[kVertex_GrShaderType],
                                                   &inputs[kVertex_GrShaderType],
                                                   errorHandler);
                        success = success && GrSkSLToMSL(fGpu,
                                                         cached_sksl[kFragment_GrShaderType],
                                                         SkSL::ProgramKind::kFragment,
                                                         settings,
                                                         &msl[kFragment_GrShaderType],
                                                         &inputs[kFragment_GrShaderType],
                                                         errorHandler);
                        if (!success) {
                            return nullptr;
                        }
                    }
                    break;
                }

                default: {
                    break;
                }
            }
        }

        // Create any MSL shaders from pipeline data if necessary and cache
        if (msl[kVertex_GrShaderType].empty() || msl[kFragment_GrShaderType].empty()) {
            bool success = true;
            if (msl[kVertex_GrShaderType].empty()) {
                success = GrSkSLToMSL(fGpu,
                                      fVS.fCompilerString,
                                      SkSL::ProgramKind::kVertex,
                                      settings,
                                      &msl[kVertex_GrShaderType],
                                      &inputs[kVertex_GrShaderType],
                                      errorHandler);
            }
            if (success && msl[kFragment_GrShaderType].empty()) {
                success = GrSkSLToMSL(fGpu,
                                      fFS.fCompilerString,
                                      SkSL::ProgramKind::kFragment,
                                      settings,
                                      &msl[kFragment_GrShaderType],
                                      &inputs[kFragment_GrShaderType],
                                      errorHandler);
            }
            if (!success) {
                return nullptr;
            }

            if (persistentCache && !cached) {
                sk_sp<SkData> pipelineData = writer->snapshotAsData();
                if (fGpu->getContext()->priv().options().fShaderCacheStrategy ==
                        GrContextOptions::ShaderCacheStrategy::kSkSL) {
                    SkSL::String sksl[kGrShaderTypeCount];
                    sksl[kVertex_GrShaderType] = GrShaderUtils::PrettyPrint(fVS.fCompilerString);
                    sksl[kFragment_GrShaderType] = GrShaderUtils::PrettyPrint(fFS.fCompilerString);
                    this->storeShadersInCache(sksl, inputs, &settings,
                                              std::move(pipelineData), true);
                } else {
                    /*** dump pipeline data here */
                    this->storeShadersInCache(msl, inputs, nullptr,
                                              std::move(pipelineData), false);
                }
            }
        }

        // Compile MSL to libraries
        shaderLibraries[kVertex_GrShaderType] = this->compileMtlShaderLibrary(
                                                        msl[kVertex_GrShaderType],
                                                        inputs[kVertex_GrShaderType],
                                                        errorHandler);
        shaderLibraries[kFragment_GrShaderType] = this->compileMtlShaderLibrary(
                                                        msl[kFragment_GrShaderType],
                                                        inputs[kFragment_GrShaderType],
                                                        errorHandler);
        if (!shaderLibraries[kVertex_GrShaderType] || !shaderLibraries[kFragment_GrShaderType]) {
            return nullptr;
        }

        pipelineDescriptor.vertexFunction =
                [shaderLibraries[kVertex_GrShaderType] newFunctionWithName: @"vertexMain"];
        pipelineDescriptor.fragmentFunction =
                [shaderLibraries[kFragment_GrShaderType] newFunctionWithName: @"fragmentMain"];
    }

    if (pipelineDescriptor.vertexFunction == nil) {
        SkDebugf("Couldn't find vertexMain() in library\n");
        return nullptr;
    }
    if (pipelineDescriptor.fragmentFunction == nil) {
        SkDebugf("Couldn't find fragmentMain() in library\n");
        return nullptr;
    }
    SkASSERT(pipelineDescriptor.vertexFunction);
    SkASSERT(pipelineDescriptor.fragmentFunction);

    NSError* error = nil;
#if GR_METAL_SDK_VERSION >= 230
    if (@available(macOS 11.0, iOS 14.0, *)) {
        id<MTLBinaryArchive> archive = fGpu->binaryArchive();
        if (archive) {
            NSArray* archiveArray = [NSArray arrayWithObjects:archive, nil];
            pipelineDescriptor.binaryArchives = archiveArray;
            BOOL result;
            {
                TRACE_EVENT0("skia.shaders", "addRenderPipelineFunctionsWithDescriptor");
                result = [archive addRenderPipelineFunctionsWithDescriptor: pipelineDescriptor
                                                                            error: &error];
            }
            if (!result && error) {
                SkDebugf("Error storing pipeline: %s\n",
                        [[error localizedDescription] cStringUsingEncoding: NSASCIIStringEncoding]);
            }
        }
    }
#endif
    id<MTLRenderPipelineState> pipelineState;
    {
        TRACE_EVENT0("skia.shaders", "newRenderPipelineStateWithDescriptor");
#if defined(SK_BUILD_FOR_MAC)
        pipelineState = GrMtlNewRenderPipelineStateWithDescriptor(
                                                     fGpu->device(), pipelineDescriptor, &error);
#else
        pipelineState =
            [fGpu->device() newRenderPipelineStateWithDescriptor: pipelineDescriptor
                                                           error: &error];
#endif
    }
    if (error) {
        SkDebugf("Error creating pipeline: %s\n",
                 [[error localizedDescription] cStringUsingEncoding: NSASCIIStringEncoding]);
        return nullptr;
    }
    if (!pipelineState) {
        return nullptr;
    }

    uint32_t bufferSize = buffer_size(fUniformHandler.fCurrentUBOOffset,
                                      fUniformHandler.fCurrentUBOMaxAlignment);
    return new GrMtlPipelineState(fGpu,
                                  pipelineState,
                                  pipelineDescriptor.colorAttachments[0].pixelFormat,
                                  fUniformHandles,
                                  fUniformHandler.fUniforms,
                                  bufferSize,
                                  (uint32_t)fUniformHandler.numSamplers(),
                                  std::move(fGeometryProcessor),
                                  std::move(fXferProcessor),
                                  std::move(fFPImpls));
}

//////////////////////////////////////////////////////////////////////////////

bool GrMtlPipelineStateBuilder::PrecompileShaders(GrMtlGpu* gpu, const SkData& cachedData,
                                                  GrMtlPrecompiledLibraries* precompiledLibs) {
    SkASSERT(precompiledLibs);

    SkReadBuffer reader(cachedData.data(), cachedData.size());
    SkFourByteTag shaderType = GrPersistentCacheUtils::GetType(&reader);

    auto errorHandler = gpu->getContext()->priv().getShaderErrorHandler();

    SkSL::Program::Settings settings;
    settings.fSharpenTextures = gpu->getContext()->priv().options().fSharpenMipmappedTextures;
    GrPersistentCacheUtils::ShaderMetadata meta;
    meta.fSettings = &settings;

    SkSL::String shaders[kGrShaderTypeCount];
    SkSL::Program::Inputs inputs[kGrShaderTypeCount];
    if (!GrPersistentCacheUtils::UnpackCachedShaders(&reader, shaders, inputs, kGrShaderTypeCount,
                                                     &meta)) {
        return false;
    }

    // skip the size
    reader.readUInt();
    auto pipelineDescriptor = read_pipeline_data(&reader);
    if (!reader.isValid()) {
        return false;
    }

    switch (shaderType) {
        case kMSL_Tag: {
            precompiledLibs->fVertexLibrary =
                    GrCompileMtlShaderLibrary(gpu, shaders[kVertex_GrShaderType], errorHandler);
            precompiledLibs->fFragmentLibrary =
                    GrCompileMtlShaderLibrary(gpu, shaders[kFragment_GrShaderType], errorHandler);
            break;
        }

        case kSKSL_Tag: {
            SkSL::String msl[kGrShaderTypeCount];
            if (!GrSkSLToMSL(gpu,
                           shaders[kVertex_GrShaderType],
                           SkSL::ProgramKind::kVertex,
                           settings,
                           &msl[kVertex_GrShaderType],
                           &inputs[kVertex_GrShaderType],
                           errorHandler)) {
                return false;
            }
            if (!GrSkSLToMSL(gpu,
                           shaders[kFragment_GrShaderType],
                           SkSL::ProgramKind::kFragment,
                           settings,
                           &msl[kFragment_GrShaderType],
                           &inputs[kFragment_GrShaderType],
                           errorHandler)) {
                return false;
            }
            precompiledLibs->fVertexLibrary =
                    GrCompileMtlShaderLibrary(gpu, msl[kVertex_GrShaderType], errorHandler);
            precompiledLibs->fFragmentLibrary =
                    GrCompileMtlShaderLibrary(gpu, msl[kFragment_GrShaderType], errorHandler);
            break;
        }

        default: {
            return false;
        }
    }

    pipelineDescriptor.vertexFunction =
            [precompiledLibs->fVertexLibrary newFunctionWithName: @"vertexMain"];
    pipelineDescriptor.fragmentFunction =
            [precompiledLibs->fFragmentLibrary newFunctionWithName: @"fragmentMain"];

#if GR_METAL_SDK_VERSION >= 230
    if (@available(macOS 11.0, iOS 14.0, *)) {
        id<MTLBinaryArchive> archive = gpu->binaryArchive();
        if (archive) {
            NSArray* archiveArray = [NSArray arrayWithObjects:archive, nil];
            pipelineDescriptor.binaryArchives = archiveArray;
            BOOL result;
            NSError* error = nil;
            {
                TRACE_EVENT0("skia.shaders", "addRenderPipelineFunctionsWithDescriptor");
                result = [archive addRenderPipelineFunctionsWithDescriptor: pipelineDescriptor
                                                                            error: &error];
            }
            if (!result && error) {
                SkDebugf("Error storing pipeline: %s\n",
                        [[error localizedDescription] cStringUsingEncoding: NSASCIIStringEncoding]);
            }
        }
    }
#endif
    {
        TRACE_EVENT0("skia.shaders", "newRenderPipelineStateWithDescriptor");
        MTLNewRenderPipelineStateCompletionHandler completionHandler =
                 ^(id<MTLRenderPipelineState> state, NSError* error) {
                     if (error) {
                         SkDebugf("Error creating pipeline: %s\n",
                                  [[error localizedDescription]
                                           cStringUsingEncoding: NSASCIIStringEncoding]);
                     }
                 };

        // kick off asynchronous pipeline build and depend on Apple's cache to manage it
        [gpu->device() newRenderPipelineStateWithDescriptor: pipelineDescriptor
                                          completionHandler: completionHandler];
    }

    precompiledLibs->fRTHeight = inputs[kFragment_GrShaderType].fRTHeight;
    return true;
}

GR_NORETAIN_END
