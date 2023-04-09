#include "frontend.h"
#include "slam_memory.h"
#include "log_api.h"

// Debug.
#include "opencv2/opencv.hpp"

namespace VISUAL_FRONTEND {

// Debug.
void DrawImageWithFeatures(const std::string title, const Image &image, const std::vector<Vec2> &features) {
    cv::Mat cv_cur_image(image.rows(), image.cols(), CV_8UC1, image.data());

    cv::Mat show_cur_image(cv_cur_image.rows, cv_cur_image.cols, CV_8UC3);
    cv::cvtColor(cv_cur_image, show_cur_image, cv::COLOR_GRAY2BGR);
    for (unsigned long i = 0; i < features.size(); i++) {
        cv::circle(show_cur_image, cv::Point2f(features[i].x(), features[i].y()), 2, cv::Scalar(0, 0, 255), 3);
    }
    cv::imshow(title, show_cur_image);
    cv::waitKey(1);
}

Frontend::Frontend(const uint32_t image_rows, const uint32_t image_cols) {
    const uint32_t size = image_rows * image_cols;
    stored_buff_ = (uint8_t *)SlamMemory::Malloc(sizeof(uint8_t) * size * 8);

    // Allocate memory for stored buffer.
    uint8_t *buf = stored_buff_;
    for (uint32_t i = 0; i < 4; ++i) {
        stored_pyramids_[i].SetRawImage(buf, image_rows, image_cols);
        buf += size;
        stored_pyramids_[i].SetPyramidBuff(buf);
        buf += size;
    }

}

Frontend::~Frontend() {
    SlamMemory::Free(stored_buff_);
}

bool Frontend::RunOnce(const Image &cur_image) {
    if (cur_image.data() == nullptr) {
        return false;
    }

    if (camera_model_ == nullptr) {
        LogError("Camera model is nullptr, please set it to be pinhole or fisheye.");
        return false;
    }

    // Image process.
    std::copy_n(cur_image.data(), cur_image.rows() * cur_image.cols(), cur_pyramid_left_->GetImage(0).data());
    cur_pyramid_left_->CreateImagePyramid(4);

    // Run klt if ref frame is ok.
    if (ref_points_->size() != 0) {
        // Track features from ref pyramid to cur pyramid.
        *cur_points_ = *ref_points_;
        *cur_ids_ = *ref_ids_;
        tracked_status_.clear();
        feature_tracker_->TrackMultipleLevel(*ref_pyramid_left_, *cur_pyramid_left_, *ref_points_, *cur_points_, tracked_status_);

        // Reject outliers by essential/fundemantal matrix.
        cur_norm_xy_->resize(cur_points_->size());
        for (uint32_t i = 0; i < cur_points_->size(); ++i) {
            camera_model_->LiftToNormalizedPlaneAndUndistort((*cur_points_)[i], (*cur_norm_xy_)[i]);
        }
        Mat3 essential;
        epipolar_solver_->EstimateEssential(*ref_norm_xy_, *cur_norm_xy_, essential, tracked_status_);

        // Adjust result.
        AdjustVectorByStatus(tracked_status_, *cur_points_);
        AdjustVectorByStatus(tracked_status_, *cur_ids_);
        AdjustVectorByStatus(tracked_status_, *cur_norm_xy_);
        tracked_status_.resize(cur_points_->size(), static_cast<uint8_t>(OPTICAL_FLOW::TrackStatus::TRACKED));
    }

    // Check if cur_pyarmid should be keyframe.
    if (cur_points_->size() < options_.kMinDetectedFeaturePointsNumberInCurrentImage) {
        // Cur should be keyframe.
        is_cur_image_keyframe_ = true;

        // Detect new features in cur.
        feature_detector_->DetectGoodFeatures(cur_pyramid_left_->GetImage(0),
                                             options_.kMaxStoredFeaturePointsNumber,
                                             *cur_points_);
        const uint32_t new_features_num = cur_points_->size() - cur_ids_->size();
        for (uint32_t i = 0; i < new_features_num; ++i) {
            cur_ids_->emplace_back(feature_id_cnt_);
            ++feature_id_cnt_;
        }

        // Replace ref with cur.
        ExchangePointer(&ref_pyramid_left_, &cur_pyramid_left_);
        ExchangePointer(&ref_points_, &cur_points_);
        ExchangePointer(&ref_ids_, &cur_ids_);
        ExchangePointer(&ref_norm_xy_, &cur_norm_xy_);
    } else {
        // Maintain ref to be keyframe.
        is_cur_image_keyframe_ = false;
    }

    // Prepare for next step.

    // Draw result to debug.
    if (is_cur_image_keyframe_) {
        DrawImageWithFeatures("ref", ref_pyramid_left_->GetImage(0), *ref_points_);
        DrawImageWithFeatures("cur", ref_pyramid_left_->GetImage(0), *ref_points_);
    } else {
        DrawImageWithFeatures("cur", cur_pyramid_left_->GetImage(0), *cur_points_);
    }

    return true;
}

bool Frontend::RunOnce(const Image &image_left, const Image &image_right) {
    if (image_left.data() == nullptr || image_right.data() == nullptr) {
        return false;
    }

    if (camera_model_ == nullptr) {
        LogError("Camera model is nullptr, please set it to be pinhole or fisheye.");
        return false;
    }

    // TODO:

    return true;
}

template<typename T>
void Frontend::ExchangePointer(T **ptr1, T** ptr2) {
    T *ptr_tmp = *ptr1;
    *ptr1 = *ptr2;
    *ptr2 = ptr_tmp;
}

template<typename T, typename StatusType>
void Frontend::AdjustVectorByStatus(const std::vector<StatusType> &status,
                                    std::vector<T> &v)  {
    uint32_t j = 0;
    for (uint32_t i = 0; i < status.size(); ++i) {
        if (status[i] == static_cast<StatusType>(1)) {
            v[j] = v[i];
            ++j;
        }
    }
    v.resize(j);
}

}
