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

#ifndef INTEL_UFO_HWC_GLCELLCOMPOSER_H
#define INTEL_UFO_HWC_GLCELLCOMPOSER_H

#define GL_GLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <utils/RefBase.h>
#include <utils/LruCache.h>
#include <utils/JenkinsHash.h>

#include "PartitionedComposer.h"
#include "Option.h"

namespace intel {
namespace ufo {
namespace hwc {


/** \brief Helper class to save and restore the current GL context
 */
class GLContextSaver
{
public:
    void save();
    void restore();
private:
    bool       mbSaved           = false;
    EGLDisplay mPrevDisplay      = EGL_NO_DISPLAY;
    EGLSurface mPrevDrawSurface  = EGL_NO_SURFACE;
    EGLSurface mPrevReadSurface  = EGL_NO_SURFACE;
    EGLContext mPrevContext      = EGL_NO_CONTEXT;
};

struct GLContextRestorer
{
    void operator()(GLContextSaver* pSaver);
};

class GLContext
{
public:
    virtual ~GLContext();
    static std::shared_ptr<GLContext> create();

    using SavedContext = std::unique_ptr<GLContextSaver, GLContextRestorer>;
    SavedContext makeCurrent();

    EGLDisplay getDisplay() { return mDisplay; }

private:
    GLContext();

    EGLDisplay mDisplay = EGL_NO_DISPLAY; /// Current display id
    EGLSurface mSurface = EGL_NO_SURFACE; /// Rendering surface
    EGLContext mContext = EGL_NO_CONTEXT; /// Rendering context
    GLuint     mFboId   = 0; /// FBO id

    GLContextSaver mSavedContext;
};

class GlCellComposer : NonCopyable, public PartitionedComposer::CellComposer
{
public:
    static std::shared_ptr<GlCellComposer> create(std::shared_ptr<GLContext> context = NULL);

    virtual ~GlCellComposer();

    status_t beginFrame(const Content::LayerStack& source, const Layer& target);
    status_t drawLayerSet(uint32_t numIndices, const uint32_t* pIndices, const Region& region);
    status_t endFrame();

    bool isLayerSupportedAsInput(const Layer& layer);
    bool isLayerSupportedAsOutput(const Layer& layer);
    bool canBlankUnsupportedInputLayers();

    enum EProgramType {
        eCellRenderProgram,
        eCellRenderProgramNV12,
    };

    struct ProgramKey
    {
        EProgramType type;
        uint32_t numLayers;
        // Note that these masks currently (artificially) limit us to a maximum
        // of 32 layers per pass.
        uint32_t opaqueLayerMask;
        uint32_t premultLayerMask;
        uint32_t blankLayerMask;

        bool operator==(const GlCellComposer::ProgramKey& right) const
        {
            return !memcmp(this, &right, sizeof(right));
        }

        bool operator!=(const GlCellComposer::ProgramKey& right) const
        {
            return !(*this == right);
        }
    };

private:
    GlCellComposer(std::shared_ptr<GLContext> context);

    bool attachToFBO(GLuint textureId);

    status_t bindTexture(GLuint texturingUnit, GLuint textureId);

    status_t drawLayerSetInternal(uint32_t numIndices, const uint32_t* pIndices, const Region& region);

    bool shouldBlankLayer(const Layer& layer);

    /** \brief OpenGL shader

    Provides the common operations for OpenGL shaders.
    */

    struct ShaderDeleter { void operator()(int); typedef int pointer; };
    struct ProgramDeleter { void operator()(int); typedef int pointer; };

    using ShaderHandle = std::unique_ptr<int, ShaderDeleter>;
    using ProgramHandle = std::unique_ptr<int, ProgramDeleter>;

    static ShaderHandle createShader(GLenum shaderType, const char* source);
    static ProgramHandle createProgram(std::initializer_list< std::reference_wrapper<const ShaderHandle> > shaders);

    static bool use(const ProgramHandle& prog);

    /**
     Provides any GL program needed for compositing a rectangular region
     that a set of layers is known to fully cover. Each of the programs
     is composed by a vertex shader and a fragment shader.
     */

    class CProgramStore
    {
    public:
        // There are limits to the number of textures we can sample, however
        // there are also (artificial) limits because we use 32 bit masks for
        // some state.  If we need to support >32 then change the mask types!
        enum { maxNumLayers = 15 };

        CProgramStore();

        bool bind(
            uint32_t numLayers,
            float planeAlphas[],
            uint32_t opaqueLayerMask,
            uint32_t premultLayerMask,
            uint32_t blankLayerMask,
            bool renderToNV12);

        GLint getPositionVertexAttribute() const;
        uint32_t getNumTexCoords() const;
        GLint getTexCoordsVertexAttribute(uint32_t index) const;

    private:
        static bool getProgramLocations(
            const ProgramHandle& program,
            uint32_t numLayers,
            GLint* pvinPosition,
            GLint* pvinTexCoords,
            GLint* puPlaneAlphas,
            GLfloat* pPlaneAlphas,
            GLfloat* pWidth,
            GLfloat* pHeight);

        bool bindBackground(bool load, uint32_t numLayers, float planeAlphas[], uint32_t framebufferWidth, uint32_t framebufferHeight);

        class CRendererProgram
        {
        public:
            CRendererProgram(uint32_t numLayers, ProgramHandle handle);
            virtual ~CRendererProgram();

            const ProgramHandle &getHandle() { return mHandle; }

            GLint getPositionVertexIn() const { return mVinPosition; }
            uint32_t getNumPlanes() const { return mNumPlanes; };
            GLint getTexCoordsVertexIn(uint32_t index) const { return mVinTexCoords[index]; }

            bool setPlaneAlphaUniforms(uint32_t numLayers, float planeAlphas[]);
            bool getLocations();

        protected:
            ProgramHandle mHandle;
            GLint         mVinPosition = 0;
            uint32_t      mNumPlanes   = 0;
            GLint         mVinTexCoords[maxNumLayers+1];
            GLint         mUPlaneAlphas[maxNumLayers+1];
            GLfloat       mPlaneAlphas[maxNumLayers+1];
        };

        using RenderProgHandle = std::shared_ptr<CProgramStore::CRendererProgram>;

        static RenderProgHandle createProgram(
            uint32_t numLayers,
            uint32_t opaqueLayerMask,
            uint32_t premultLayerMask,
            uint32_t blankLayerMask,
            bool renderToNV12);

        RenderProgHandle mCurrent;
        android::LruCache<ProgramKey, RenderProgHandle> mPrograms;
    };

    class Texture
    {
    public:
        ~Texture();
        static std::unique_ptr<Texture> createTexture(const Layer& layer, AbstractBufferManager& bm, EGLDisplay display);

        GLuint getId() { return mTextureId; }
    private:
        Texture(sp<GraphicBuffer> pBuffer, EGLImageKHR eglImage, GLuint textureId, EGLDisplay display) :
            mpBuffer(pBuffer), mEglImage(eglImage), mTextureId(textureId), mDisplay(display)
        {
        }

        sp<GraphicBuffer> mpBuffer;
        EGLImageKHR       mEglImage  = EGL_NO_IMAGE_KHR;
        GLuint            mTextureId = 0;
        EGLDisplay        mDisplay   = EGL_NO_DISPLAY;
    };

    AbstractBufferManager&  mBm;

    static const uint32_t   NumVboIds          = 10;

    std::shared_ptr<GLContext> mContext;
    GLuint                  mVboIds[NumVboIds] = {0};
    uint32_t                mNextVboIdIndex    =  0;

    CProgramStore           mProgramStore;

    GLContext::SavedContext mSavedContext;

    // Layers for the current frame
    const Content::LayerStack* mpLayers = nullptr;

    // Destination texture
    uint32_t                 mDestWidth     = 0;
    uint32_t                 mDestHeight    = 0;
    std::unique_ptr<Texture> mDestTexture;

    // NV12 Rendering
    bool                    mDestTextureExternal = false;
    bool                    mNv12TargetSupported = false;
    Option                  mNv12RenderingEnabled;

    // Source textures
    std::vector< std::unique_ptr<Texture> > mSourceTextures;

    void bindAVbo();
};

} // namespace hwc
} // namespace ufo
} // namespace intel

namespace android {
template<> inline hash_t hash_type(const intel::ufo::hwc::GlCellComposer::ProgramKey& key)
{
    return JenkinsHashWhiten(JenkinsHashMixBytes(0, (uint8_t*)&key, sizeof(key)));
}
}


#endif // INTEL_UFO_HWC_GLCELLCOMPOSER_H
