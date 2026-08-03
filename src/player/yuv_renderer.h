#pragma once

#include <libavutil/frame.h>
#include <pathfinder/common/math/mat3.h>
#include <pathfinder/gpu/device.h>
#include <pathfinder/gpu/queue.h>
#include <pathfinder/gpu/render_pipeline.h>
#include <pathfinder/gpu/texture.h>

#include <memory>
#include <optional>

#ifdef __APPLE__
// Undefine MSR to avoid conflict with macOS SDK headers.
// devourer's hal_com_reg.h defines MSR which clashes with MachineExceptions.h
// included transitively by CoreVideo headers.
#ifdef MSR
#undef MSR
#endif

// CoreVideo base types (CVImageBufferRef etc.) are defined in CoreVideo.h
#include <CoreVideo/CoreVideo.h>

// Forward declaration for CVMetalTextureCache (Metal-specific type not in base CoreView.h)
typedef struct __CVMetalTextureCache *CVMetalTextureCacheRef;

// CVMetalTextureRef is actually CVImageBufferRef (defined in CoreVideo.h)
// We create a matching typedef for clarity
typedef CVImageBufferRef CVMetalTextureRef;
#endif

#ifdef AVIATEUR_USE_OPENCV
    #include "../feature/low_light_enhancer.h"
    #include "../feature/video_stabilizer.h"
#endif

namespace cv {
class Mat;
}

class YuvRenderer {
public:
    YuvRenderer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue);
    ~YuvRenderer();
    void init();
    void render(const std::shared_ptr<Pathfinder::Texture>& outputTex);
    void updateTextureInfo(int width, int height, int format);
    void updateTextureData(const std::shared_ptr<AVFrame>& newFrameData);

    void clear();

    bool isTextureAllocated() const { return mTextureAllocated; }

#ifdef __APPLE__
    bool isZeroCopyAvailable() const { return mZeroCopyAvailable; }
    void updateTextureFromHwFrame(const std::shared_ptr<AVFrame>& hwFrame);
#endif

#ifdef AVIATEUR_USE_OPENCV
    bool mStabilize = false;
    bool mLowLightEnhancement = false;
    std::optional<LowLightEnhancer> mLowLightEnhancer;
#endif

protected:
    void initPipeline();
    void initGeometry();

private:
    std::shared_ptr<Pathfinder::RenderPipeline> mPipeline;
    std::shared_ptr<Pathfinder::Queue> mQueue;
    std::shared_ptr<Pathfinder::Fence> mFence;
    std::shared_ptr<Pathfinder::RenderPass> mRenderPass;
    std::shared_ptr<Pathfinder::Texture> mTexY;
    std::shared_ptr<Pathfinder::Texture> mTexU;
    std::shared_ptr<Pathfinder::Texture> mTexV;
    std::shared_ptr<AVFrame> mPrevFrameData;
    std::shared_ptr<Pathfinder::DescriptorSetLayout> mDescriptorSetLayout;
    std::shared_ptr<Pathfinder::DescriptorSet> mDescriptorSet;
    std::shared_ptr<Pathfinder::Sampler> mSampler;
    std::shared_ptr<Pathfinder::Buffer> mVertexBuffer;
    std::shared_ptr<Pathfinder::Buffer> mUniformBuffer;

    Pathfinder::Mat3 mXform;
    bool mXformChanged = true;

    int mPixFmt = 0;
    bool mPixFmtChanged = true;
    bool mTextureAllocated = false;
    bool mZeroCopyAvailable = false;

#ifdef AVIATEUR_USE_OPENCV
    VideoStabilizer mStabilizer;
    std::optional<cv::Mat> mPreviousFrameY;
#endif

    bool mNeedClear = false;

    std::shared_ptr<Pathfinder::Device> mDevice;

    std::vector<uint8_t> mPackedY;
    std::vector<uint8_t> mPackedU;
    std::vector<uint8_t> mPackedV;

#ifdef __APPLE__
    CVMetalTextureCacheRef mCvMetalCache = nullptr;
    CVMetalTextureRef mCvTexY = nullptr;
    CVMetalTextureRef mCvTexUV = nullptr;
    void releaseCvTextures();
    void initZeroCopy();
#endif

    volatile bool mInited = false;
};
