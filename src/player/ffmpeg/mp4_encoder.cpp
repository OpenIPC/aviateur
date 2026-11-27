#include "mp4_encoder.h"

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
    std::lock_guard<std::mutex> lock(writeMtx_);
    AVStream *os = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!os) {
        return;
    }
    int ret = avcodec_parameters_copy(os->codecpar, stream->codecpar);
    if (ret < 0) {
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
    if (avio_open(&formatCtx_->pb, saveFilePath_.c_str(), AVIO_FLAG_READ_WRITE) < 0) {
        return false;
    }
    // Write output stream header information
    AVDictionary *opts = nullptr;
    // Fragmented MP4 flags for maximum resilience against crashes + QuickTime compatibility (write_colr)
    av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof+write_colr", 0);
    int ret = avformat_write_header(formatCtx_.get(), &opts);
    av_dict_free(&opts);

    if (ret < 0) {
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
    if (!isOpen_) {
        return;
    }

    // Clone packet to isolate metadata/timestamp changes from the live player thread
    AVPacket *clonedPkt = av_packet_clone(pkt.get());
    if (!clonedPkt) {
        return;
    }

    std::shared_ptr<AVPacket> newPkt(clonedPkt, [](AVPacket *p) { av_packet_free(&p); });

    if (isVideo) {
        if (videoIndex < 0 || videoIndex >= static_cast<int>(formatCtx_->nb_streams)) {
            return;
        }
        newPkt->stream_index = videoIndex;
        av_packet_rescale_ts(newPkt.get(), originVideoTimeBase_, formatCtx_->streams[videoIndex]->time_base);
    } else {
        if (audioIndex < 0 || audioIndex >= static_cast<int>(formatCtx_->nb_streams)) {
            return;
        }
        newPkt->stream_index = audioIndex;
        av_packet_rescale_ts(newPkt.get(), originAudioTimeBase_, formatCtx_->streams[audioIndex]->time_base);
    }
    newPkt->pos = -1;

    int ret = av_interleaved_write_frame(formatCtx_.get(), newPkt.get());

    if (ret >= 0 && (newPkt->flags & AV_PKT_FLAG_KEY)) {
        writtenKeyFrame_ = true;
        if (formatCtx_->pb) {
            avio_flush(formatCtx_->pb);
        }
    }
}

void Mp4Encoder::stop() {
    std::lock_guard<std::mutex> lock(writeMtx_);
    if (!isOpen_) {
        return;
    }
    isOpen_ = false;

    if (formatCtx_) {
        if (writtenKeyFrame_) {
            av_write_trailer(formatCtx_.get());
        }
        if (formatCtx_->pb) {
            avio_closep(&formatCtx_->pb);
        }
    }
}
