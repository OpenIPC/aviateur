#include "mp4_encoder.h"

Mp4Encoder::Mp4Encoder(const std::string &saveFilePath) {
    formatCtx_ = std::shared_ptr<AVFormatContext>(avformat_alloc_context(), &avformat_free_context);

    formatCtx_->oformat = av_guess_format("mov", nullptr, nullptr);

    saveFilePath_ = saveFilePath;
}

Mp4Encoder::~Mp4Encoder() {
    if (isOpen_) {
        stop();
    }
}

void Mp4Encoder::addTrack(AVStream *stream) {
    AVStream *os = avformat_new_stream(formatCtx_.get(), nullptr);
    if (!os) {
        return;
    }
    int ret = avcodec_parameters_copy(os->codecpar, stream->codecpar);
    if (ret < 0) {
        return;
    }
    os->codecpar->codec_tag = 0;
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        audioIndex = os->index;
        originAudioTimeBase_ = stream->time_base;
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        videoIndex = os->index;
        originVideoTimeBase_ = stream->time_base;
    }
}

bool Mp4Encoder::start() {
    // 初始化上下文
    if (avio_open(&formatCtx_->pb, saveFilePath_.c_str(), AVIO_FLAG_READ_WRITE) < 0) {
        return false;
    }
    // 写输出流头信息
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov", 0);
    int ret = avformat_write_header(formatCtx_.get(), &opts);
    if (ret < 0) {
        return false;
    }
    isOpen_ = true;
    return true;
}

void Mp4Encoder::writePacket(const std::shared_ptr<AVPacket> &pkt, bool isVideo) {
    if (!isOpen_) {
        return;
    }
#ifdef I_FRAME_FIRST
    // 未获取视频关键帧前先忽略音频
    if (videoIndex >= 0 && !writtenKeyFrame && !isVideo) {
        return;
    }
    // 跳过非关键帧，使关键帧前置
    if (!writtenKeyFrame && pkt->flags & AV_PKT_FLAG_KEY) {
        return;
    }
    writtenKeyFrame = true;
#endif
    if (isVideo) {
        pkt->stream_index = videoIndex;
        av_packet_rescale_ts(pkt.get(), originVideoTimeBase_, formatCtx_->streams[videoIndex]->time_base);
    } else {
        pkt->stream_index = audioIndex;
        av_packet_rescale_ts(pkt.get(), originAudioTimeBase_, formatCtx_->streams[audioIndex]->time_base);
    }
    pkt->pos = -1;
    av_write_frame(formatCtx_.get(), pkt.get());
}

void Mp4Encoder::stop() {
    isOpen_ = false;

    av_write_trailer(formatCtx_.get());

    avio_close(formatCtx_->pb);
}
