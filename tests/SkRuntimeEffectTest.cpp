/*
 * Copyright 2019 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkBitmap.h"
#include "include/core/SkBlender.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/gpu/GrDirectContext.h"
#include "src/core/SkColorSpacePriv.h"
#include "src/core/SkRuntimeEffectPriv.h"
#include "src/core/SkTLazy.h"
#include "src/gpu/GrColor.h"
#include "src/gpu/GrDirectContextPriv.h"
#include "src/gpu/GrFragmentProcessor.h"
#include "src/gpu/effects/GrSkSLFP.h"
#include "tests/Test.h"

#include <algorithm>
#include <thread>

void test_invalid_effect(skiatest::Reporter* r, const char* src, const char* expected) {
    auto [effect, errorText] = SkRuntimeEffect::MakeForShader(SkString(src));
    REPORTER_ASSERT(r, !effect);
    REPORTER_ASSERT(r, errorText.contains(expected),
                    "Expected error message to contain \"%s\". Actual message: \"%s\"",
                    expected, errorText.c_str());
};

#define EMPTY_MAIN "half4 main(float2 p) { return half4(0); }"

DEF_TEST(SkRuntimeEffectInvalid_FPOnly, r) {
    // Features that are only allowed in .fp files (key, in uniform, ctype, when).
    // Ensure that these fail, and the error messages contain the relevant keyword.
    test_invalid_effect(r, "layout(key) in bool Input;"             EMPTY_MAIN, "key");
    test_invalid_effect(r, "in uniform float Input;"                EMPTY_MAIN, "in uniform");
    test_invalid_effect(r, "layout(ctype=SkRect) float4 Input;"     EMPTY_MAIN, "ctype");
    test_invalid_effect(r, "in bool Flag; "
                           "layout(when=Flag) uniform float Input;" EMPTY_MAIN, "when");
}

DEF_TEST(SkRuntimeEffectInvalid_LimitedUniformTypes, r) {
    // Runtime SkSL supports a limited set of uniform types. No bool, for example:
    test_invalid_effect(r, "uniform bool b;" EMPTY_MAIN, "uniform");
}

DEF_TEST(SkRuntimeEffectInvalid_NoInVariables, r) {
    // 'in' variables aren't allowed at all:
    test_invalid_effect(r, "in bool b;"    EMPTY_MAIN, "'in'");
    test_invalid_effect(r, "in float f;"   EMPTY_MAIN, "'in'");
    test_invalid_effect(r, "in float2 v;"  EMPTY_MAIN, "'in'");
    test_invalid_effect(r, "in half3x3 m;" EMPTY_MAIN, "'in'");
}

DEF_TEST(SkRuntimeEffectInvalid_UndefinedFunction, r) {
    test_invalid_effect(r, "half4 missing(); half4 main(float2 p) { return missing(); }",
                           "undefined function");
}

DEF_TEST(SkRuntimeEffectInvalid_UndefinedMain, r) {
    // Shouldn't be possible to create an SkRuntimeEffect without "main"
    test_invalid_effect(r, "", "main");
}

DEF_TEST(SkRuntimeEffectInvalid_SkCapsDisallowed, r) {
    // sk_Caps is an internal system. It should not be visible to runtime effects
    test_invalid_effect(
            r,
            "half4 main(float2 p) { return sk_Caps.integerSupport ? half4(1) : half4(0); }",
            "unknown identifier 'sk_Caps'");
}

DEF_TEST(SkRuntimeEffectCanDisableES2Restrictions, r) {
    auto test_valid_es3 = [](skiatest::Reporter* r, const char* sksl) {
        SkRuntimeEffect::Options opt = SkRuntimeEffectPriv::ES3Options();
        auto [effect, errorText] = SkRuntimeEffect::MakeForShader(SkString(sksl), opt);
        REPORTER_ASSERT(r, effect, "%s", errorText.c_str());
    };

    test_invalid_effect(r, "float f[2] = float[2](0, 1);" EMPTY_MAIN, "construction of array type");
    test_valid_es3     (r, "float f[2] = float[2](0, 1);" EMPTY_MAIN);
}

DEF_TEST(SkRuntimeEffectForColorFilter, r) {
    // Tests that the color filter factory rejects or accepts certain SkSL constructs
    auto test_valid = [r](const char* sksl) {
        auto [effect, errorText] = SkRuntimeEffect::MakeForColorFilter(SkString(sksl));
        REPORTER_ASSERT(r, effect, "%s", errorText.c_str());
    };

    auto test_invalid = [r](const char* sksl, const char* expected) {
        auto [effect, errorText] = SkRuntimeEffect::MakeForColorFilter(SkString(sksl));
        REPORTER_ASSERT(r, !effect);
        REPORTER_ASSERT(r,
                        errorText.contains(expected),
                        "Expected error message to contain \"%s\". Actual message: \"%s\"",
                        expected,
                        errorText.c_str());
    };

    // Color filters must use the 'half4 main(half4)' signature. Either color can be float4/vec4
    test_valid("half4  main(half4  c) { return c; }");
    test_valid("float4 main(half4  c) { return c; }");
    test_valid("half4  main(float4 c) { return c; }");
    test_valid("float4 main(float4 c) { return c; }");
    test_valid("vec4   main(half4  c) { return c; }");
    test_valid("half4  main(vec4   c) { return c; }");
    test_valid("vec4   main(vec4   c) { return c; }");

    // Invalid return types
    test_invalid("void  main(half4 c) {}",                "'main' must return");
    test_invalid("half3 main(half4 c) { return c.rgb; }", "'main' must return");

    // Invalid argument types (some are valid as shaders, but not color filters)
    test_invalid("half4 main() { return half4(1); }",           "'main' parameter");
    test_invalid("half4 main(float2 p) { return half4(1); }",   "'main' parameter");
    test_invalid("half4 main(float2 p, half4 c) { return c; }", "'main' parameter");

    // sk_FragCoord should not be available
    test_invalid("half4 main(half4 c) { return sk_FragCoord.xy01; }", "unknown identifier");

    // Sampling a child shader requires that we pass explicit coords
    test_valid("uniform shader child;"
               "half4 main(half4 c) { return sample(child, c.rg); }");
    // Trying to pass a color as well. (Works internally with FPs, but not in runtime effects).
    test_invalid("uniform shader child;"
                 "half4 main(half4 c) { return sample(child, c.rg, c); }",
                 "no match for sample(shader, half2, half4)");

    // Shader with just a color
    test_invalid("uniform shader child;"
                 "half4 main(half4 c) { return sample(child, c); }",
                 "no match for sample(shader, half4)");
    // Coords and color in a different order
    test_invalid("uniform shader child;"
                 "half4 main(half4 c) { return sample(child, c, c.rg); }",
                 "no match for sample(shader, half4, half2)");

    // Older variants that are no longer allowed
    test_invalid(
            "uniform shader child;"
            "half4 main(half4 c) { return sample(child); }",
            "no match for sample(shader)");
    test_invalid(
            "uniform shader child;"
            "half4 main(half4 c) { return sample(child, float3x3(1)); }",
            "no match for sample(shader, float3x3)");

    // Sampling a colorFilter requires a color. No other signatures are valid.
    test_valid("uniform colorFilter child;"
               "half4 main(half4 c) { return sample(child, c); }");

    test_invalid("uniform colorFilter child;"
                 "half4 main(half4 c) { return sample(child); }",
                 "sample(colorFilter)");
    test_invalid("uniform colorFilter child;"
                 "half4 main(half4 c) { return sample(child, c.rg); }",
                 "sample(colorFilter, half2)");
    test_invalid("uniform colorFilter child;"
                 "half4 main(half4 c) { return sample(child, c.rg, c); }",
                 "sample(colorFilter, half2, half4)");
}

DEF_TEST(SkRuntimeEffectForBlender, r) {
    // Tests that the blender factory rejects or accepts certain SkSL constructs
    auto test_valid = [r](const char* sksl) {
        auto [effect, errorText] = SkRuntimeEffect::MakeForBlender(SkString(sksl));
        REPORTER_ASSERT(r, effect, "%s", errorText.c_str());
    };

    auto test_invalid = [r](const char* sksl, const char* expected) {
        auto [effect, errorText] = SkRuntimeEffect::MakeForBlender(SkString(sksl));
        REPORTER_ASSERT(r, !effect);
        REPORTER_ASSERT(r,
                        errorText.contains(expected),
                        "Expected error message to contain \"%s\". Actual message: \"%s\"",
                        expected,
                        errorText.c_str());
    };

    // Color filters must use the 'half4 main(half4, half4)' signature. Any mixture of
    // float4/vec4/half4 is allowed.
    test_valid("half4  main(half4  s, half4  d) { return s; }");
    test_valid("float4 main(float4 s, float4 d) { return d; }");
    test_valid("float4 main(half4  s, float4 d) { return s; }");
    test_valid("half4  main(float4 s, half4  d) { return d; }");
    test_valid("vec4   main(half4  s, half4  d) { return s; }");
    test_valid("half4  main(vec4   s, vec4   d) { return d; }");
    test_valid("vec4   main(vec4   s, vec4   d) { return s; }");

    // Invalid return types
    test_invalid("void  main(half4 s, half4 d) {}",                "'main' must return");
    test_invalid("half3 main(half4 s, half4 d) { return s.rgb; }", "'main' must return");

    // Invalid argument types (some are valid as shaders/color filters)
    test_invalid("half4 main() { return half4(1); }",                    "'main' parameter");
    test_invalid("half4 main(half4 c) { return c; }",                    "'main' parameter");
    test_invalid("half4 main(float2 p) { return half4(1); }",            "'main' parameter");
    test_invalid("half4 main(float2 p, half4 c) { return c; }",          "'main' parameter");
    test_invalid("half4 main(float2 p, half4 a, half4 b) { return a; }", "'main' parameter");
    test_invalid("half4 main(half4 a, half4 b, half4 c) { return a; }",  "'main' parameter");

    // sk_FragCoord should not be available
    test_invalid("half4 main(half4 s, half4 d) { return sk_FragCoord.xy01; }",
                 "unknown identifier");

    // Child shaders are currently unsupported in blends
    test_invalid("uniform shader sh; half4 main(half4 s, half4 d) { return s; }",
                 "'shader' is not allowed in runtime blend");
    test_invalid("uniform shader sh; half4 main(half4 s, half4 d) { return sample(sh, s.rg); }",
                 "unknown identifier 'sample'");
}

DEF_TEST(SkRuntimeEffectForShader, r) {
    // Tests that the shader factory rejects or accepts certain SkSL constructs
    auto test_valid = [r](const char* sksl) {
        auto [effect, errorText] = SkRuntimeEffect::MakeForShader(SkString(sksl));
        REPORTER_ASSERT(r, effect, "%s", errorText.c_str());
    };

    auto test_invalid = [r](const char* sksl, const char* expected) {
        auto [effect, errorText] = SkRuntimeEffect::MakeForShader(SkString(sksl));
        REPORTER_ASSERT(r, !effect);
        REPORTER_ASSERT(r,
                        errorText.contains(expected),
                        "Expected error message to contain \"%s\". Actual message: \"%s\"",
                        expected,
                        errorText.c_str());
    };

    // Shaders must use either the 'half4 main(float2)' or 'half4 main(float2, half4)' signature
    // Either color can be half4/float4/vec4, but the coords must be float2/vec2
    test_valid("half4  main(float2 p) { return p.xyxy; }");
    test_valid("float4 main(float2 p) { return p.xyxy; }");
    test_valid("vec4   main(float2 p) { return p.xyxy; }");
    test_valid("half4  main(vec2   p) { return p.xyxy; }");
    test_valid("vec4   main(vec2   p) { return p.xyxy; }");
    test_valid("half4  main(float2 p, half4  c) { return c; }");
    test_valid("half4  main(float2 p, float4 c) { return c; }");
    test_valid("half4  main(float2 p, vec4   c) { return c; }");
    test_valid("float4 main(float2 p, half4  c) { return c; }");
    test_valid("vec4   main(float2 p, half4  c) { return c; }");
    test_valid("vec4   main(vec2   p, vec4   c) { return c; }");

    // Invalid return types
    test_invalid("void  main(float2 p) {}",                "'main' must return");
    test_invalid("half3 main(float2 p) { return p.xy1; }", "'main' must return");

    // Invalid argument types (some are valid as color filters, but not shaders)
    test_invalid("half4 main() { return half4(1); }", "'main' parameter");
    test_invalid("half4 main(half4 c) { return c; }", "'main' parameter");

    // sk_FragCoord should be available
    test_valid("half4 main(float2 p) { return sk_FragCoord.xy01; }");

    // Sampling a child shader requires that we pass explicit coords
    test_valid("uniform shader child;"
               "half4 main(float2 p) { return sample(child, p); }");

    // Trying to pass a color as well. (Works internally with FPs, but not in runtime effects).
    test_invalid("uniform shader child;"
                 "half4 main(float2 p, half4 c) { return sample(child, p, c); }",
                 "no match for sample(shader, float2, half4)");

    // Shader with just a color
    test_invalid("uniform shader child;"
                 "half4 main(float2 p, half4 c) { return sample(child, c); }",
                 "no match for sample(shader, half4)");
    // Coords and color in a different order
    test_invalid("uniform shader child;"
                 "half4 main(float2 p, half4 c) { return sample(child, c, p); }",
                 "no match for sample(shader, half4, float2)");

    // Older variants that are no longer allowed
    test_invalid(
            "uniform shader child;"
            "half4 main(float2 p) { return sample(child); }",
            "no match for sample(shader)");
    test_invalid(
            "uniform shader child;"
            "half4 main(float2 p) { return sample(child, float3x3(1)); }",
            "no match for sample(shader, float3x3)");

    // Sampling a colorFilter requires a color. No other signatures are valid.
    test_valid("uniform colorFilter child;"
               "half4 main(float2 p, half4 c) { return sample(child, c); }");

    test_invalid("uniform colorFilter child;"
                 "half4 main(float2 p) { return sample(child); }",
                 "sample(colorFilter)");
    test_invalid("uniform colorFilter child;"
                 "half4 main(float2 p) { return sample(child, p); }",
                 "sample(colorFilter, float2)");
    test_invalid("uniform colorFilter child;"
                 "half4 main(float2 p, half4 c) { return sample(child, p, c); }",
                 "sample(colorFilter, float2, half4)");
}

using PreTestFn = std::function<void(SkCanvas*, SkPaint*)>;

void paint_canvas(SkCanvas* canvas, SkPaint* paint, const PreTestFn& preTestCallback) {
    canvas->save();
    if (preTestCallback) {
        preTestCallback(canvas, paint);
    }
    canvas->drawPaint(*paint);
    canvas->restore();
}

static void verify_2x2_surface_results(skiatest::Reporter* r,
                                       const SkRuntimeEffect* effect,
                                       SkSurface* surface,
                                       std::array<GrColor, 4> expected) {
    std::array<GrColor, 4> actual;
    SkImageInfo info = surface->imageInfo();
    if (!surface->readPixels(info, actual.data(), info.minRowBytes(), /*srcX=*/0, /*srcY=*/0)) {
        REPORT_FAILURE(r, "readPixels", SkString("readPixels failed"));
        return;
    }

    if (actual != expected) {
        REPORT_FAILURE(r, "Runtime effect didn't match expectations",
                       SkStringPrintf("\n"
                                      "Expected: [ %08x %08x %08x %08x ]\n"
                                      "Got     : [ %08x %08x %08x %08x ]\n"
                                      "SkSL:\n%s\n",
                                      expected[0], expected[1], expected[2], expected[3],
                                      actual[0],   actual[1],   actual[2],   actual[3],
                                      effect->source().c_str()));
    }
}

class TestEffect {
public:
    TestEffect(skiatest::Reporter* r, sk_sp<SkSurface> surface)
            : fReporter(r), fSurface(std::move(surface)) {}

    void build(const char* src) {
        auto [effect, errorText] = SkRuntimeEffect::MakeForShader(SkString(src));
        if (!effect) {
            REPORT_FAILURE(fReporter, "effect",
                           SkStringPrintf("Effect didn't compile: %s", errorText.c_str()));
            return;
        }
        fBuilder.init(std::move(effect));
    }

    SkRuntimeShaderBuilder::BuilderUniform uniform(const char* name) {
        return fBuilder->uniform(name);
    }

    SkRuntimeShaderBuilder::BuilderChild child(const char* name) {
        return fBuilder->child(name);
    }

    void test(std::array<GrColor, 4> expected, PreTestFn preTestCallback = nullptr) {
        auto shader = fBuilder->makeShader(/*localMatrix=*/nullptr, /*isOpaque=*/false);
        if (!shader) {
            REPORT_FAILURE(fReporter, "shader", SkString("Effect didn't produce a shader"));
            return;
        }

        SkCanvas* canvas = fSurface->getCanvas();
        SkPaint paint;
        paint.setShader(std::move(shader));
        paint.setBlendMode(SkBlendMode::kSrc);

        paint_canvas(canvas, &paint, preTestCallback);

        verify_2x2_surface_results(fReporter, fBuilder->effect(), fSurface.get(), expected);
    }

    void test(GrColor expected, PreTestFn preTestCallback = nullptr) {
        this->test({expected, expected, expected, expected}, preTestCallback);
    }

private:
    skiatest::Reporter*             fReporter;
    sk_sp<SkSurface>                fSurface;
    SkTLazy<SkRuntimeShaderBuilder> fBuilder;
};

class TestBlend {
public:
    TestBlend(skiatest::Reporter* r, sk_sp<SkSurface> surface)
            : fReporter(r), fSurface(std::move(surface)) {}

    void build(const char* src) {
        auto [effect, errorText] = SkRuntimeEffect::MakeForBlender(SkString(src));
        if (!effect) {
            REPORT_FAILURE(fReporter, "effect",
                           SkStringPrintf("Effect didn't compile: %s", errorText.c_str()));
            return;
        }
        fBuilder.init(std::move(effect));
    }

    SkRuntimeBlendBuilder::BuilderUniform uniform(const char* name) {
        return fBuilder->uniform(name);
    }

    void test(std::array<GrColor, 4> expected, PreTestFn preTestCallback = nullptr) {
        auto blender = fBuilder->makeBlender();
        if (!blender) {
            REPORT_FAILURE(fReporter, "blender", SkString("Effect didn't produce a blender"));
            return;
        }

        SkCanvas* canvas = fSurface->getCanvas();
        SkPaint paint;
        paint.experimental_setBlender(std::move(blender));
        paint.setColor(SK_ColorGRAY);

        paint_canvas(canvas, &paint, preTestCallback);

        verify_2x2_surface_results(fReporter, fBuilder->effect(), fSurface.get(), expected);
    }

    void test(GrColor expected, PreTestFn preTestCallback = nullptr) {
        this->test({expected, expected, expected, expected}, preTestCallback);
    }

private:
    skiatest::Reporter*            fReporter;
    sk_sp<SkSurface>               fSurface;
    SkTLazy<SkRuntimeBlendBuilder> fBuilder;
};

// Produces a 2x2 bitmap shader, with opaque colors:
// [  Red, Green ]
// [ Blue, White ]
static sk_sp<SkShader> make_RGBW_shader() {
    SkBitmap bmp;
    bmp.allocPixels(SkImageInfo::Make(2, 2, kRGBA_8888_SkColorType, kPremul_SkAlphaType));
    SkIRect topLeft = SkIRect::MakeWH(1, 1);
    bmp.pixmap().erase(SK_ColorRED,   topLeft);
    bmp.pixmap().erase(SK_ColorGREEN, topLeft.makeOffset(1, 0));
    bmp.pixmap().erase(SK_ColorBLUE,  topLeft.makeOffset(0, 1));
    bmp.pixmap().erase(SK_ColorWHITE, topLeft.makeOffset(1, 1));
    return bmp.makeShader(SkSamplingOptions());
}

static void test_RuntimeEffect_Shaders(skiatest::Reporter* r, GrRecordingContext* rContext) {
    SkImageInfo info = SkImageInfo::Make(2, 2, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = rContext
                                    ? SkSurface::MakeRenderTarget(rContext, SkBudgeted::kNo, info)
                                    : SkSurface::MakeRaster(info);
    REPORTER_ASSERT(r, surface);
    TestEffect effect(r, surface);

    using float4 = std::array<float, 4>;
    using int4 = std::array<int, 4>;

    // Local coords
    effect.build("half4 main(float2 p) { return half4(half2(p - 0.5), 0, 1); }");
    effect.test({0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF});

    // Use of a simple uniform. (Draw twice with two values to ensure it's updated).
    effect.build("uniform float4 gColor; half4 main(float2 p) { return half4(gColor); }");
    effect.uniform("gColor") = float4{ 0.0f, 0.25f, 0.75f, 1.0f };
    effect.test(0xFFBF4000);
    effect.uniform("gColor") = float4{ 1.0f, 0.0f, 0.0f, 0.498f };
    effect.test(0x7F00007F);  // Tests that we clamp to valid premul

    // Same, with integer uniforms
    effect.build("uniform int4 gColor; half4 main(float2 p) { return half4(gColor) / 255.0; }");
    effect.uniform("gColor") = int4{ 0x00, 0x40, 0xBF, 0xFF };
    effect.test(0xFFBF4000);
    effect.uniform("gColor") = int4{ 0xFF, 0x00, 0x00, 0x7F };
    effect.test(0x7F00007F);  // Tests that we clamp to valid premul

    // Test sk_FragCoord (device coords). Rotate the canvas to be sure we're seeing device coords.
    // Since the surface is 2x2, we should see (0,0), (1,0), (0,1), (1,1). Multiply by 0.498 to
    // make sure we're not saturating unexpectedly.
    effect.build(
            "half4 main(float2 p) { return half4(0.498 * (half2(sk_FragCoord.xy) - 0.5), 0, 1); }");
    effect.test({0xFF000000, 0xFF00007F, 0xFF007F00, 0xFF007F7F},
                [](SkCanvas* canvas, SkPaint*) { canvas->rotate(45.0f); });

    // Runtime effects should use relaxed precision rules by default
    effect.build("half4 main(float2 p) { return float4(p - 0.5, 0, 1); }");
    effect.test({0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF});

    // ... and support *returning* float4 (aka vec4), not just half4
    effect.build("float4 main(float2 p) { return float4(p - 0.5, 0, 1); }");
    effect.test({0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF});
    effect.build("vec4 main(float2 p) { return float4(p - 0.5, 0, 1); }");
    effect.test({0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF});

    // Mutating coords should work. (skbug.com/10918)
    effect.build("vec4 main(vec2 p) { p -= 0.5; return vec4(p, 0, 1); }");
    effect.test({0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF});
    effect.build("void moveCoords(inout vec2 p) { p -= 0.5; }"
                 "vec4 main(vec2 p) { moveCoords(p); return vec4(p, 0, 1); }");
    effect.test({0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF});

    //
    // Sampling children
    //

    // Sampling a null child should return the paint color
    effect.build("uniform shader child;"
                 "half4 main(float2 p) { return sample(child, p); }");
    effect.child("child") = nullptr;
    effect.test(0xFF00FFFF,
                [](SkCanvas*, SkPaint* paint) { paint->setColor4f({1.0f, 1.0f, 0.0f, 1.0f}); });

    sk_sp<SkShader> rgbwShader = make_RGBW_shader();

    // Sampling a simple child at our coordinates
    effect.build("uniform shader child;"
                 "half4 main(float2 p) { return sample(child, p); }");
    effect.child("child") = rgbwShader;
    effect.test({0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFFFFFFFF});

    // Sampling with explicit coordinates (reflecting about the diagonal)
    effect.build("uniform shader child;"
                 "half4 main(float2 p) { return sample(child, p.yx); }");
    effect.child("child") = rgbwShader;
    effect.test({0xFF0000FF, 0xFFFF0000, 0xFF00FF00, 0xFFFFFFFF});

    //
    // Helper functions
    //

    // Test case for inlining in the pipeline-stage and fragment-shader passes (skbug.com/10526):
    effect.build("float2 helper(float2 x) { return x + 1; }"
                 "half4 main(float2 p) { float2 v = helper(p); return half4(half2(v), 0, 1); }");
    effect.test(0xFF00FFFF);
}

DEF_TEST(SkRuntimeEffectSimple, r) {
    test_RuntimeEffect_Shaders(r, nullptr);
}

DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SkRuntimeEffectSimple_GPU, r, ctxInfo) {
    test_RuntimeEffect_Shaders(r, ctxInfo.directContext());
}

static void test_RuntimeEffect_Blenders(skiatest::Reporter* r, GrRecordingContext* rContext) {
    SkImageInfo info = SkImageInfo::Make(2, 2, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = rContext
                                    ? SkSurface::MakeRenderTarget(rContext, SkBudgeted::kNo, info)
                                    : SkSurface::MakeRaster(info);
    REPORTER_ASSERT(r, surface);
    TestBlend effect(r, surface);

    using float4 = std::array<float, 4>;
    using int4 = std::array<int, 4>;

    // Use of a simple uniform. (Draw twice with two values to ensure it's updated).
    effect.build("uniform float4 gColor; half4 main(half4 s, half4 d) { return half4(gColor); }");
    effect.uniform("gColor") = float4{ 0.0f, 0.25f, 0.75f, 1.0f };
    effect.test(0xFFBF4000);
    effect.uniform("gColor") = float4{ 1.0f, 0.0f, 0.0f, 0.498f };
    effect.test(0x7F0000FF);  // Unlike SkShaders, we don't clamp here

    // Same, with integer uniforms
    effect.build("uniform int4 gColor;"
                 "half4 main(half4 s, half4 d) { return half4(gColor) / 255.0; }");
    effect.uniform("gColor") = int4{ 0x00, 0x40, 0xBF, 0xFF };
    effect.test(0xFFBF4000);
    effect.uniform("gColor") = int4{ 0xFF, 0x00, 0x00, 0x7F };
    effect.test(0x7F0000FF);  // Unlike SkShaders, we don't clamp here

    // Verify that mutating the source and destination colors is allowed
    effect.build("half4 main(half4 s, half4 d) { s += d; d += s; return half4(1); }");
    effect.test(0xFFFFFFFF);

    // Verify that we can write out the source color (ignoring the dest color)
    // This is equivalent to the kSrc blend mode.
    effect.build("half4 main(half4 s, half4 d) { return s; }");
    effect.test(0xFF888888);

    // Fill the destination with a variety of colors (using the RGBW shader)
    SkPaint paint;
    paint.setShader(make_RGBW_shader());
    paint.setBlendMode(SkBlendMode::kSrc);
    surface->getCanvas()->drawPaint(paint);

    // Verify that we can read back the dest color exactly as-is (ignoring the source color)
    // This is equivalent to the kDst blend mode.
    effect.build("half4 main(half4 s, half4 d) { return d; }");
    effect.test({0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFFFFFFFF});

    // Verify that we can invert the destination color (including the alpha channel).
    // The expected outputs are the exact inverse of the previous test.
    effect.build("half4 main(half4 s, half4 d) { return half4(1) - d; }");
    effect.test({0x00FFFF00, 0x00FF00FF, 0x0000FFFF, 0x00000000});

    // Verify that color values are clamped to 0 and 1.
    effect.build("half4 main(half4 s, half4 d) { return half4(-1); }");
    effect.test(0x00000000);
    effect.build("half4 main(half4 s, half4 d) { return half4(2); }");
    effect.test(0xFFFFFFFF);
}

DEF_TEST(SkRuntimeEffect_Blender_CPU, r) {
    test_RuntimeEffect_Blenders(r, /*rContext=*/nullptr);
}

DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SkRuntimeEffect_Blender_GPU, r, ctxInfo) {
    test_RuntimeEffect_Blenders(r, ctxInfo.directContext());
}

DEF_TEST(SkRuntimeShaderBuilderReuse, r) {
    const char* kSource = R"(
        uniform half x;
        half4 main(float2 p) { return half4(x); }
    )";

    sk_sp<SkRuntimeEffect> effect = SkRuntimeEffect::MakeForShader(SkString(kSource)).effect;
    REPORTER_ASSERT(r, effect);

    // Test passes if this sequence doesn't assert.  skbug.com/10667
    SkRuntimeShaderBuilder b(std::move(effect));
    b.uniform("x") = 0.0f;
    auto shader_0 = b.makeShader(/*localMatrix=*/nullptr, /*isOpaque=*/false);

    b.uniform("x") = 1.0f;
    auto shader_1 = b.makeShader(/*localMatrix=*/nullptr, /*isOpaque=*/true);
}

DEF_TEST(SkRuntimeBlendBuilderReuse, r) {
    const char* kSource = R"(
        uniform half x;
        half4 main(half4 s, half4 d) { return half4(x); }
    )";

    sk_sp<SkRuntimeEffect> effect = SkRuntimeEffect::MakeForBlender(SkString(kSource)).effect;
    REPORTER_ASSERT(r, effect);

    // We should be able to construct multiple SkBlenders in a row without asserting.
    SkRuntimeBlendBuilder b(std::move(effect));
    for (float x = 0.0f; x <= 2.0f; x += 2.0f) {
        b.uniform("x") = x;
        sk_sp<SkBlender> blender = b.makeBlender();
    }
}

DEF_TEST(SkRuntimeShaderBuilderSetUniforms, r) {
    const char* kSource = R"(
        uniform half x;
        uniform vec2 offset;
        half4 main(float2 p) { return half4(x); }
    )";

    sk_sp<SkRuntimeEffect> effect = SkRuntimeEffect::MakeForShader(SkString(kSource)).effect;
    REPORTER_ASSERT(r, effect);

    SkRuntimeShaderBuilder b(std::move(effect));

    // Test passes if this sequence doesn't assert.
    float x = 1.0f;
    REPORTER_ASSERT(r, b.uniform("x").set(&x, 1));

    // add extra value to ensure that set doesn't try to use sizeof(array)
    float origin[] = { 2.0f, 3.0f, 4.0f };
    REPORTER_ASSERT(r, b.uniform("offset").set<float>(origin, 2));

#ifndef SK_DEBUG
    REPORTER_ASSERT(r, !b.uniform("offset").set<float>(origin, 1));
    REPORTER_ASSERT(r, !b.uniform("offset").set<float>(origin, 3));
#endif

    auto shader = b.makeShader(/*localMatrix=*/nullptr, /*isOpaque=*/false);
}

DEF_TEST(SkRuntimeEffectThreaded, r) {
    // SkRuntimeEffect uses a single compiler instance, but it's mutex locked.
    // This tests that we can safely use it from more than one thread, and also
    // that programs don't refer to shared structures owned by the compiler.
    // skbug.com/10589
    static constexpr char kSource[] = "half4 main(float2 p) { return sk_FragCoord.xyxy; }";

    std::thread threads[16];
    for (auto& thread : threads) {
        thread = std::thread([r]() {
            auto [effect, error] = SkRuntimeEffect::MakeForShader(SkString(kSource));
            REPORTER_ASSERT(r, effect);
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

DEF_TEST(SkRuntimeColorFilterSingleColor, r) {
    // Test runtime colorfilters support filterColor4f().
    auto [effect, err] =
            SkRuntimeEffect::MakeForColorFilter(SkString{"half4 main(half4 c) { return c*c; }"});
    REPORTER_ASSERT(r, effect);
    REPORTER_ASSERT(r, err.isEmpty());

    sk_sp<SkColorFilter> cf = effect->makeColorFilter(SkData::MakeEmpty());
    REPORTER_ASSERT(r, cf);

    SkColor4f c = cf->filterColor4f({0.25, 0.5, 0.75, 1.0},
                                    sk_srgb_singleton(), sk_srgb_singleton());
    REPORTER_ASSERT(r, c.fR == 0.0625f);
    REPORTER_ASSERT(r, c.fG == 0.25f);
    REPORTER_ASSERT(r, c.fB == 0.5625f);
    REPORTER_ASSERT(r, c.fA == 1.0f);
}

static void test_RuntimeEffectStructNameReuse(skiatest::Reporter* r, GrRecordingContext* rContext) {
    // Test that two different runtime effects can reuse struct names in a single paint operation
    auto [childEffect, err] = SkRuntimeEffect::MakeForShader(SkString(
        "uniform shader paint;"
        "struct S { half4 rgba; };"
        "void process(inout S s) { s.rgba.rgb *= 0.5; }"
        "half4 main(float2 p) { S s; s.rgba = sample(paint, p); process(s); return s.rgba; }"
    ));
    REPORTER_ASSERT(r, childEffect, "%s\n", err.c_str());
    sk_sp<SkShader> nullChild = nullptr;
    sk_sp<SkShader> child = childEffect->makeShader(/*uniforms=*/nullptr, &nullChild,
                                                    /*childCount=*/1, /*localMatrix=*/nullptr,
                                                    /*isOpaque=*/false);

    SkImageInfo info = SkImageInfo::Make(2, 2, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = rContext
                                    ? SkSurface::MakeRenderTarget(rContext, SkBudgeted::kNo, info)
                                    : SkSurface::MakeRaster(info);
    REPORTER_ASSERT(r, surface);

    TestEffect effect(r, surface);
    effect.build(
            "uniform shader child;"
            "struct S { float2 coord; };"
            "void process(inout S s) { s.coord = s.coord.yx; }"
            "half4 main(float2 p) { S s; s.coord = p; process(s); return sample(child, s.coord); "
            "}");
    effect.child("child") = child;
    effect.test(0xFF00407F, [](SkCanvas*, SkPaint* paint) {
        paint->setColor4f({0.99608f, 0.50196f, 0.0f, 1.0f});
    });
}

DEF_TEST(SkRuntimeStructNameReuse, r) {
    test_RuntimeEffectStructNameReuse(r, nullptr);
}

DEF_GPUTEST_FOR_RENDERING_CONTEXTS(SkRuntimeStructNameReuse_GPU, r, ctxInfo) {
    test_RuntimeEffectStructNameReuse(r, ctxInfo.directContext());
}

DEF_TEST(SkRuntimeColorFilterFlags, r) {
    {   // Here's a non-trivial filter that doesn't change alpha.
        auto [effect, err] = SkRuntimeEffect::MakeForColorFilter(SkString{
                "half4 main(half4 color) { return color + half4(1,1,1,0); }"});
        REPORTER_ASSERT(r, effect && err.isEmpty());
        sk_sp<SkColorFilter> filter = effect->makeColorFilter(SkData::MakeEmpty());
        REPORTER_ASSERT(r, filter && filter->isAlphaUnchanged());
    }

    {  // Here's one that definitely changes alpha.
        auto [effect, err] = SkRuntimeEffect::MakeForColorFilter(SkString{
                "half4 main(half4 color) { return color + half4(0,0,0,4); }"});
        REPORTER_ASSERT(r, effect && err.isEmpty());
        sk_sp<SkColorFilter> filter = effect->makeColorFilter(SkData::MakeEmpty());
        REPORTER_ASSERT(r, filter && !filter->isAlphaUnchanged());
    }
}

DEF_TEST(SkRuntimeShaderSampleCoords, r) {
    // This test verifies that we detect calls to sample where the coords are the same as those
    // passed to main. In those cases, it's safe to turn the "explicit" sampling into "passthrough"
    // sampling. This optimization is implemented very conservatively.
    //
    // It also checks that we correctly set the "referencesSampleCoords" bit on the runtime effect
    // FP, depending on how the coords parameter to main is used.

    auto test = [&](const char* src, bool expectExplicit, bool expectReferencesSampleCoords) {
        auto [effect, err] =
                SkRuntimeEffect::MakeForShader(SkStringPrintf("uniform shader child; %s", src));
        REPORTER_ASSERT(r, effect);

        auto child = GrFragmentProcessor::MakeColor({ 1, 1, 1, 1 });
        auto fp = GrSkSLFP::Make(effect, "test_fp", /*inputFP=*/nullptr, GrSkSLFP::OptFlags::kNone,
                                 "child", std::move(child));
        REPORTER_ASSERT(r, fp);

        REPORTER_ASSERT(r, fp->childProcessor(0)->isSampledWithExplicitCoords() == expectExplicit);
        REPORTER_ASSERT(r, fp->referencesSampleCoords() == expectReferencesSampleCoords);
    };

    // Cases where our optimization is valid, and works:

    // Direct use of passed-in coords. Here, the only use of sample coords is for a sample call
    // converted to passthrough, so referenceSampleCoords is *false*, despite appearing in main.
    test("half4 main(float2 xy) { return sample(child, xy); }", false, false);
    // Sample with passed-in coords, read (but don't write) sample coords elsewhere
    test("half4 main(float2 xy) { return sample(child, xy) + sin(xy.x); }", false, true);

    // Cases where our optimization is not valid, and does not happen:

    // Sampling with values completely unrelated to passed-in coords
    test("half4 main(float2 xy) { return sample(child, float2(0, 0)); }", true, false);
    // Use of expression involving passed in coords
    test("half4 main(float2 xy) { return sample(child, xy * 0.5); }", true, true);
    // Use of coords after modification
    test("half4 main(float2 xy) { xy *= 2; return sample(child, xy); }", true, true);
    // Use of coords after modification via out-param call
    test("void adjust(inout float2 xy) { xy *= 2; }"
         "half4 main(float2 xy) { adjust(xy); return sample(child, xy); }", true, true);

    // There should (must) not be any false-positive cases. There are false-negatives.
    // In all of these cases, our optimization would be valid, but does not happen:

    // Direct use of passed-in coords, modified after use
    test("half4 main(float2 xy) { half4 c = sample(child, xy); xy *= 2; return c; }", true, true);
    // Passed-in coords copied to a temp variable
    test("half4 main(float2 xy) { float2 p = xy; return sample(child, p); }", true, true);
    // Use of coords passed to helper function
    test("half4 helper(float2 xy) { return sample(child, xy); }"
         "half4 main(float2 xy) { return helper(xy); }", true, true);
}

DEF_GPUTEST_FOR_ALL_CONTEXTS(GrSkSLFP_Specialized, r, ctxInfo) {
    struct FpAndKey {
        std::unique_ptr<GrFragmentProcessor> fp;
        SkTArray<uint32_t, true>             key;
    };

    // Constant color, but with a similar option to GrFragmentProcessor::OverrideInput
    // specialize decides if the color is inserted in the SkSL as a literal, or left as a uniform
    auto make_color_fp = [&](SkPMColor4f color, bool specialize) {
        auto effect = SkMakeRuntimeEffect(SkRuntimeEffect::MakeForShader, R"(
            uniform half4 color;
            half4 main(float2 xy) { return color; }
        )");
        FpAndKey result;
        result.fp = GrSkSLFP::Make(std::move(effect), "color_fp", /*inputFP=*/nullptr,
                                   GrSkSLFP::OptFlags::kNone,
                                   "color", GrSkSLFP::SpecializeIf(specialize, color));
        GrProcessorKeyBuilder builder(&result.key);
        result.fp->getGLSLProcessorKey(*ctxInfo.directContext()->priv().caps()->shaderCaps(),
                                       &builder);
        builder.flush();
        return result;
    };

    FpAndKey uRed   = make_color_fp({1, 0, 0, 1}, false),
             uGreen = make_color_fp({0, 1, 0, 1}, false),
             sRed   = make_color_fp({1, 0, 0, 1}, true),
             sGreen = make_color_fp({0, 1, 0, 1}, true);

    // uRed and uGreen should have the same key - they just have different uniforms
    SkASSERT(uRed.key == uGreen.key);
    // sRed and sGreen should have keys that are different from the uniform case, and each other
    SkASSERT(sRed.key != uRed.key);
    SkASSERT(sGreen.key != uRed.key);
    SkASSERT(sRed.key != sGreen.key);
}
