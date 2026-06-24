#include "yuv_renderer.h"

#include <libavutil/pixfmt.h>
#include <pathfinder/common/color.h>
#include <pathfinder/common/math/mat4.h>

#include <utility>

#include "resources/resource.h"

// clang-format off
// SPV
#include "../shaders/generated/yuv_frag_spv.h"
#include "../shaders/generated/yuv_vert_spv.h"
// GLSL
#include "../shaders/generated/yuv_frag.h"
#include "../shaders/generated/yuv_vert.h"
#include "src/gui_interface.h"
// clang-format on

struct FragUniformBlock {
    Pathfinder::Mat4 xform;
    int pixFmt;
    int pad0;
    int pad1;
    int pad2;
};

YuvRenderer::YuvRenderer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue) {
    mDevice = std::move(device);
    mQueue = std::move(queue);
}

void YuvRenderer::init() {
    mRenderPass = mDevice->create_render_pass(Pathfinder::TextureFormat::Rgba8Unorm,
                                              Pathfinder::AttachmentLoadOp::Clear,
                                              "yuv render pass");

    mXform = Pathfinder::Mat3(1);

    mFence = mDevice->create_fence("VideoPlayerGst fence");

    initPipeline();
    initGeometry();
}

void YuvRenderer::initGeometry() {
    // Set up vertex data (and buffer(s)) and configure vertex attributes.
    constexpr float vertices[] = {
        // Positions, UVs.
        -1.0, -1.0, 0.0, 0.0, // 0
        1.0,  -1.0, 1.0, 0.0, // 1
        1.0,  1.0,  1.0, 1.0, // 2
        -1.0, -1.0, 0.0, 0.0, // 3
        1.0,  1.0,  1.0, 1.0, // 4
        -1.0, 1.0,  0.0, 1.0  // 5
    };

    mVertexBuffer = mDevice->create_buffer(
        {Pathfinder::BufferType::Vertex, sizeof(vertices), Pathfinder::MemoryProperty::DeviceLocal},
        "yuv renderer vertex buffer");

    auto encoder = mDevice->create_command_encoder("upload yuv vertex buffer");
    encoder->write_buffer(mVertexBuffer, 0, sizeof(vertices), vertices);
    mQueue->submit(encoder, mFence);
}

void YuvRenderer::initPipeline() {
    std::vector<char> vert_source, frag_source;

    if (mDevice->get_backend_type() == Pathfinder::BackendType::Vulkan) {
        vert_source = std::vector<char>(std::begin(aviateur::yuv_vert_spv), std::end(aviateur::yuv_vert_spv));
        frag_source = std::vector<char>(std::begin(aviateur::yuv_frag_spv), std::end(aviateur::yuv_frag_spv));
    } else {
        vert_source = std::vector<char>(std::begin(aviateur::yuv_vert), std::end(aviateur::yuv_vert));
        frag_source = std::vector<char>(std::begin(aviateur::yuv_frag), std::end(aviateur::yuv_frag));
    }

    std::vector<Pathfinder::VertexInputAttributeDescription> attribute_descriptions;

    constexpr uint32_t stride = 4 * sizeof(float);

    attribute_descriptions.push_back({0, 2, Pathfinder::DataType::f32, stride, 0, Pathfinder::VertexInputRate::Vertex});

    attribute_descriptions.push_back(
        {0, 2, Pathfinder::DataType::f32, stride, 2 * sizeof(float), Pathfinder::VertexInputRate::Vertex});

    Pathfinder::BlendState blend_state{};
    blend_state.enabled = false;

    mUniformBuffer = mDevice->create_buffer(
        {Pathfinder::BufferType::Uniform, sizeof(FragUniformBlock), Pathfinder::MemoryProperty::HostVisibleAndCoherent},
        "yuv renderer uniform buffer");

    {
        std::vector<Pathfinder::DescriptorLayout> layouts = {
            {0, Pathfinder::ShaderStage::VertexAndFragment, Pathfinder::DescriptorType::UniformBuffer, "bUniform0"},
            {1, Pathfinder::ShaderStage::Fragment, Pathfinder::DescriptorType::Sampler, "tex_y"},
            {2, Pathfinder::ShaderStage::Fragment, Pathfinder::DescriptorType::Sampler, "tex_u"},
            {3, Pathfinder::ShaderStage::Fragment, Pathfinder::DescriptorType::Sampler, "tex_v"},
        };

        mDescriptorSetLayout = mDevice->create_descriptor_set_layout(layouts);
    }

    mDescriptorSet = mDevice->create_descriptor_set(mDescriptorSetLayout);
    mDescriptorSet->add_or_update({
        Pathfinder::Descriptor::uniform(0, mUniformBuffer),
    });

    Pathfinder::SamplerDescriptor sampler_desc{};
    sampler_desc.mag_filter = Pathfinder::SamplerFilter::Nearest;
    sampler_desc.min_filter = Pathfinder::SamplerFilter::Nearest;
    sampler_desc.address_mode_u = Pathfinder::SamplerAddressMode::ClampToEdge;
    sampler_desc.address_mode_v = Pathfinder::SamplerAddressMode::ClampToEdge;

    mSampler = mDevice->create_sampler(sampler_desc);

    mPipeline = mDevice->create_render_pipeline(
        mDevice->create_shader_module(vert_source, Pathfinder::ShaderStage::Vertex, "yuv vert"),
        mDevice->create_shader_module(frag_source, Pathfinder::ShaderStage::Fragment, "yuv frag"),
        attribute_descriptions,
        blend_state,
        mDescriptorSetLayout,
        Pathfinder::TextureFormat::Rgba8Unorm,
        "yuv pipeline");
}

void YuvRenderer::updateTextureInfo(int width, int height, int format) {
    if (width == 0 || height == 0) {
        return;
    }

    mPixFmt = format;
    mPixFmtChanged = true;

    mTexY = mDevice->create_texture({{width, height}, Pathfinder::TextureFormat::R8}, "y texture");

    if (format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P) {
        GuiInterface::Instance().PutLog(LogLevel::Info, "YUV pixel format is YUV420P/YUVJ420P", __FUNCTION__);

        mTexU = mDevice->create_texture({{width / 2, height / 2}, Pathfinder::TextureFormat::R8}, "u texture");

        mTexV = mDevice->create_texture({{width / 2, height / 2}, Pathfinder::TextureFormat::R8}, "v texture");
    } else if (format == AV_PIX_FMT_NV12) {
        GuiInterface::Instance().PutLog(LogLevel::Info, "YUV pixel format is NV12", __FUNCTION__);

        mTexU = mDevice->create_texture({{width / 2, height / 2}, Pathfinder::TextureFormat::Rg8}, "u texture");

        // V is not used for NV12.
        if (mTexV == nullptr) {
            mTexV = mDevice->create_texture({{2, 2}, Pathfinder::TextureFormat::R8}, "dummy v texture");
        }
    } else if (format == AV_PIX_FMT_YUV444P) {
        GuiInterface::Instance().PutLog(LogLevel::Info, "YUV pixel format is YUV444P", __FUNCTION__);

        mTexU = mDevice->create_texture({{width, height}, Pathfinder::TextureFormat::R8}, "u texture");

        mTexV = mDevice->create_texture({{width, height}, Pathfinder::TextureFormat::R8}, "v texture");
    } else {
        GuiInterface::Instance().PutLog(LogLevel::Error, "YUV pixel format is unsupported!", __FUNCTION__);

        abort();
    }

    mTextureAllocated = true;
}

void YuvRenderer::updateTextureData(const std::shared_ptr<AVFrame>& newFrameData) {
    if (mTexY == nullptr) {
        return;
    }

    auto encoder = mDevice->create_command_encoder("upload yuv data");

#ifdef AVIATEUR_USE_OPENCV
    if (mStabilize) {
        const auto frameY = cv::Mat(mTexY->get_size().y,
                                    mTexY->get_size().x,
                                    CV_8UC1,
                                    (void*)newFrameData->data[0],
                                    newFrameData->linesize[0]);

        if (mPreviousFrameY.has_value()) {
            auto stabXform = mStabilizer.stabilize(mPreviousFrameY.value(), frameY);

            mXform = Pathfinder::Mat3(1);
            mXform.v[0] = stabXform.at<double>(0, 0);
            mXform.v[3] = stabXform.at<double>(0, 1);
            mXform.v[1] = stabXform.at<double>(1, 0);
            mXform.v[4] = stabXform.at<double>(1, 1);
            mXform.v[6] = stabXform.at<double>(0, 2) / mTexY->get_size().x;
            mXform.v[7] = stabXform.at<double>(1, 2) / mTexY->get_size().y;

            mXform = mXform.scale(
                Pathfinder::Vec2F(1.0f + static_cast<float>(HORIZONTAL_BORDER_CROP) / mTexY->get_size().x));
        }

        mPreviousFrameY = frameY.clone();

        if (!mPrevFrameData) {
            mPrevFrameData = newFrameData;
        }

        if (mPrevFrameData->linesize[0]) {
            const void* texYData = mPrevFrameData->data[0];
            int width = mTexY->get_size().x;
            int height = mTexY->get_size().y;
            if (mPrevFrameData->linesize[0] != width) {
                mPackedY.resize(width * height);
                for (int i = 0; i < height; ++i) {
                    memcpy(mPackedY.data() + i * width,
                           mPrevFrameData->data[0] + i * mPrevFrameData->linesize[0],
                           width);
                }
                texYData = mPackedY.data();
            }
            encoder->write_texture(mTexY, {}, texYData);
        }
        if (mPrevFrameData->linesize[1]) {
            const void* texUData = mPrevFrameData->data[1];
            int rowWidth = mTexU->get_size().x * (mPixFmt == AV_PIX_FMT_NV12 ? 2 : 1);
            int height = mTexU->get_size().y;
            if (mPrevFrameData->linesize[1] != rowWidth) {
                mPackedU.resize(rowWidth * height);
                for (int i = 0; i < height; ++i) {
                    memcpy(mPackedU.data() + i * rowWidth,
                           mPrevFrameData->data[1] + i * mPrevFrameData->linesize[1],
                           rowWidth);
                }
                texUData = mPackedU.data();
            }
            encoder->write_texture(mTexU, {}, texUData);
        }
        if (mPrevFrameData->linesize[2] && mPixFmt != AV_PIX_FMT_NV12) {
            const void* texVData = mPrevFrameData->data[2];
            int width = mTexV->get_size().x;
            int height = mTexV->get_size().y;
            if (mPrevFrameData->linesize[2] != width) {
                mPackedV.resize(width * height);
                for (int i = 0; i < height; ++i) {
                    memcpy(mPackedV.data() + i * width,
                           mPrevFrameData->data[2] + i * mPrevFrameData->linesize[2],
                           width);
                }
                texVData = mPackedV.data();
            }
            encoder->write_texture(mTexV, {}, texVData);
        }

        mQueue->submit_and_wait(encoder);

        // Do this after submitting.
        mPrevFrameData = newFrameData;
    } else
#endif
    {
#ifdef AVIATEUR_USE_OPENCV
        if (mPreviousFrameY.has_value()) {
            mPreviousFrameY.reset();
        }
#endif

        if (mPrevFrameData) {
            mPrevFrameData.reset();
        }

        mXform = Pathfinder::Mat3(1);

// Keep the cv frame alive until we call `submit_and_wait`
#ifdef AVIATEUR_USE_OPENCV
        cv::Mat enhancedFrameY;
#endif

        if (newFrameData->linesize[0]) {
            const void* texYData = newFrameData->data[0];
            int width = mTexY->get_size().x;
            int height = mTexY->get_size().y;

#ifdef AVIATEUR_USE_OPENCV
            if (mLowLightEnhancement) {
                if (!mLowLightEnhancer.has_value()) {
                    mLowLightEnhancer = LowLightEnhancer(revector::get_asset_dir("weights/pairlie_180x320.onnx"));
                }

                cv::Mat originalFrameY =
                    cv::Mat(height, width, CV_8UC1, (void*)newFrameData->data[0], newFrameData->linesize[0]);

                enhancedFrameY = mLowLightEnhancer->detect(originalFrameY);

                texYData = enhancedFrameY.data;
            } else
#endif
                if (newFrameData->linesize[0] != width) {
                mPackedY.resize(width * height);
                for (int i = 0; i < height; ++i) {
                    memcpy(mPackedY.data() + i * width, newFrameData->data[0] + i * newFrameData->linesize[0], width);
                }
                texYData = mPackedY.data();
            }

            encoder->write_texture(mTexY, {}, texYData);
        }
        if (newFrameData->linesize[1]) {
            const void* texUData = newFrameData->data[1];
            int rowWidth = mTexU->get_size().x * (mPixFmt == AV_PIX_FMT_NV12 ? 2 : 1);
            int height = mTexU->get_size().y;
            if (newFrameData->linesize[1] != rowWidth) {
                mPackedU.resize(rowWidth * height);
                for (int i = 0; i < height; ++i) {
                    memcpy(mPackedU.data() + i * rowWidth,
                           newFrameData->data[1] + i * newFrameData->linesize[1],
                           rowWidth);
                }
                texUData = mPackedU.data();
            }
            encoder->write_texture(mTexU, {}, texUData);
        }
        if (newFrameData->linesize[2] && mPixFmt != AV_PIX_FMT_NV12) {
            const void* texVData = newFrameData->data[2];
            int width = mTexV->get_size().x;
            int height = mTexV->get_size().y;
            if (newFrameData->linesize[2] != width) {
                mPackedV.resize(width * height);
                for (int i = 0; i < height; ++i) {
                    memcpy(mPackedV.data() + i * width, newFrameData->data[2] + i * newFrameData->linesize[2], width);
                }
                texVData = mPackedV.data();
            }
            encoder->write_texture(mTexV, {}, texVData);
        }

        mQueue->submit(encoder, mFence);
    }
}

void YuvRenderer::render(const std::shared_ptr<Pathfinder::Texture>& outputTex) {
    if (!mTextureAllocated) {
        return;
    }
    if (mNeedClear) {
        mNeedClear = false;
        return;
    }

    auto encoder = mDevice->create_command_encoder("render yuv");

    // Update uniform buffers.
    FragUniformBlock uniform;
    if (mXformChanged || mPixFmtChanged) {
        uniform = {Pathfinder::Mat4::from_mat3(mXform), mPixFmt};

        // We don't need to preserve the data until the upload commands are implemented because
        // these uniform buffers are host-visible/coherent.
        encoder->write_buffer(mUniformBuffer, 0, sizeof(FragUniformBlock), &uniform);

        mXformChanged = false;
        mPixFmtChanged = false;
    }

    // Update descriptor set.
    mDescriptorSet->add_or_update({
        Pathfinder::Descriptor::sampled(1, mTexY, mSampler),
        Pathfinder::Descriptor::sampled(2, mTexU, mSampler),
        Pathfinder::Descriptor::sampled(3, mTexV, mSampler),
    });

    encoder->begin_render_pass(mRenderPass, outputTex, Pathfinder::ColorF::black());

    encoder->set_viewport({{0, 0}, outputTex->get_size()});

    encoder->bind_render_pipeline(mPipeline);

    encoder->bind_vertex_buffers({{mVertexBuffer, 0}});

    encoder->bind_descriptor_set(mDescriptorSet);

    encoder->draw(0, 6);

    encoder->end_render_pass();

    mQueue->submit(encoder, mFence);
}

void YuvRenderer::clear() {
    mNeedClear = true;
}
