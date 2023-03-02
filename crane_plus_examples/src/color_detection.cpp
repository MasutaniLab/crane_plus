// Copyright 2023 RT Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Reference:
// https://www.opencv-srf.com/2010/09/object-detection-using-color-seperation.html

#include <cmath>
#include <iostream>
#include <iomanip>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "opencv2/opencv.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "cv_bridge/cv_bridge.h"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2/LinearMath/Matrix3x3.h"
#include "tf2_ros/transform_broadcaster.h"
#include "image_geometry/pinhole_camera_model.h"
using std::placeholders::_1;

class ImageSubscriber : public rclcpp::Node
{
public:
  ImageSubscriber()
  : Node("color_detection")
  {
    image_subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
      "image_raw", 10, std::bind(&ImageSubscriber::image_callback, this, _1));

    camera_info_subscription_ = this->create_subscription<sensor_msgs::msg::CameraInfo>(
      "camera_info", 10, std::bind(&ImageSubscriber::camera_info_callback, this, _1));

    image_thresholded_publisher_ =
      this->create_publisher<sensor_msgs::msg::Image>("image_thresholded", 10);

    tf_broadcaster_ =
      std::make_unique<tf2_ros::TransformBroadcaster>(*this);
  }

private:
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr camera_info_subscription_;
  sensor_msgs::msg::CameraInfo::SharedPtr camera_info_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_thresholded_publisher_;
  std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;

  void image_callback(const sensor_msgs::msg::Image::SharedPtr msg)
  {
    // カメラのパラメータを取得してから処理を行う
    if (camera_info_) {
      // 赤い物体を認識するようにHSVの範囲を設定
      // 周囲の明るさ等の動作環境に合わせて調整
      const int low_h_1 = 0, high_h_1 = 10;
      const int low_h_2 = 170, high_h_2 = 179;
      const int low_s = 100, high_s = 255;
      const int low_v = 50, high_v = 255;

      // ウェブカメラの画像を受け取る
      auto cv_img = cv_bridge::toCvShare(msg, msg->encoding);

      // 画像をRGBからHSVに変換
      cv::cvtColor(cv_img->image, cv_img->image, cv::COLOR_RGB2HSV);

      // 画像処理用の変数を用意
      cv::Mat img_mask_1;
      cv::Mat img_mask_2;
      cv::Mat img_thresholded;

      // 画像の二値化
      cv::inRange(
        cv_img->image,
        cv::Scalar(low_h_1, low_s, low_v),
        cv::Scalar(high_h_1, high_s, high_v),
        img_mask_1);
      cv::inRange(
        cv_img->image,
        cv::Scalar(low_h_2, low_s, low_v),
        cv::Scalar(high_h_2, high_s, high_v),
        img_mask_2);

      // マスク画像の合成
      img_thresholded = img_mask_1 | img_mask_2;

      // ノイズ除去の処理
      cv::morphologyEx(
        img_thresholded,
        img_thresholded,
        cv::MORPH_OPEN,
        cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));

      // 穴埋めの処理
      cv::morphologyEx(
        img_thresholded,
        img_thresholded,
        cv::MORPH_CLOSE,
        cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));

      // 画像の検出領域におけるモーメントを計算
      cv::Moments moment = moments(img_thresholded);
      double d_m01 = moment.m01;
      double d_m10 = moment.m10;
      double d_area = moment.m00;

      // 検出した領域のピクセル数が10000より大きい場合
      if (d_area > 10000) {
        // カメラモデル作成
        image_geometry::PinholeCameraModel camera_model;

        // カメラのパラメータを設定
        camera_model.fromCameraInfo(camera_info_);

        // 画像座標系における把持対象物の位置（2D）
        const double pixel_x = d_m10 / d_area;
        const double pixel_y = d_m01 / d_area;
        const cv::Point2d point(pixel_x, pixel_y);

        // 補正後の画像座標系における把持対象物の位置を取得（2D）
        const cv::Point2d rect_point = camera_model.rectifyPoint(point);

        // カメラ座標系から見た把持対象物の方向（Ray）を取得する
        const cv::Point3d ray = camera_model.projectPixelTo3dRay(rect_point);

        // カメラの高さを0.44[m]として把持対象物の位置を計算
        const double camera_height = 0.44;
        cv::Point3d ray_after(ray.x * camera_height, ray.y * camera_height, ray.z * camera_height);

        // 把持対象物の位置をTFに配信
        geometry_msgs::msg::TransformStamped t;
        t.header.stamp = this->get_clock()->now();
        t.header.frame_id = "camera_color_optical_frame";
        t.child_frame_id = "target_0";
        t.transform.translation.x = ray_after.x;
        t.transform.translation.y = ray_after.y;
        t.transform.translation.z = ray_after.z;
        tf_broadcaster_->sendTransform(t);

        // 閾値の画像を配信
        cv_bridge::CvImage cv_image;
        cv_image.encoding = "mono8";
        cv_image.header.stamp = this->now();
        cv_image.header.frame_id = "image_thresholded_frame";
        img_thresholded.copyTo(cv_image.image);
        sensor_msgs::msg::Image ros_image;
        cv_image.toImageMsg(ros_image);
        image_thresholded_publisher_->publish(std::move(ros_image));
      }
    }
  }

  void camera_info_callback(const sensor_msgs::msg::CameraInfo::SharedPtr msg)
  {
    camera_info_ = msg;
  }
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ImageSubscriber>());
  rclcpp::shutdown();
  return 0;
}
