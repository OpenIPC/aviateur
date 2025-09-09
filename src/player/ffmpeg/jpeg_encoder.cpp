#include "jpeg_encoder.h"

#include <memory>

inline bool convertToYUV420P(const std::shared_ptr<AVFrame> &frame, std::shared_ptr<AVFrame> &yuvFrame) {
    int width = frame->width;
    int height = frame->height;

    // Allocate YUV frame
    yuvFrame = std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *f) { av_frame_free(&f); });
    if (!yuvFrame) {
        return false;
    }
    yuvFrame->format = AV_PIX_FMT_YUVJ420P;
    yuvFrame->width = width;
    yuvFrame->height = height;

    // Allocate buffer for YUV frame
    int ret = av_frame_get_buffer(yuvFrame.get(), 32);
    if (ret < 0) {
        return false;
    }

    // Convert RGB to YUV420P
    struct SwsContext *sws_ctx = sws_getContext(width,
                                                height,
                                                static_cast<AVPixelFormat>(frame->format),
                                                width,
                                                height,
                                                AV_PIX_FMT_YUVJ420P,
                                                0,
                                                nullptr,
                                                nullptr,
                                                nullptr);
    if (!sws_ctx) {
        return false;
    }

    // Perform RGB to YUV conversion
    ret = sws_scale(sws_ctx, frame->data, frame->linesize, 0, height, yuvFrame->data, yuvFrame->linesize);
    if (ret <= 0) {
        sws_freeContext(sws_ctx);
        return false;
    }

    // Cleanup
    sws_freeContext(sws_ctx);

    return true;
}

bool JpegEncoder::encodeJpeg(const std::string &outFilePath, const std::shared_ptr<AVFrame> &frame) {
    if (!(frame && frame->height && frame->width && frame->linesize[0])) {
        return false;
    }

    auto pFormatCtx = std::shared_ptr<AVFormatContext>(avformat_alloc_context(), &avformat_free_context);

    pFormatCtx->oformat = av_guess_format("mjpeg", nullptr, nullptr);

    if (avio_open(&pFormatCtx->pb, outFilePath.c_str(), AVIO_FLAG_READ_WRITE) < 0) {
        return false;
    }

    AVStream *pAVStream = avformat_new_stream(pFormatCtx.get(), nullptr);
    if (pAVStream == nullptr) {
        return false;
    }

    const AVCodec *pCodec = avcodec_find_encoder(pFormatCtx->oformat->video_codec);
    if (!pCodec) {
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

    // Convert frame to YUV420P if it's not already in that format
    std::shared_ptr<AVFrame> yuvFrame;
    if (frame->format != AV_PIX_FMT_YUVJ420P && frame->format != AV_PIX_FMT_YUV420P) {
        if (!convertToYUV420P(frame, yuvFrame)) {
            return false;
        }
        codecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    } else {
        yuvFrame = frame; // If already YUV420P, use as is
    }

    if (avcodec_open2(codecCtx.get(), pCodec, nullptr) < 0) {
        return false;
    }

    avcodec_parameters_from_context(pAVStream->codecpar, codecCtx.get());

    int ret{};

    ret = avformat_write_header(pFormatCtx.get(), nullptr);
    if (ret < 0) {
        return false;
    }

    int y_size = codecCtx->width * codecCtx->height;

    // Resize packet
    auto pkt = std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket *pkt) { av_packet_free(&pkt); });
    av_new_packet(pkt.get(), y_size);

    ret = avcodec_send_frame(codecCtx.get(), yuvFrame.get());
    if (ret < 0) {
        return false;
    }

    avcodec_receive_packet(codecCtx.get(), pkt.get());

    av_write_frame(pFormatCtx.get(), pkt.get());

    av_write_trailer(pFormatCtx.get());

    avio_close(pFormatCtx->pb);

    return true;
}
