#pragma once

#ifdef AVIATEUR_USE_GSTREAMER
    #include <gst/video/video-frame.h>
#endif
#include <libavutil/frame.h>
#include <pathfinder/common/math/mat3.h>
#include <pathfinder/gpu/device.h>
#include <pathfinder/gpu/queue.h>
#include <pathfinder/gpu/render_pipeline.h>
#include <pathfinder/gpu/texture.h>

#include <memory>
#include <optional>

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
    ~YuvRenderer() = default;
    void init();
    void render(const std::shared_ptr<Pathfinder::Texture>& outputTex);
    void updateTextureInfo(int width, int height, int format);
    void updateTextureData(const std::shared_ptr<AVFrame>& newFrameData);

#ifdef AVIATEUR_USE_GSTREAMER
    void updateTextureInfoGst(int width, int height, GstVideoFormat format);
    void updateTextureDataGst(GstVideoFrame vframe);
#endif

    void clear();

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
    std::shared_ptr<Pathfinder::DescriptorSet> mDescriptorSet;
    std::shared_ptr<Pathfinder::Sampler> mSampler;
    std::shared_ptr<Pathfinder::Buffer> mVertexBuffer;
    std::shared_ptr<Pathfinder::Buffer> mUniformBuffer;

    Pathfinder::Mat3 mXform;
    bool mXformChanged = true;

    int mPixFmt = 0;
    bool mPixFmtChanged = true;
    bool mTextureAllocated = false;

#ifdef AVIATEUR_USE_OPENCV
    VideoStabilizer mStabilizer;
    std::optional<cv::Mat> mPreviousFrameY;
#endif

    bool mNeedClear = false;

    std::shared_ptr<Pathfinder::Device> mDevice;

    volatile bool mInited = false;
};
