/*
Thanks to Nghia Ho for his excellent code.
And,I modified the smooth step using a simple kalman filter .
So,It can processes live video streaming.
modified by chen jia.
email:chenjia2013@foxmail.com
*/

#include "video_stabilizer.h"

#include <common/utils.h>

#include <cassert>
#include <cmath>
#include <opencv2/core/ocl.hpp>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

// This video stabilization smooths the global trajectory using a sliding average window

// 1. Get previous to current frame transformation (dx, dy, da) for all frames
// 2. Accumulate the transformations to get the image trajectory
// 3. Smooth out the trajectory using an averaging window
// 4. Generate new set of previous to current transform, such that the trajectory ends up being the same as the smoothed
// trajectory
// 5. Apply the new transformation to the video

double pstd = 4e-3;             // can be changed
double cstd = 0.25;             // can be changed
Trajectory Q(pstd, pstd, pstd); // process noise covariance
Trajectory R(cstd, cstd, cstd); // measurement noise covariance

VideoStabilizer::VideoStabilizer() {
    if (cv::ocl::haveOpenCL()) {
        std::cout << "OpenCL is available and enabled." << std::endl;
        // Optionally, ensure it's set to be used
        cv::ocl::setUseOpenCL(true);
    } else {
        std::cout << "OpenCL is not available." << std::endl;
        cv::ocl::setUseOpenCL(false);
    }
}

cv::Mat VideoStabilizer::stabilize(cv::Mat prev, cv::Mat cur_grey) {
    auto timestamp = revector::Timestamp("Aviateur");

    prev_grey = prev;

    Mat xform = Mat::zeros(2, 3, CV_64F);
    xform.at<double>(0, 0) = 1;
    xform.at<double>(1, 1) = 1;

    // Get features from the previous frame.
    vector<Point2f> prev_corners;

    if (1) {
        goodFeaturesToTrack(prev_grey, prev_corners, 200, 0.01, 30);
        if (prev_corners.empty()) {
            return xform;
        }
    } else {
        std::vector<KeyPoint> key_points;
        FAST(prev_grey, key_points, 10);
        prev_corners.resize(key_points.size());
        for (int i = 0; i < prev_corners.size(); i++) {
            prev_corners[i] = Point2f(key_points[i].pt.x, key_points[i].pt.y);
        }
    }

    timestamp.record("goodFeaturesToTrack");

    vector<uchar> status;
    vector<float> err;
    vector<Point2f> cur_corners;
    calcOpticalFlowPyrLK(prev_grey, cur_grey, prev_corners, cur_corners, status, err);

    timestamp.record("calcOpticalFlowPyrLK");

    vector<Point2f> prev_corners2, cur_corners2;
    prev_corners2.reserve(prev_corners.size());
    cur_corners2.reserve(cur_corners.size());

    // Weed out bad matches
    for (size_t i = 0; i < status.size(); i++) {
        if (status[i]) {
            prev_corners2.push_back(prev_corners[i]);
            cur_corners2.push_back(cur_corners[i]);
        }
    }

    // Step 1 - Get previous to current frame transformation
    // Rigid transform, translation + rotation only, no scaling/shearing.
    xform = estimateAffinePartial2D(prev_corners2, cur_corners2);

    timestamp.record("estimateAffinePartial2D");
#ifndef NDEBUG
    timestamp.print();
#endif

    // In rare cases no transform is found. We'll just use the last known good transform.
    if (xform.data == nullptr) {
        last_xform.copyTo(xform);
    }

    xform.copyTo(last_xform);

    // Decompose transform
    double dx = xform.at<double>(0, 2);
    double dy = xform.at<double>(1, 2);
    double da = atan2(xform.at<double>(1, 0), xform.at<double>(0, 0));

    if (k == 1) {
        // Initial guesses
        X = Trajectory(0, 0, 0); // Initial estimate, set 0
        P = Trajectory(1, 1, 1); // Error variance, set 1

        x = dx;
        y = dy;
        a = da;

// Reset debug data files
#ifndef NDEBUG
        out_trajectory = std::ofstream("trajectory.txt");
        out_smoothed_trajectory = std::ofstream("smoothed_trajectory.txt");
#endif
    } else {
        // Accumulated frame to frame transform
        x += dx;
        y += dy;
        a += da;

        // Actual measurement
        auto z = Trajectory(x, y, a);

        Trajectory X_; // priori estimate
        Trajectory P_; // priori estimate error covariance

        // Time update (prediction)
        X_ = X;     // X_(k) = X(k-1);
        P_ = P + Q; // P_(k) = P(k-1)+Q;

        // Measurement update (correction)
        Trajectory K = P_ / (P_ + R);       // gain, K(k) = P_(k)/( P_(k)+R )
        X = X_ + K * (z - X_);              // z-X_ is residual, X(k) = X_(k)+K(k)*(z(k)-X_(k))
        P = (Trajectory(1, 1, 1) - K) * P_; // P(k) = (1-K(k))*P_(k);
    }

#ifndef NDEBUG
    out_trajectory << k << " " << x << " " << y << " " << a << endl;
    out_smoothed_trajectory << k << " " << X.x << " " << X.y << " " << X.a << endl;
#endif

    // Target - current
    double diff_x = X.x - x;
    double diff_y = X.y - y;
    double diff_a = X.a - a;

    dx += diff_x;
    dy += diff_y;
    da += diff_a;

    xform.at<double>(0, 0) = cos(da);
    xform.at<double>(0, 1) = -sin(da);
    xform.at<double>(1, 0) = sin(da);
    xform.at<double>(1, 1) = cos(da);

    xform.at<double>(0, 2) = dx;
    xform.at<double>(1, 2) = dy;

    cur_grey.copyTo(prev_grey);

    k++;

    return xform;
}
