//
// Created by liangzhuohua on 2022/2/28.
//

#pragma once

#include <memory>
#include <string>

#include "ffmpeg_include.h"

class JpegEncoder {
public:
    static bool encodeJpeg(const std::string &outFilePath, const std::shared_ptr<AVFrame> &frame);
};
