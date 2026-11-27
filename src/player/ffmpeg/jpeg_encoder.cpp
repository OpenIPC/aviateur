#include "jpeg_encoder.h"

#include <memory>

#include "src/gui_interface.h"

inline bool convertToYUV420P(const std::shared_ptr<AVFrame> &frame, std::shared_ptr<AVFrame> &yuvFrame) {
    const int width = frame->width;
    const int height = frame->height;

    // Allocate YUV frame
    yuvFrame = std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *f) { av_frame_free(&f); });
    if (!yuvFrame) {
        return false;
    }
    yuvFrame->format = AV_PIX_FMT_YUV420P;
    yuvFrame->color_range = AVCOL_RANGE_JPEG;
    yuvFrame->width = width;
    yuvFrame->height = height;

    // Allocate buffer for YUV frame
    int ret = av_frame_get_buffer(yuvFrame.get(), 32);
    if (ret < 0) {
        return false;
    }

    // Convert RGB to YUV420P
    SwsContext *swsCtx = sws_getContext(width,
                                        height,
                                        static_cast<AVPixelFormat>(frame->format),
                                        width,
                                        height,
                                        AV_PIX_FMT_YUV420P,
                                        SWS_BILINEAR,
                                        nullptr,
                                        nullptr,
                                        nullptr);
    if (!swsCtx) {
        return false;
    }

    const int *coeffs = sws_getCoefficients(SWS_CS_DEFAULT);

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    if (!desc) {
        sws_freeContext(swsCtx);
        return false;
    }

    const bool srcFullRange = (desc->flags & AV_PIX_FMT_FLAG_RGB) || frame->color_range == AVCOL_RANGE_JPEG;

    ret = sws_setColorspaceDetails(swsCtx, coeffs, srcFullRange ? 1 : 0, coeffs, 1, 0, 1 << 16, 1 << 16);
    if (ret < 0) {
        sws_freeContext(swsCtx);
        return false;
    }

    // Perform RGB to YUV conversion
    ret = sws_scale(swsCtx, frame->data, frame->linesize, 0, height, yuvFrame->data, yuvFrame->linesize);
    if (ret <= 0) {
        sws_freeContext(swsCtx);
        return false;
    }

    // Cleanup
    sws_freeContext(swsCtx);

    return true;
}

bool JpegEncoder::encodeJpeg(const std::string &outFilePath, const std::shared_ptr<AVFrame> &frame) {
    if (!(frame && frame->format >= 0 && frame->height && frame->width && frame->linesize[0])) {
        return false;
    }

    AVFormatContext *ctx = nullptr;

    int ret = avformat_alloc_output_context2(&ctx, nullptr, "image2", outFilePath.c_str());
    if (ret < 0 || !ctx) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to create JPEG output context");
        return false;
    }

    auto pFormatCtx = std::shared_ptr<AVFormatContext>(ctx, &avformat_free_context);

    if (avio_open(&pFormatCtx->pb, outFilePath.c_str(), AVIO_FLAG_WRITE) < 0) {
        return false;
    }

    AVStream *pAVStream = avformat_new_stream(pFormatCtx.get(), nullptr);
    if (pAVStream == nullptr) {
        avio_closep(&pFormatCtx->pb);
        return false;
    }

    const AVCodec *pCodec = avcodec_find_encoder(pFormatCtx->oformat->video_codec);
    if (!pCodec) {
        avio_closep(&pFormatCtx->pb);
        return false;
    }

    auto codecCtx = std::shared_ptr<AVCodecContext>(avcodec_alloc_context3(pCodec),
                                                    [](AVCodecContext *ctx) { avcodec_free_context(&ctx); });
    codecCtx->codec_id = pFormatCtx->oformat->video_codec;
    codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    codecCtx->pix_fmt = static_cast<AVPixelFormat>(frame->format);
    codecCtx->width = frame->width;
    codecCtx->height = frame->height;
    codecCtx->time_base = AVRational{1, 25};

    std::shared_ptr<AVFrame> yuvFrame;
    const bool isYuv420pFullRange = frame->format == AV_PIX_FMT_YUV420P && frame->color_range == AVCOL_RANGE_JPEG;

    // Convert frame to YUV420P if it's not already in that format
    if (isYuv420pFullRange) {
        yuvFrame = frame;
    } else {
        if (!convertToYUV420P(frame, yuvFrame)) {
            avio_closep(&pFormatCtx->pb);
            return false;
        }
    }

    codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    codecCtx->color_range = AVCOL_RANGE_JPEG;

    if (avcodec_open2(codecCtx.get(), pCodec, nullptr) < 0) {
        avio_closep(&pFormatCtx->pb);
        return false;
    }

    avcodec_parameters_from_context(pAVStream->codecpar, codecCtx.get());

    ret = avformat_write_header(pFormatCtx.get(), nullptr);
    if (ret < 0) {
        avio_closep(&pFormatCtx->pb);
        return false;
    }

    // Resize packet
    auto pkt = std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket *pkt) { av_packet_free(&pkt); });

    ret = avcodec_send_frame(codecCtx.get(), yuvFrame.get());
    if (ret < 0) {
        avio_closep(&pFormatCtx->pb);
        return false;
    }

    ret = avcodec_receive_packet(codecCtx.get(), pkt.get());

    if (ret < 0) {
        avio_closep(&pFormatCtx->pb);
        return false;
    }

    ret = av_write_frame(pFormatCtx.get(), pkt.get());

    if (ret < 0) {
        avio_closep(&pFormatCtx->pb);
        return false;
    }

    ret = av_write_trailer(pFormatCtx.get());

    if (ret < 0) {
        avio_closep(&pFormatCtx->pb);
        return false;
    }

    avio_closep(&pFormatCtx->pb);

    return true;
}
