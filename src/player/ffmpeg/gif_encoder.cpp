#include "gif_encoder.h"

#include <chrono>

bool GifEncoder::open(int width, int height, AVPixelFormat pixelFormat, int frameRate, const std::string &outputPath) {
    _formatCtx = std::shared_ptr<AVFormatContext>(avformat_alloc_context(), &avformat_free_context);

    _formatCtx->oformat = av_guess_format("gif", nullptr, nullptr);

    AVStream *pAVStream = avformat_new_stream(_formatCtx.get(), nullptr);
    if (pAVStream == nullptr) {
        return false;
    }

    const AVCodec *pCodec = avcodec_find_encoder(_formatCtx->oformat->video_codec);
    if (!pCodec) {
        return false;
    }

    _codecCtx = std::shared_ptr<AVCodecContext>(avcodec_alloc_context3(pCodec),
                                                [](AVCodecContext *ctx) { avcodec_free_context(&ctx); });
    _frameRate = frameRate;
    _codecCtx->codec_id = _formatCtx->oformat->video_codec;
    _codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    _codecCtx->pix_fmt = AV_PIX_FMT_RGB8;
    _codecCtx->width = 640;
    _codecCtx->height = (int)(640.0 * height / width);
    _codecCtx->time_base = AVRational{1, frameRate};

    // 根据需要创建颜色空间转换器
    if (_codecCtx->pix_fmt != pixelFormat) {
        // 颜色转换器
        _imgConvertCtx = sws_getCachedContext(_imgConvertCtx,
                                              width,
                                              height,
                                              pixelFormat,
                                              _codecCtx->width,
                                              _codecCtx->height,
                                              _codecCtx->pix_fmt,
                                              SWS_BICUBIC,
                                              nullptr,
                                              nullptr,
                                              nullptr);
        if (!_imgConvertCtx) {
            return false;
        }
    }

    if (avcodec_open2(_codecCtx.get(), pCodec, nullptr) < 0) {
        return false;
    }

    avcodec_parameters_from_context(pAVStream->codecpar, _codecCtx.get());

    if (avformat_write_header(_formatCtx.get(), nullptr) < 0) {
        return false;
    }

    if (avio_open(&_formatCtx->pb, outputPath.c_str(), AVIO_FLAG_READ_WRITE) < 0) {
        return false;
    }
    _opened = true;

    _saveFilePath = outputPath;

    return true;
}

bool GifEncoder::encodeFrame(const std::shared_ptr<AVFrame> &frame) {
    if (!_opened) {
        return false;
    }

    std::lock_guard lck(_encodeMtx);

    // Convert format.
    if (_codecCtx->pix_fmt != frame->format) {
        // Allocate a temporary frame.
        if (!_tmpFrame) {
            _tmpFrame = std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *f) { av_frame_free(&f); });
            if (!_tmpFrame) {
                return false;
            }
            _tmpFrame->width = _codecCtx->width;
            _tmpFrame->height = _codecCtx->height;
            _tmpFrame->format = _codecCtx->pix_fmt;
            int size = av_image_get_buffer_size(_codecCtx->pix_fmt, _codecCtx->width, _codecCtx->height, 1);
            _buff.resize(size);
            int ret = av_image_fill_arrays(_tmpFrame->data,
                                           _tmpFrame->linesize,
                                           _buff.data(),
                                           _codecCtx->pix_fmt,
                                           _codecCtx->width,
                                           _codecCtx->width,
                                           1);
            if (ret < 0) {
                return false;
            }
        }
        // 转换为GIF编码需要的颜色和高度
        int h = sws_scale(_imgConvertCtx,
                          frame->data,
                          frame->linesize,
                          0,
                          frame->height,
                          _tmpFrame->data,
                          _tmpFrame->linesize);
        if (h != _codecCtx->height) {
            return false;
        }
    }

    // Packet size.
    int size = _codecCtx->width * _codecCtx->height;

    // Allocate a packet.

    std::shared_ptr<AVPacket> pkt =
        std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket *pkt) { av_packet_free(&pkt); });
    av_new_packet(pkt.get(), size);

    // 记录帧编码时间
    _lastEncodeTime =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    // 发送帧到编码上下文
    int ret = avcodec_send_frame(_codecCtx.get(), _tmpFrame.get());
    if (ret < 0) {
        return false;
    }

    // 获取已经编码完成的帧
    avcodec_receive_packet(_codecCtx.get(), pkt.get());

    // 写文件
    av_write_frame(_formatCtx.get(), pkt.get());

    return true;
}

std::string GifEncoder::close() {
    std::lock_guard lck(_encodeMtx);

    if (!_opened) {
        return "";
    }

    if (_formatCtx) {
        av_write_trailer(_formatCtx.get());
    }

    if (_codecCtx) {
        _codecCtx.reset();
    }

    // Close file.
    avio_close(_formatCtx->pb);
    _opened = false;

    return _saveFilePath;
}

GifEncoder::~GifEncoder() {
    close();
}

bool GifEncoder::isOpened() {
    std::lock_guard lck(_encodeMtx);
    return _opened;
}
