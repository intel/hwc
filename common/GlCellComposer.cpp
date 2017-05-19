/*
// Copyright (c) 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <stdio.h>
#include "Hwc.h"
#include "PhysicalDisplay.h"
#include "GlCellComposer.h"
#include "Utils.h"
#include "Log.h"
#include "Utils.h"

#include <ufo/graphics.h>
#include <utils/String8.h>

#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES2/gl2ext.h>

#define GL_RENDER_TO_NV12_OPTION_NAME    "glrendertonv12"
#define GL_RENDER_TO_NV12_OPTION_DEFAULT 1

using namespace android;

namespace intel {
namespace ufo {
namespace hwc {

template<class T> static bool getGLErrorGen(const char* operation, const char* desc, T (*pFnGetError)(void), GLint successVal)
{
    if (sbInternalBuild)
    {
        GLint error = pFnGetError();
        if (error != successVal)
        {
            ALOGE("Error 0x%x on %s%s%s\n", error, operation, (desc ? ": " : ""), desc ? desc : "");
            return true;
        }
    }

    return false;
}

static bool getGLError(const char* operation, const char* desc = NULL)
{
    return getGLErrorGen(operation, desc, glGetError, GL_NO_ERROR);
}

static bool getEGLError(const char* operation, const char* desc = NULL)
{
    return getGLErrorGen(operation, desc, eglGetError, EGL_SUCCESS);
}

void GLContextSaver::save()
{
    ATRACE_CALL_IF(HWC_TRACE);

    mPrevDisplay = eglGetCurrentDisplay();
    getEGLError("eglGetCurrentDisplay");

    mPrevDrawSurface = eglGetCurrentSurface(EGL_DRAW);
    getEGLError("eglGetCurrentSurface");

    mPrevReadSurface = eglGetCurrentSurface(EGL_READ);
    getEGLError("eglGetCurrentSurface");

    mPrevContext = eglGetCurrentContext();
    getEGLError("eglGetCurrentContext");

    mbSaved = true;
}

void GLContextSaver::restore()
{
    ATRACE_CALL_IF(HWC_TRACE);

    if (mbSaved && mPrevContext != EGL_NO_CONTEXT)
    {
        eglMakeCurrent(mPrevDisplay, mPrevDrawSurface, mPrevReadSurface, mPrevContext);
        getEGLError("eglMakeCurrent");
        mbSaved = false;
    }
}

void GLContextRestorer::operator()(GLContextSaver* pSaver)
{
    if (pSaver)
        pSaver->restore();
}

GLContext::GLContext()
{
}


void GlCellComposer::ShaderDeleter::operator()(int id)
{
    if (id != 0)
    {
        glDeleteShader(id);
        getGLError("glDeleteShader");
    }
}

void GlCellComposer::ProgramDeleter::operator()(int id)
{
    if (id != 0)
    {
        glDeleteProgram(id);
        getGLError("glDeleteProgram");
    }
}

GlCellComposer::ShaderHandle GlCellComposer::createShader(GLenum shaderType, const char* source)
{
    ShaderHandle result = ShaderHandle( glCreateShader(shaderType), ShaderDeleter() );

    if (getGLError("glCreateShader") || (result.get() == 0))
        return NULL;

    // The object creation has finished and we set it up with some values
    glShaderSource(result.get(), 1, &source, NULL);
    if (getGLError("glShaderSource"))
        return NULL;

    glCompileShader(result.get());
    if (getGLError("glCompileShader"))
        return NULL;

    GLint compiledStatus = 0;

    glGetShaderiv(result.get(), GL_COMPILE_STATUS, &compiledStatus);
    if (getGLError("glGetShaderiv") || compiledStatus != GL_TRUE)
    {
        char buffer[1000];

        // Show the shader compilation errors
        const char* description = "Description not available";

        glGetShaderInfoLog(result.get(), sizeof(buffer), NULL, buffer);
        if (!getGLError("glGetShaderInfoLog"))
        {
            description = buffer;
        }

        ALOGE("Error on shader compilation: %s. \n%s\n", description, source);

        return NULL;
    }

    return result;
}

/** \brief Link several shaders to produce a ready to use program

Create a new program, attach some shaders and link. If the whole
sequence was successful the id of the newly created program replaces the
old one. The program id becomes 0 otherwise.
*/

GlCellComposer::ProgramHandle GlCellComposer::createProgram(std::initializer_list< std::reference_wrapper<const ShaderHandle> > shaders)
{
    ProgramHandle prog = ProgramHandle( glCreateProgram(), ProgramDeleter() );
    if (getGLError("glCreateProgram") || (prog.get() == 0))
        return NULL;

    // Attach the shaders
    for (const ShaderHandle& shader : shaders)
    {
        glAttachShader(prog.get(), shader.get());
        if (getGLError("glAttachShader"))
            return NULL;
    }

    // Link the program
    glLinkProgram(prog.get());
    if (getGLError("glLinkProgram"))
        return NULL;

    GLint linkStatus = GL_FALSE;

    glGetProgramiv(prog.get(), GL_LINK_STATUS, &linkStatus);
    if (getGLError("glGetProgramiv") || linkStatus != GL_TRUE)
    {
        char buffer[1000];

        // Show the shader compilation errors
        const char* description = "Description not available";

        glGetProgramInfoLog(prog.get(), sizeof(buffer), NULL, buffer);
        if (!getGLError("glGetProgramInfoLog"))
        {
            description = buffer;
        }

        ALOGE("Error on program linkage: %s", description );

        return NULL;
    }

    return prog;
}

bool GlCellComposer::use(const ProgramHandle& prog)
{
    glUseProgram(prog.get());
    return getGLError("glUseProgram") ? false : true;
}

GlCellComposer::CProgramStore::CProgramStore()
 : mPrograms(64) // TODO: check that this is a sensible size at some point...
{
}

GlCellComposer::CProgramStore::CRendererProgram::CRendererProgram(
    uint32_t numLayers, ProgramHandle handle):
        mHandle(std::move(handle)),
        mNumPlanes(numLayers)
{
    // Setup the per-plane data
    uint32_t index;
    for (index = 0; index < numLayers; ++index)
    {
        mVinTexCoords[index] = 0;
        mUPlaneAlphas[index] = 0;
    }
}

GlCellComposer::CProgramStore::CRendererProgram::~CRendererProgram()
{
}

bool GlCellComposer::CProgramStore::CRendererProgram::setPlaneAlphaUniforms(uint32_t numLayers, float planeAlphas[])
{
    ALOGD_IF(COMPOSER_DEBUG, "GlCellComposer::CProgramStore::CRendererProgram::setPlaneAlphaUniforms");
    uint32_t index;
    for (index = 0; index < numLayers; ++index)
    {
        ALOGD_IF(COMPOSER_DEBUG, "setPlaneAlphaUniforms %d %f, %f", index, planeAlphas[index], mPlaneAlphas[index]);
        if (fabs(planeAlphas[index] - mPlaneAlphas[index]) > 0.00001f)
        {
            ALOGD_IF(COMPOSER_DEBUG, "glUniform1f(mUPlaneAlphas[%d] == %d, %f)", index, mUPlaneAlphas[index], planeAlphas[index]);
            glUniform1f(mUPlaneAlphas[index], planeAlphas[index]);
            if (getGLError("glUniform1f", "Error setting up per-plane alpha uniform"))
            {
                return false;
            }
            mPlaneAlphas[index] = planeAlphas[index];
        }
    }

    return true;
}

bool GlCellComposer::CProgramStore::getProgramLocations(
    const ProgramHandle& program,
    uint32_t numLayers,
    GLint *pvinPosition,
    GLint *pvinTexCoords,
    GLint* puPlaneAlphas,
    GLfloat* pPlaneAlphas,
    GLfloat *pWidth,
    GLfloat *pHeight
)
{
    bool result = true;

    GLint vinPosition = 0;
    GLint vinTexCoords[numLayers];
    static const GLfloat defaultWidth = 1.f;
    static const GLfloat defaultHeight = 1.f;
    static const GLfloat defaultAlpha = 1.f;
    GLint uPlaneAlphas[numLayers];

    if (pvinPosition)
    {
        vinPosition = glGetAttribLocation(program.get(), "vinPosition");
        result = !getGLError("glGetAttribLocation");
    }

    if (pvinTexCoords)
    {
        uint32_t i;
        for (i = 0; result && i < numLayers; ++i)
        {
            static const char texCoordNameFormat[] = "vinTexCoords%d";
            char texCoordName[ (sizeof(texCoordNameFormat)-1) + 10 + 1];
            snprintf(texCoordName, sizeof(texCoordName), texCoordNameFormat, i);

            vinTexCoords[i] = glGetAttribLocation(program.get(), texCoordName);
            result = result && !getGLError("glGetAttribLocation");
        }

        GLint uTexture;
        if (result)
        {
            uTexture = glGetUniformLocation(program.get(), "uTexture");
            result = !getGLError("glGetUniformLocation", "Unable to find the uTexture uniform location");
        }

        // Setup a default value
        if (result)
        {
            GLint texturingUnits[numLayers];
            uint32_t i;
            for (i = 0; i < numLayers; ++i)
            {
                texturingUnits[i] = (GLint)i;
            }

            glUniform1iv(uTexture, numLayers, texturingUnits);
            result = !getGLError("glUniform1iv", "Unable to set the uTexture uniform");
        }
    }

    ALOGD_IF(COMPOSER_DEBUG, "puPlaneAlphas = %p", puPlaneAlphas);
    if (puPlaneAlphas)
    {
        uint32_t index;

        for (index = 0; result && index < numLayers; ++index)
        {
            String8 planeAlphaUniformName = String8::format("uPlaneAlpha[%d]", index);
            uPlaneAlphas[index] = glGetUniformLocation(program.get(), planeAlphaUniformName);
            if (getGLError("glGetUniformLocation"))
            {
                ALOGE("Unable to find the %s uniform location", planeAlphaUniformName.string());
                result = false;
            }

            // Setup a default alpha
            if (result)
            {
                ALOGD_IF(COMPOSER_DEBUG, "glUniform1f(uPlaneAlphas[%d] == %d, %f)", index, uPlaneAlphas[index], defaultAlpha);
                glUniform1f(uPlaneAlphas[index], defaultAlpha);
                if (getGLError("glUniform1iv"))
                {
                    ALOGE("Unable to set a default value for the %s uniform", planeAlphaUniformName.string());
                    result = false;
                }
            }
        }
    }

    // Setup the outputs, if everything went ok
    if (result)
    {
        if (pvinPosition)
            *pvinPosition = vinPosition;

        if (pvinTexCoords)
        {
            uint32_t index;
            for (index = 0; index < numLayers; ++index)
            {
                pvinTexCoords[index] = vinTexCoords[index];
            }
        }

        if (pWidth)
            *pWidth = defaultWidth;

        if (pHeight)
            *pHeight = defaultHeight;

        if (puPlaneAlphas)
        {
            uint32_t index;
            for (index = 0; index < numLayers; ++index)
            {
                puPlaneAlphas[index] = uPlaneAlphas[index];
                pPlaneAlphas[index] = defaultAlpha;
            }
        }
    }

    return result;
}

GlCellComposer::CProgramStore::RenderProgHandle
GlCellComposer::CProgramStore::createProgram(
    uint32_t numLayers,
    uint32_t opaqueLayerMask,
    uint32_t premultLayerMask,
    uint32_t blankLayerMask,
    bool renderToNV12)
{
    String8 vertexShaderSource;

    if (numLayers)
    {
        // Multiple layers
        static const char vertexShaderFormat [] =
            "#version 300 es\n"
            "in mediump vec2 vinPosition;\n"
            "%s"
            "\n"
            "out mediump vec2 finTexCoords[%d];\n"
            "\n"
            "void main()\n"
            "{\n"
            "    gl_Position = vec4(vinPosition.x, vinPosition.y, 0, 1);\n"
            "%s"
            "}";

        static const char texCoordDeclarationFormat[] = "in mediump vec2 vinTexCoords%d;\n";
        static const char texCoordSetupFormat[] = "    finTexCoords[%d] = vinTexCoords%d;\n";

        String8 texCoordDeclarationBlock;
        String8 texCoordSetupBlock;
        for (uint32_t i = 0; i < numLayers; ++i)
        {
            texCoordDeclarationBlock += String8::format(texCoordDeclarationFormat, i);
            texCoordSetupBlock += String8::format(texCoordSetupFormat, i, i);
        }

        vertexShaderSource = String8::format(vertexShaderFormat, texCoordDeclarationBlock.string(), numLayers, texCoordSetupBlock.string());
    }
    else
    {
        vertexShaderSource =
            "#version 300 es\n"
            "in mediump vec2 vinPosition;\n"
            "void main()\n"
            "{\n"
            "    gl_Position = vec4(vinPosition.x, vinPosition.y, 0, 1);\n"
            "}";
    }
    ALOGD_IF(COMPOSITION_DEBUG, "\nVertex Shader:\n%s\n", vertexShaderSource.string());

    auto vertexShader = GlCellComposer::createShader(GL_VERTEX_SHADER, vertexShaderSource);
    if (!vertexShader.get())
    {
        ALOGE("Error on \"composite\" vertex shader creation");
        return NULL;
    }

    String8 fragmentShaderSource;

    // Additional output declarations for NV12
    static const char fragmentShaderNV12OutputDecls[] =
        "#extension GL_EXT_YUV_target : require\n"
        "layout(yuv) "; // will add before "out vec4 cOut;\n"

    if (numLayers)
    {
        // Final Color Conversion for NV12
        static const char fragmentShaderNV12OutputConversion[] =
            "    vec3 yuvColor = rgb_2_yuv(outColor.xyz, itu_601);\n"
            "    outColor = vec4(yuvColor.xyz, outColor.w);\n";

        // Fragment shader main body
        static const char fragmentShaderFormat[] =
            "#version 300 es\n"
            "#extension GL_OES_EGL_image_external : require\n"
            "%sout vec4 outColor;\n" // Output Decls
            "\n"
            "uniform mediump sampler2D uTexture[%d];\n"
            "uniform mediump float uPlaneAlpha[%d];\n"
            "\n"
            "in mediump vec2 finTexCoords[%d];\n"
            "\n"
            "void main()\n"
            "{\n"
            "    mediump vec4 incoming;\n"
            "    mediump float planeAlpha;\n"
            "%s" // Layers
            "%s" // Output conversion
            "}";

        // Sample the given texture
        static const char blendingFormatSample[] =
            "    incoming = texture(uTexture[%d], finTexCoords[%d]);\n";

        // Treat the incoming as black (opaque)
        static const char blendingFormatSampleBlack[] =
            "    incoming = vec4(0,0,0,1);\n";

        // Get the plane-alpha
        static const char blendingFormatSamplePlaneAlpha[] =
            "    planeAlpha = uPlaneAlpha[%d];\n";

        // Apply the plane alpha differently for premult and coverage
        static const char blendingFormatPremultPlaneAlpha[] =
            "    incoming = incoming * planeAlpha;\n";

        static const char blendingFormatCoveragePlaneAlpha[] =
            "    incoming.a = incoming.a * planeAlpha;\n";

        // Apply the plane alpha for opaque surfaces (slightly more optimally)
        static const char blendingFormatOpaquePremultPlaneAlpha[] =
            "    incoming.rgb = incoming.rgb * planeAlpha;\n"
            "    incoming.a = planeAlpha;\n";

        static const char blendingFormatOpaqueCoveragePlaneAlpha[] =
            "    incoming.a = planeAlpha;\n";

        // Note: SurfaceFlinger has a big problem with coverage blending.
        // If asked to render a single plane with coverage: it will apply
        // the specified (SRC_ALPHA, 1-SRC_ALPHA) to all four channels
        // (as per OpenGL spec) and give us a result to blend with
        // (1, 1-SRC_ALPHA) this will produce a different dst alpha than if
        // SF had done the whole composition (with a back layer) in GL.
        // The 'correct' way to do the blend would be to apply
        // (SRC_ALPHA, 1-SRC_ALPHA) only to the rgb channels and
        // (1, 1-SRC_ALPHA) for the alpha.

        // Do the coverage multiply
        static const char blendingFormatCoverageMultiply[] =
            "    incoming.rgb = incoming.rgb * incoming.a;\n";

        // Write the colour directly for the first layer
        static const char blendingFormatWrite[] =
            "    outColor = incoming;\n";

        // Otherwise blend and write
        static const char blendingFormatWritePremultBlend[] =
            "    outColor = outColor * (1.0-incoming.a) + incoming;\n";

        String8 blendingBlock;

        for (uint32_t i = 0; i < numLayers; ++i)
        {
            if (blankLayerMask & (1 << i))
                blendingBlock += blendingFormatSampleBlack;
            else
                blendingBlock += String8::format(blendingFormatSample, i, i);
            blendingBlock += String8::format(blendingFormatSamplePlaneAlpha, i);

            bool opaque = opaqueLayerMask & (1 << i);
            bool premult = premultLayerMask & (1 << i);
            if (opaque)
            {
                if (premult)
                    blendingBlock += blendingFormatOpaquePremultPlaneAlpha;
                else
                    blendingBlock += blendingFormatOpaqueCoveragePlaneAlpha;
            }
            else
            {
                if (premult)
                    blendingBlock += blendingFormatPremultPlaneAlpha;
                else
                    blendingBlock += blendingFormatCoveragePlaneAlpha;
            }
            if (!premult)
                blendingBlock += blendingFormatCoverageMultiply;
            if (i == 0)
                blendingBlock += blendingFormatWrite;
            else
                blendingBlock += blendingFormatWritePremultBlend;
        }

        String8 outputDecls;
        String8 outputConversion;

        if (renderToNV12)
        {
            outputDecls = fragmentShaderNV12OutputDecls;
            outputConversion = fragmentShaderNV12OutputConversion;
        }

        fragmentShaderSource = String8::format(fragmentShaderFormat, outputDecls.string(), numLayers, numLayers, numLayers, blendingBlock.string(), outputConversion.string());
    }
    else
    {
        // Zero layers should result in clear to transparent
        static const char fragmentShaderFormat[] =
            "#version 300 es\n"
            "%sout vec4 outColor;\n"
            "void main()\n"
            "{\n"
            "    outColor = %s;\n"
            "}";

        String8 outputDecls;
        String8 outputValue;
        if (renderToNV12)
        {
            outputDecls = fragmentShaderNV12OutputDecls;
            outputValue = "vec4(rgb_2_yuv(vec3(0,0,0), itu_601), 0)";
        }
        else
        {
            outputValue = "vec4(0,0,0,0)";
        }

        fragmentShaderSource = String8::format(fragmentShaderFormat, outputDecls.string(), outputValue.string());
    }

    ALOGD_IF(COMPOSITION_DEBUG, "Fragment Shader:\n%s\n", fragmentShaderSource.string());

    auto fragmentShader = GlCellComposer::createShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (!fragmentShader.get())
    {
        ALOGE("Error on \"composite\" fragment shader creation");
        return NULL;
    }

    ProgramHandle program = GlCellComposer::createProgram({vertexShader, fragmentShader});
    if (!program.get())
    {
        ALOGE("Error on \"composite\" program shader creation");
    }
    else if (!use(program))
    {
        ALOGE("Error on \"composite\" program binding");
    }

    auto prog = std::make_shared<CRendererProgram>(numLayers, std::move(program));
    if (!prog->getLocations())
    {
        ALOGE("Error on \"composite\" program shader locations query");
    }
    else
    {
        return prog;
    }

    return NULL;
}

bool GlCellComposer::CProgramStore::CRendererProgram::getLocations()
{
    return getProgramLocations(
        mHandle,
        getNumPlanes(),
        &mVinPosition,
        &mVinTexCoords[0],
        mUPlaneAlphas,
        mPlaneAlphas,
        NULL,
        NULL);
}

bool GlCellComposer::CProgramStore::bind(
    uint32_t numLayers,
    float planeAlphas[],
    uint32_t opaqueLayerMask,
    uint32_t premultLayerMask,
    uint32_t blankLayerMask,
    bool renderToNV12)
{
    ALOG_ASSERT(numLayers <= maxNumLayers);

    ProgramKey key;
    key.type = (renderToNV12 ? eCellRenderProgramNV12 : eCellRenderProgram);
    key.numLayers = numLayers;
    key.opaqueLayerMask = opaqueLayerMask;
    key.premultLayerMask = premultLayerMask;
    key.blankLayerMask = blankLayerMask;

    // Make sure the program exists
    if (mPrograms.get(key) == NULL)
    {
        mPrograms.put(key, createProgram(numLayers, opaqueLayerMask, premultLayerMask, blankLayerMask, renderToNV12));
    }
    const RenderProgHandle &program = mPrograms.get(key);

    // Bind the program if it isn't bound already
    if ((mCurrent == program) || (program && use(program->getHandle())))
    {
        // Setup the uniforms
        if (program->setPlaneAlphaUniforms(numLayers, planeAlphas))
        {
            mCurrent = program;
        }
        return true;
    }

    return false;
}

GLint GlCellComposer::CProgramStore::getPositionVertexAttribute() const
{
    return (mCurrent != NULL) ? mCurrent->getPositionVertexIn() : 0;
}

uint32_t GlCellComposer::CProgramStore::getNumTexCoords() const
{
    return (mCurrent != NULL) ? mCurrent->getNumPlanes() : 0;
}

GLint GlCellComposer::CProgramStore::getTexCoordsVertexAttribute(uint32_t index) const
{
    return (mCurrent != NULL) ? mCurrent->getTexCoordsVertexIn(index) : 0;
}

// This composition class adds a dither pattern to the render target.
// It currently assumes that the dithering is to a 6:6:6 display

GlCellComposer::GlCellComposer(std::shared_ptr<GLContext> context):
    mBm( AbstractBufferManager::get() ),
    mContext(context),
    mNv12RenderingEnabled(GL_RENDER_TO_NV12_OPTION_NAME, GL_RENDER_TO_NV12_OPTION_DEFAULT)
{
}

GlCellComposer::~GlCellComposer()
{
    {
        GLContext::SavedContext saved;
        if (mContext)
            saved = mContext->makeCurrent();

        // Delete the vertex buffer object
        if (mVboIds[0] != 0)
        {
            // Destroy the VBO
            glDeleteBuffers(NumVboIds, mVboIds);
            getGLError("glDeleteBuffers");
        }

        mSourceTextures.clear();
    }
    mContext.reset();
}

bool GlCellComposer::isLayerSupportedAsInput(const Layer& layer)
{
    bool ret = true;

    // YV12 videos shows blending artefacts when partGLcomp used with this source layers.
    // Disable partglcomp (use vpp instead) as temporary workaround if such format present in input.
    int32_t format = layer.getBufferFormat();
    if (format == HAL_PIXEL_FORMAT_YV12)
        ret = false;

    ECompressionType compression = layer.getBufferCompression();
    if (!mBm.isCompressionSupportedByGL(compression))
    {
        ret = false;
    }

    if (layer.isEncrypted())
        ret = false;

    return ret;
}

bool GlCellComposer::shouldBlankLayer(const Layer& layer)
{
    return (0 == layer.getHandle()) || !isLayerSupportedAsInput(layer);
}

bool GlCellComposer::isLayerSupportedAsOutput(const Layer& layer)
{
    int32_t format = layer.getBufferFormat();
    ECompressionType compression = layer.getBufferCompression();
    bool hasPlaneAlpha = layer.isPlaneAlpha();
    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
            return mBm.isCompressionSupportedByGL(compression);
        case HAL_PIXEL_FORMAT_RGB_565:
            return (compression == COMPRESSION_NONE);
        case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
        case HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL:
        case HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL:
        case HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL:
            ALOGD_IF(COMPOSITION_DEBUG, "NV12HWC: %s format = %d (%s and %s)", __FUNCTION__, format, mNv12TargetSupported ? "supported" : "unsupported", mNv12RenderingEnabled ? "enabled" : "disabled");
            return ((mNv12TargetSupported && mNv12RenderingEnabled)
                    && (!hasPlaneAlpha) // NV12 output is not compatible with constant alpha
                    && (compression == COMPRESSION_NONE));
        default:
            return false;
    }
}

bool GlCellComposer::canBlankUnsupportedInputLayers()
{
    return true;
}

GLContext::SavedContext GLContext::makeCurrent()
{
    // Switch to our context (if available)
    if ( mDisplay != EGL_NO_DISPLAY &&
         mSurface != EGL_NO_SURFACE &&
         mContext != EGL_NO_CONTEXT)
    {
        mSavedContext.save();

        eglMakeCurrent(mDisplay, mSurface, mSurface, mContext);
        if (getEGLError("eglMakeCurrent"))
        {
            // Note, resources may leak if there is an error here. However, typically this
            // happens with global destructors in tests where its harmless.
            return NULL;
        }
        return SavedContext(&mSavedContext, GLContextRestorer());
    }
    return NULL;
}

GLContext::~GLContext()
{
    // Switch to our context (if available)
    auto savedContext = makeCurrent();

    // Delete the frame buffer object
    if (mFboId != 0)
    {
        // Destroy the FBO
        glDeleteFramebuffers(1, &mFboId);
        getGLError("glDeleteFramebuffers");
    }

    // Unset the context and surface
    if (mDisplay != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        getEGLError("eglMakeCurrent");

        // Destroy the surface
        if (mSurface != EGL_NO_SURFACE)
        {
            eglDestroySurface(mDisplay, mSurface);
            getEGLError("eglDestroySurface");
        }

        // Destroy the context
        if (mContext != EGL_NO_CONTEXT)
        {
            eglDestroyContext(mDisplay, mContext);
            getEGLError("eglDestroyContext");
        }
    }
}

std::shared_ptr<GLContext> GLContext::create()
{
    auto context = std::shared_ptr<GLContext>(new GLContext());
    if (!context)
        return NULL;

    // Get a connection to the display
    context->mDisplay = eglGetDisplay( EGL_DEFAULT_DISPLAY );
    if (getEGLError("eglGetDisplay") || context->mDisplay == EGL_NO_DISPLAY)
    {
        ALOGE("Error on eglGetDisplay");
        return NULL;
    }

    // Initialize EGL
    GLint majorVersion, minorVersion;

    GLint status = eglInitialize(context->mDisplay, &majorVersion, &minorVersion);
    if (getEGLError("eglInitialize") || status == EGL_FALSE)
    {
        ALOGE("Error on eglInitialize");
        return NULL;
    }

    // Get a configuration with at least 8 bits for red, green, blue and alpha.
    EGLConfig config;
    EGLint numConfigs;
    static EGLint const attributes[] =
    {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    eglChooseConfig(context->mDisplay, attributes, &config, 1, &numConfigs);
    if (getEGLError("eglChooseConfig") || numConfigs == 0)
    {
        ALOGE("Error on eglChooseConfig");
        return NULL;
    }

    // Create the context
    EGLint context_attribs[] =
    {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    context->mContext = eglCreateContext(context->mDisplay, config, EGL_NO_CONTEXT, context_attribs);
    if (getEGLError("eglCreateContext") || context->mContext == EGL_NO_CONTEXT)
    {
        ALOGE("Error on eglCreateContext");
        return NULL;
    }

    // Create a 16x16 pbuffer which is never going to be written to, so the
    // dimmensions do not really matter
    static EGLint const pbuffer_attributes[] =
    {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE
    };

    context->mSurface = eglCreatePbufferSurface(context->mDisplay, config, pbuffer_attributes);
    if (getEGLError("eglCreatePbufferSurface") || context->mSurface == EGL_NO_SURFACE)
    {
        ALOGE("Error on eglCreatePbufferSurface");
        return NULL;
    }

    // Switch to the newly created context.
    auto savedContext = context->makeCurrent();
    if (!savedContext)
    {
        ALOGE("Error on eglMakeCurrent");
        return NULL;
    }

    // Create the FBO
    glGenFramebuffers(1, &context->mFboId);
    if (getGLError("glGenFramebuffers"))
        return NULL;

    // Bind the frame buffer object
    glBindFramebuffer(GL_FRAMEBUFFER, context->mFboId);
    if (getGLError("glBindFramebuffer"))
        return NULL;

    return context;
}

std::shared_ptr<GlCellComposer> GlCellComposer::create(std::shared_ptr<GLContext> context)
{
    if (!context)
        context = GLContext::create();
    if (!context)
        return NULL;

    std::shared_ptr<GlCellComposer> composer(new GlCellComposer(context));
    if (!composer)
        return NULL;

    auto saved = context->makeCurrent();
    if (!saved)
        return NULL;

    // Create the vertex buffer object
    glGenBuffers(NumVboIds, composer->mVboIds);
    if (getGLError("glGenBuffers"))
        return NULL;

    // Because this is a dedicated context that is only used for the
    // dithering operations we can setup most of the context state as
    // constant for the whole context life cycle.

    if (NumVboIds == 1)
    {
        // Bind the VBO
        glBindBuffer(GL_ARRAY_BUFFER, composer->mVboIds[0]);
        getGLError("glBindBuffer");
    }

    // Disable blending
    glDisable(GL_BLEND);
    getGLError("glEnable");

    // Query the context for extension support
    const char* pExtensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    composer->mNv12TargetSupported = (pExtensions != NULL) && (strstr(pExtensions, "GL_EXT_YUV_target") != NULL);
    ALOGD_IF(COMPOSITION_DEBUG, "NV12HWC: NV12 rendering is %s", composer->mNv12TargetSupported ? "supported" : "unsupported");

    return composer;
}

bool GlCellComposer::attachToFBO(GLuint textureId)
{
    bool done = false;

    GLenum texTarget;
    if (mDestTextureExternal)
        texTarget = GL_TEXTURE_EXTERNAL_OES;
    else
        texTarget = GL_TEXTURE_2D;

    // Attach the colour buffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texTarget, textureId, 0);
    if (!getGLError("glFramebufferTexture2D", "A temporary texture could not be attached to the frame buffer object"))
    {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (getGLError("glCheckFramebufferStatus") || status != GL_FRAMEBUFFER_COMPLETE)
            ALOGE("The frame buffer is not ready");
        else
            done = true;
    }

    return done;
}


GlCellComposer::Texture::~Texture()
{
    if (0 != mTextureId)
    {
        glDeleteTextures(1, &mTextureId);
        getGLError("glDeleteTextures");
    }
    // Destroy the destination EGL image
    if (EGL_NO_IMAGE_KHR != mEglImage)
    {
        eglDestroyImageKHR(mDisplay, mEglImage);
        getEGLError("eglDestroyImageKHR");
    }
}

std::unique_ptr<GlCellComposer::Texture> GlCellComposer::Texture::createTexture(const Layer& layer, AbstractBufferManager& bm, EGLDisplay display)
{
    const uint32_t texturingUnit = 0;

    ALOG_ASSERT(layer.getHandle());

    sp<GraphicBuffer> pBuffer = bm.createGraphicBuffer( "GLCELLTEX",
                                        layer.getBufferWidth(), layer.getBufferHeight(),
                                        layer.getBufferFormat(),
                                        layer.getBufferUsage(),
                                        layer.getBufferPitch(),
                                        const_cast<native_handle*>(layer.getHandle()),
                                        false );

    EGLImageKHR eglImage = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, EGLClientBuffer(pBuffer->getNativeBuffer()), 0);
    if (getEGLError("eglCreateImageKHR", "A temporary EGL image could not be created"))
        return NULL;

    // Create a texture for the EGL image
    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    if (getGLError("glGenTextures", "A temporary texture could not be created"))
        return NULL;

    std::unique_ptr<Texture> pTex(new Texture(pBuffer, eglImage, textureId, display));

    glActiveTexture(GL_TEXTURE0 + texturingUnit);
    if (getGLError("glActiveTexture", "A temporary texture could not be set"))
        return NULL;

    glBindTexture(GL_TEXTURE_2D, textureId);
    if (getGLError("glBindTexture", "A temporary texture could not be set"))
        return NULL;

    GLint filter = layer.isScale() ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    if (getGLError("glTexParameteri", "A temporary texture could not be set"))
        return NULL;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    if (getGLError("glTexParameteri", "A temporary texture could not be set"))
        return NULL;

    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)eglImage);
    if (getGLError("glEGLImageTargetTexture2DOES", "A temporary texture could not be set"))
        return NULL;

    return pTex;
}

status_t GlCellComposer::bindTexture(
    GLuint texturingUnit,
    GLuint textureId)
{
    if (textureId == 0)
        return OK;

    glActiveTexture(GL_TEXTURE0 + texturingUnit);
    if (getGLError("glActiveTexture", "A temporary texture could not be set"))
        return UNKNOWN_ERROR;

    glBindTexture(GL_TEXTURE_2D, textureId);
    if (getGLError("glBindTexture", "A temporary texture could not be set"))
        return UNKNOWN_ERROR;

    return OK;
}

/**
 Create a quad where each vertex contains:
 - Anticlockwise-defined positions in NDC
 - Texture coordinates in [0,1] range, one pair per layer
*/

static void setupVBOData(
    GLfloat* vboData,
    uint32_t stride,
    uint32_t destWidth,
    uint32_t destHeight,
    float left,
    float right,
    float top,
    float bottom,
    const Content::LayerStack& layers,
    uint32_t setSize,
    const uint32_t *setIndices)
{
    GLfloat destCenterX = 0.5f * destWidth;
    GLfloat destCenterY = 0.5f * destHeight;

    float width2 = right - left;
    float height2 = bottom - top;
    float right2 = left + width2;
    float top2 = bottom - height2;

    // Calculate the corners in normalized device coordinates
    GLfloat ndcX0 = 2.f * (left - destCenterX) / destWidth;
    GLfloat ndcX1 = 2.f * (right2 - destCenterX) / destWidth;
    GLfloat ndcY0 = 2.f * (top2 - destCenterY) / destHeight;
    GLfloat ndcY1 = 2.f * (bottom - destCenterY) / destHeight;

    // Left-top
    vboData[0*stride+0] = ndcX0;
    vboData[0*stride+1] = ndcY0;

    // Left-bottom
    vboData[1*stride+0] = ndcX0;
    vboData[1*stride+1] = ndcY1;

    // Right-bottom
    vboData[2*stride+0] = ndcX1;
    vboData[2*stride+1] = ndcY1;

    // Right-top
    vboData[3*stride+0] = ndcX1;
    vboData[3*stride+1] = ndcY0;

    uint32_t i;
    for (i=0; i < setSize; ++i)
    {
        uint32_t ly = setIndices[i];
        const Layer& layer = layers[ly];

        // Calculate the insideness in the 0..+1 range
        GLfloat primWidthRec  = 1.f / (layer.getDst().right  - layer.getDst().left);
        GLfloat primHeightRec = 1.f / (layer.getDst().bottom - layer.getDst().top);

        GLfloat insidenessLeft   = (left   - layer.getDst().left) * primWidthRec;
        GLfloat insidenessRight  = (right  - layer.getDst().left) * primWidthRec;
        GLfloat insidenessTop    = (top    - layer.getDst().top)  * primHeightRec;
        GLfloat insidenessBottom = (bottom - layer.getDst().top)  * primHeightRec;

        // Use the insideness for calculating the texture coordinates
        GLfloat sourceWidthRec  = 1.f / (GLfloat)layer.getBufferWidth();
        GLfloat sourceHeightRec = 1.f / (GLfloat)layer.getBufferHeight();

        GLfloat sourceLeft   = ((GLfloat)layer.getSrc().left)   * sourceWidthRec;
        GLfloat sourceTop    = ((GLfloat)layer.getSrc().top)    * sourceHeightRec;
        GLfloat sourceRight  = ((GLfloat)layer.getSrc().right)  * sourceWidthRec;
        GLfloat sourceBottom = ((GLfloat)layer.getSrc().bottom) * sourceHeightRec;

        // Apply transforms and scale appropriately.
        if (isFlipH(layer.getTransform()))
        {
            swap(sourceLeft, sourceRight);
        }
        if (isFlipV(layer.getTransform()))
        {
            swap(sourceTop, sourceBottom);
        }
        if (isTranspose(layer.getTransform()))
        {
            GLfloat scaledLeftY   = sourceBottom + (sourceTop   - sourceBottom) * insidenessLeft;
            GLfloat scaledRightY  = sourceBottom + (sourceTop   - sourceBottom) * insidenessRight;
            GLfloat scaledTopX    = sourceLeft   + (sourceRight - sourceLeft)   * insidenessTop;
            GLfloat scaledBottomX = sourceLeft   + (sourceRight - sourceLeft)   * insidenessBottom;

            vboData[0*stride+2+2*i+0] = scaledTopX;
            vboData[0*stride+2+2*i+1] = scaledLeftY;
            vboData[1*stride+2+2*i+0] = scaledBottomX;
            vboData[1*stride+2+2*i+1] = scaledLeftY;
            vboData[2*stride+2+2*i+0] = scaledBottomX;
            vboData[2*stride+2+2*i+1] = scaledRightY;
            vboData[3*stride+2+2*i+0] = scaledTopX;
            vboData[3*stride+2+2*i+1] = scaledRightY;
        }
        else
        {
            GLfloat scaledLeftX   = sourceLeft + (sourceRight  - sourceLeft) * insidenessLeft;
            GLfloat scaledRightX  = sourceLeft + (sourceRight  - sourceLeft) * insidenessRight;
            GLfloat scaledTopY    = sourceTop  + (sourceBottom - sourceTop)  * insidenessTop;
            GLfloat scaledBottomY = sourceTop  + (sourceBottom - sourceTop)  * insidenessBottom;

            vboData[0*stride+2+2*i+0] = scaledLeftX;
            vboData[0*stride+2+2*i+1] = scaledTopY;
            vboData[1*stride+2+2*i+0] = scaledLeftX;
            vboData[1*stride+2+2*i+1] = scaledBottomY;
            vboData[2*stride+2+2*i+0] = scaledRightX;
            vboData[2*stride+2+2*i+1] = scaledBottomY;
            vboData[3*stride+2+2*i+0] = scaledRightX;
            vboData[3*stride+2+2*i+1] = scaledTopY;
        }
    }
}

status_t GlCellComposer::beginFrame(const Content::LayerStack& source, const Layer& target)
{
    ATRACE_CALL_IF(HWC_TRACE);

    ALOGD_IF(COMPOSITION_DEBUG, "GlCellComposer::beginFrame\n%sRT %s", source.dump().string(), target.dump().string());

    ALOG_ASSERT(target.getHandle());

    // Realloc the source layers array
    if (source.size() > mSourceTextures.size())
    {
        mSourceTextures.resize(source.size());
    }

    // Switch context and save the old GL context for later
    mSavedContext = mContext->makeCurrent();
    if (!mSavedContext)
    {
        return UNKNOWN_ERROR;
    }

    mpLayers = &source;

    // Set the destination texture
    mDestTexture = Texture::createTexture(target, mBm, mContext->getDisplay());
    mDestWidth = target.getDstWidth();
    mDestHeight = target.getDstHeight();

    uint32_t bufferFormat = target.getBufferFormat();
    ALOGD_IF(COMPOSITION_DEBUG, "NV12HWC: %s destBufferFormat=%d", __FUNCTION__, bufferFormat);
    switch (bufferFormat)
    {
    case HAL_PIXEL_FORMAT_NV12_Y_TILED_INTEL:
    case HAL_PIXEL_FORMAT_NV12_LINEAR_INTEL:
    case HAL_PIXEL_FORMAT_NV12_LINEAR_PACKED_INTEL:
    case HAL_PIXEL_FORMAT_NV12_X_TILED_INTEL:
        mDestTextureExternal = true;
        break;
    default:
        mDestTextureExternal = false;
        break;
    }

    if (!mDestTexture || !attachToFBO(mDestTexture->getId()))
    {
        mDestTexture.reset();
        return UNKNOWN_ERROR;
    }

    // Create the source textures
    for (uint32_t i = 0; i < source.size(); ++i)
    {
        // Handle invalid textures appropriately.
        if (shouldBlankLayer(source[i]))
        {
            mSourceTextures[i].reset();
            Log::alogd(true, "GlCellComposer: blanking unsupported layer %d", i);
        }
        else
        {
            mSourceTextures[i] = Texture::createTexture(source[i], mBm, mContext->getDisplay());
            if (!mSourceTextures[i])
            {
                break;
            }
            mBm.setBufferUsage( source[i].getHandle(), AbstractBufferManager::eBufferUsage_GL );
        }
    }

    // Adjust the view port for covering the whole destination rectangle
    glViewport(0, 0, mDestWidth, mDestHeight);
    getGLError("glViewport");

    return OK;
}

String8 static dump(uint32_t numIndices, const uint32_t* pIndices, const Region& region)
{
    String8 output = String8::format("numIndices:%d ", numIndices);
    for(uint32_t i=0; i < numIndices; i++)
        output += String8::format("%d,", pIndices[i]);

    size_t size;
    const Rect* pRects = region.getArray(&size);

    output += String8::format(" numRects:%zd ", size);
    for (uint32_t i = 0; i < size; i++)
        output += String8::format("(%d, %d, %d, %d) ", pRects[i].left, pRects[i].top, pRects[i].right, pRects[i].bottom);
    return output;
}

status_t GlCellComposer::drawLayerSetInternal(uint32_t numIndices, const uint32_t* pIndices, const Region& region)
{
    ATRACE_CALL_IF(HWC_TRACE);

    ALOGD_IF(COMPOSITION_DEBUG, "GlCellComposer::drawLayerSetInternal: %s", dump(numIndices, pIndices, region).string());

    // Check that the destination texture is ready to go.
    ALOG_ASSERT(mDestTexture);

    // Bind the source textures
    uint32_t i;
    for (i = 0; i < numIndices; ++i)
    {
        uint32_t ly = pIndices[i];
        status_t status = bindTexture(i, mSourceTextures[ly] ? mSourceTextures[ly]->getId() : 0);
        if (status != OK)
        {
            ALOGE("Unable to bind a source texture");
            return UNKNOWN_ERROR;
        }
    }

    // We use a 32bit mask for layer state so we cannot exceed that number of
    // layers without making changes.
    ALOG_ASSERT(CProgramStore::maxNumLayers <= 32);
    ALOG_ASSERT(numIndices <= 32);

    // Setup a vector with the per-plane alphas and masks for transparency state
    float planeAlphas[ numIndices ];
    uint32_t opaqueMask = 0;
    uint32_t premultMask = 0;
    uint32_t blankMask = 0;
    for (i = 0; i < numIndices; ++i)
    {
        uint32_t ly = pIndices[i];
        const Layer& layer = mpLayers->getLayer(ly);
        planeAlphas[i] = layer.getPlaneAlpha();

        const EBlendMode blending = layer.getBlending();
        // Mark opaque if blending is none and there is an alpha channel.
        // The theory is that there are so few surfaces like this that we will
        // generate fewer program combinations.
        // Likewise for coverage blending.
        if ((blending == EBlendMode::NONE) && layer.isAlpha())
            opaqueMask |= (1 << i);
        if (blending != EBlendMode::COVERAGE)
            premultMask |= (1 << i);

        // Blank any layer we can't actually read.
        if (shouldBlankLayer(layer))
            blankMask |= (1 << i);
    }

    // Bind the program
    bool isProgramBound =  mProgramStore.bind(numIndices, planeAlphas, opaqueMask, premultMask, blankMask, mDestTextureExternal);

    if (isProgramBound)
    {
        // Todo pass this into glDrawArrays
        size_t numVisibleRegions;
        const Rect* visibleRegions = region.getArray(&numVisibleRegions);

        // Setup the VBO contents
        uint32_t vertexStride = 2 + 2*numIndices;
        uint32_t quadStride = 4*vertexStride;
        uint32_t vboCount = numVisibleRegions*quadStride;
        GLfloat vboData[vboCount];

        uint32_t regionIndex;
        for (regionIndex = 0; regionIndex < numVisibleRegions; ++regionIndex)
        {
            setupVBOData(
                vboData + quadStride*regionIndex,
                vertexStride,
                mDestWidth,
                mDestHeight,
                visibleRegions[regionIndex].left,
                visibleRegions[regionIndex].right,
                visibleRegions[regionIndex].top,
                visibleRegions[regionIndex].bottom,
                *mpLayers,
                numIndices,
                pIndices);
        }

        // Bind a VBO
        bindAVbo();

        // Discard the previous contents and setup new ones
        glBufferData(GL_ARRAY_BUFFER, sizeof(vboData), vboData, GL_STREAM_DRAW);
        getGLError("glBufferData");

        glVertexAttribPointer(mProgramStore.getPositionVertexAttribute(), 2, GL_FLOAT, GL_FALSE, vertexStride*sizeof(GLfloat), NULL);
        getGLError("glVertexAttribPointer");

        glEnableVertexAttribArray(mProgramStore.getPositionVertexAttribute());
        getGLError("glEnableVertexAttribArray");

        for (i=0; i < mProgramStore.getNumTexCoords(); ++i)
        {
            glVertexAttribPointer(mProgramStore.getTexCoordsVertexAttribute(i), 2, GL_FLOAT, GL_FALSE, vertexStride*sizeof(GLfloat), (void*)((2+2*i)*sizeof(GLfloat)));
            getGLError("glVertexAttribPointer");

            glEnableVertexAttribArray(mProgramStore.getTexCoordsVertexAttribute(i));
            getGLError("glEnableVertexAttribArray");
        }

        // Build an index buffer describing the triangles for all the rects.
        GLushort indices[numVisibleRegions*6];
        uint32_t outidx = 0, vidx = 0;
        for (regionIndex = 0; regionIndex < numVisibleRegions; ++regionIndex)
        {
            indices[outidx++] = vidx + 0;
            indices[outidx++] = vidx + 1;
            indices[outidx++] = vidx + 2;
            indices[outidx++] = vidx + 0;
            indices[outidx++] = vidx + 2;
            indices[outidx++] = vidx + 3;
            vidx += 4;
        }

        glDrawElements(GL_TRIANGLES, numVisibleRegions*6, GL_UNSIGNED_SHORT, indices);
        getGLError("glDrawArrays");
    }

    return OK;
}

status_t GlCellComposer::drawLayerSet(uint32_t numIndices, const uint32_t* pIndices, const Region& region)
{
    ATRACE_CALL_IF(HWC_TRACE);

    ALOGD_IF(COMPOSITION_DEBUG, "GlCellComposer::drawLayerSet: %s", dump(numIndices, pIndices, region).string());

    // Check that the destination texture is attached to the FBO
    if (!mDestTexture)
    {
        ALOGE("The destination texture is not attached to the FBO");
        return UNKNOWN_ERROR;
    }

    const uint32_t maxTextures = CProgramStore::maxNumLayers;

    uint32_t startIndex = 0;
    do
    {
        const uint32_t endIndex = startIndex + min(numIndices - startIndex, maxTextures);
        const uint32_t numIndicesThisPass = endIndex - startIndex;

        if (startIndex > 0)
        {
            ALOGD_IF(COMPOSITION_DEBUG, "NV12HWC: Enabling Blend!");
            ALOG_ASSERT(!mDestTextureExternal);

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        else
        {
            glDisable(GL_BLEND);
        }

        drawLayerSetInternal(numIndicesThisPass, &pIndices[startIndex], region);

        startIndex = endIndex;
    } while (startIndex < numIndices);

    return OK;
}

status_t GlCellComposer::endFrame()
{
    ATRACE_CALL_IF(HWC_TRACE);

    glFlush();
    status_t result = (getGLError("glFlush")) ? UNKNOWN_ERROR : OK;

    // Destroy the destination texture
    mDestTexture.reset();

    // Destroy the source textures
    mSourceTextures.clear();

    // Restore the GL context
    mSavedContext.reset();

    return result;
}

void GlCellComposer::bindAVbo()
{
    if (NumVboIds > 1)
    {
        // Bind the VBO
        glBindBuffer(GL_ARRAY_BUFFER, mVboIds[mNextVboIdIndex]);
        getGLError("glBindBuffer");

        mNextVboIdIndex = (mNextVboIdIndex + 1) % NumVboIds;
    }
}

} // namespace hwc
} // namespace ufo
} // namespace intel
