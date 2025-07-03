#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ffmpeg_include.h"

class GifEncoder {
public:
    ~GifEncoder();

    bool open(int width, int height, AVPixelFormat pixelFormat, int frameRate, const std::string &outputPath);

    bool encodeFrame(const std::shared_ptr<AVFrame> &frame);

    std::string close();

    int getFrameRate() const {
        return _frameRate;
    }

    uint64_t getLastEncodeTime() const {
        return _lastEncodeTime;
    }

    bool isOpened();

    std::string _saveFilePath;

protected:
    std::mutex _encodeMtx;

    std::shared_ptr<AVFormatContext> _formatCtx;

    std::shared_ptr<AVCodecContext> _codecCtx;
    // 色彩空间转换
    SwsContext *_imgConvertCtx{};
    // 颜色转换临时frame
    std::shared_ptr<AVFrame> _tmpFrame;
    std::vector<uint8_t> _buff;

    uint64_t _lastEncodeTime = 0;

    int _frameRate = 0;

    volatile bool _opened = false;
};
