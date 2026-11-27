#include "mp4_encoder.h"

#include "src/gui_interface.h"

Mp4Encoder::Mp4Encoder(const std::string &saveFilePath) {
    formatCtx_ = std::shared_ptr<AVFormatContext>(avformat_alloc_context(), &avformat_free_context);

    formatCtx_->oformat = av_guess_format("mp4", saveFilePath.c_str(), nullptr);
    if (!formatCtx_->oformat) {
        formatCtx_->oformat = av_guess_format("mov", nullptr, nullptr);
    }

    saveFilePath_ = saveFilePath;
}

Mp4Encoder::~Mp4Encoder() {
    if (isOpen_) {
        stop();
    }
}

void Mp4Encoder::addTrack(AVStream *stream, const AVCodecContext *codecCtx) {
    if (!stream || !stream->codecpar) {
        return;
    }
    if (stream->time_base.num <= 0 || stream->time_base.den <= 0) {
        GuiInterface::Instance().PutLog(LogLevel::Warn, "Mp4Encoder::addTrack failed: invalid time_base");
        return;
    }

    std::lock_guard<std::mutex> lock(writeMtx_);
    if (isOpen_) {
        return;
    }

    // Prevent duplicate tracks
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioIndex >= 0) {
        return;
    }
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoIndex >= 0) {
        return;
    }

    AVStream *os = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!os) {
        return;
    }
    int ret = avcodec_parameters_copy(os->codecpar, stream->codecpar);
    if (ret < 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "avcodec_parameters_copy failed: {}", ret);
        return;
    }

    // Copy extradata from codec context if missing from stream codecpar (critical for QuickTime Player MP4 metadata)
    if (codecCtx && codecCtx->extradata && codecCtx->extradata_size > 0 && os->codecpar->extradata_size == 0) {
        os->codecpar->extradata =
            static_cast<uint8_t *>(av_mallocz(codecCtx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
        if (os->codecpar->extradata) {
            memcpy(os->codecpar->extradata, codecCtx->extradata, codecCtx->extradata_size);
            os->codecpar->extradata_size = codecCtx->extradata_size;
        }
    }

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        audioIndex = os->index;
        originAudioTimeBase_ = stream->time_base;
        os->codecpar->codec_tag = 0;
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        videoIndex = os->index;
        originVideoTimeBase_ = stream->time_base;

        // Ensure video dimensions are populated (RTP stream codecpar often starts with width/height == 0)
        if (os->codecpar->width <= 0 || os->codecpar->height <= 0) {
            if (codecCtx && codecCtx->width > 0 && codecCtx->height > 0) {
                os->codecpar->width = codecCtx->width;
                os->codecpar->height = codecCtx->height;
            }
        }

        // macOS QuickTime Player strictly requires 'hvc1' fourcc tag for HEVC streams (instead of 'hev1')
        if (stream->codecpar->codec_id == AV_CODEC_ID_HEVC) {
            os->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
        } else {
            os->codecpar->codec_tag = 0;
        }
    }
}

bool Mp4Encoder::start() {
    std::lock_guard<std::mutex> lock(writeMtx_);
    if (isOpen_ || !formatCtx_ || !formatCtx_->oformat) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Mp4Encoder::start failed: invalid state or context");
        return false;
    }

    if (int err = avio_open(&formatCtx_->pb, saveFilePath_.c_str(), AVIO_FLAG_WRITE); err < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(err, errbuf, sizeof(errbuf));
        GuiInterface::Instance().PutLog(LogLevel::Error, "avio_open failed for {}: {}", saveFilePath_, errbuf);
        return false;
    }

    // Write output stream header information
    AVDictionary *opts = nullptr;
    // Fragmented MP4 flags for maximum resilience against crashes + QuickTime compatibility (write_colr)
    av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof+write_colr", 0);
    int ret = avformat_write_header(formatCtx_.get(), &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        GuiInterface::Instance().PutLog(LogLevel::Error, "avformat_write_header failed: {}", errbuf);
        if (formatCtx_->pb) {
            avio_closep(&formatCtx_->pb);
        }
        return false;
    }
    isOpen_ = true;
    writtenKeyFrame_ = false;
    return true;
}

void Mp4Encoder::writePacket(const std::shared_ptr<AVPacket> &pkt, bool isVideo) {
    if (!pkt) {
        return;
    }

    std::lock_guard<std::mutex> lock(writeMtx_);
    if (!isOpen_ || !formatCtx_) {
        return;
    }

    int streamIndex = isVideo ? videoIndex : audioIndex;
    if (streamIndex < 0 || streamIndex >= static_cast<int>(formatCtx_->nb_streams)) {
        return;
    }

    // Wait for the first video keyframe before writing packets
    if (videoIndex >= 0 && !writtenKeyFrame_) {
        if (!isVideo || !(pkt->flags & AV_PKT_FLAG_KEY)) {
            return;
        }
    }

    // Clone packet to isolate metadata/timestamp changes from the live player thread
    AVPacket *clonedPkt = av_packet_clone(pkt.get());
    if (!clonedPkt) {
        return;
    }

    std::shared_ptr<AVPacket> newPkt(clonedPkt, [](AVPacket *p) { av_packet_free(&p); });

    AVRational timeBase = isVideo ? originVideoTimeBase_ : originAudioTimeBase_;
    newPkt->stream_index = streamIndex;
    av_packet_rescale_ts(newPkt.get(), timeBase, formatCtx_->streams[streamIndex]->time_base);
    newPkt->pos = -1;

    // Capture keyframe state BEFORE calling av_interleaved_write_frame (which wipes newPkt properties on return)
    const bool isKeyFrame = isVideo && (newPkt->flags & AV_PKT_FLAG_KEY);

    int ret = av_interleaved_write_frame(formatCtx_.get(), newPkt.get());
    if (ret < 0) {
        char errStr[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        GuiInterface::Instance().PutLog(LogLevel::Error, "av_interleaved_write_frame failed: {}", errStr);
        return;
    }

    if (isKeyFrame) {
        writtenKeyFrame_ = true;
    }
}

void Mp4Encoder::stop() {
    std::lock_guard<std::mutex> lock(writeMtx_);
    if (!isOpen_) {
        return;
    }
    isOpen_ = false;

    if (formatCtx_) {
        int ret = av_write_trailer(formatCtx_.get());
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            GuiInterface::Instance().PutLog(LogLevel::Error, "av_write_trailer failed: {}", errbuf);
        }
        if (formatCtx_->pb) {
            avio_closep(&formatCtx_->pb);
        }
    }
}
