/*
 * Copyright 2018 The Cartographer Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CARTOGRAPHER_MAPPING_INTERNAL_OPTIMIZATION_COST_FUNCTIONS_LANDMARK_COST_FUNCTION_2D_H_
#define CARTOGRAPHER_MAPPING_INTERNAL_OPTIMIZATION_COST_FUNCTIONS_LANDMARK_COST_FUNCTION_2D_H_

#include "Eigen/Core"
#include "Eigen/Geometry"
#include "cartographer/mapping/internal/optimization/cost_functions/cost_helpers.h"
#include "cartographer/mapping/internal/optimization/optimization_problem_2d.h"
#include "cartographer/mapping/pose_graph_interface.h"
#include "cartographer/transform/rigid_transform.h"
#include "cartographer/transform/transform.h"
#include "ceres/ceres.h"
#include "ceres/jet.h"

namespace cartographer {
namespace mapping {
namespace optimization {

// Cost function measuring the weighted error between the observed pose given by
// the landmark measurement and the linearly interpolated pose of embedded in 3D
// space node poses.
class LandmarkCostFunction2D {
 public:
  using LandmarkObservation =
      PoseGraphInterface::LandmarkNode::LandmarkObservation;

  static ceres::CostFunction* CreateAutoDiffCostFunction(
      const LandmarkObservation& observation, const NodeSpec2D& prev_node,
      const NodeSpec2D& next_node) {
    return new ceres::AutoDiffCostFunction<
        LandmarkCostFunction2D, 6 /* residuals */,
        3 /* previous node pose variables */, 3 /* next node pose variables */,
        4 /* landmark rotation variables */,
        3 /* landmark translation variables */>(
        new LandmarkCostFunction2D(observation, prev_node, next_node));
  }

  template <typename T>
  bool operator()(const T* const prev_node_pose, const T* const next_node_pose,
                  const T* const landmark_rotation,
                  const T* const landmark_translation, T* const e) const {
    const std::tuple<std::array<T, 4>, std::array<T, 3>>
        interpolated_rotation_and_translation = InterpolateNodes2D(
            prev_node_pose, prev_node_gravity_alignment_, next_node_pose,
            next_node_gravity_alignment_, interpolation_parameter_);
    std::array<T, 6> error =
        observed_from_tracking_
            ? ComputeUnscaledError(
                  landmark_to_tracking_transform_,
                  std::get<0>(interpolated_rotation_and_translation).data(),
                  std::get<1>(interpolated_rotation_and_translation).data(),
                  landmark_rotation, landmark_translation)
            : ComputeUnscaledError(
                  landmark_to_tracking_transform_, landmark_rotation,
                  landmark_translation,
                  std::get<0>(interpolated_rotation_and_translation).data(),
                  std::get<1>(interpolated_rotation_and_translation).data());

    // Rotate unscaled error to ENU frame
    const auto interpolated_rotation =
        std::get<0>(interpolated_rotation_and_translation).data();
    const Eigen::Quaternion<T> interpolated_quaternion(
        interpolated_rotation[0], interpolated_rotation[1],
        interpolated_rotation[2], interpolated_rotation[3]);
    Eigen::Matrix<T, 3, 1> translation_error{error[0], error[1], error[2]};
    translation_error = (interpolated_quaternion * translation_error).eval();

    // Assign rotated translation_error to the error array
    error[0] = translation_error[0];
    error[1] = translation_error[1];
    error[2] = translation_error[2];

    // Finally, scale the error
    error = ScaleErrorWithCovariance(error, translation_weight_,
                                     rotation_weight_, inverse_covariance_);

    std::copy(std::begin(error), std::end(error), e);
    return true;
  }

 private:
  LandmarkCostFunction2D(const LandmarkObservation& observation,
                         const NodeSpec2D& prev_node,
                         const NodeSpec2D& next_node)
      : landmark_to_tracking_transform_(
            observation.landmark_to_tracking_transform),
        prev_node_gravity_alignment_(prev_node.gravity_alignment),
        next_node_gravity_alignment_(next_node.gravity_alignment),
        translation_weight_(observation.translation_weight),
        rotation_weight_(observation.rotation_weight),
        interpolation_parameter_(
            common::ToSeconds(observation.time - prev_node.time) /
            common::ToSeconds(next_node.time - prev_node.time)),
        observed_from_tracking_(observation.observed_from_tracking),
        inverse_covariance_(observation.inverse_covariance) {}

  const transform::Rigid3d landmark_to_tracking_transform_;
  const Eigen::Quaterniond prev_node_gravity_alignment_;
  const Eigen::Quaterniond next_node_gravity_alignment_;
  const double translation_weight_;
  const double rotation_weight_;
  const double interpolation_parameter_;
  const bool observed_from_tracking_;
  const std::array<double, 9UL> inverse_covariance_;
};

}  // namespace optimization
}  // namespace mapping
}  // namespace cartographer

#endif  // CARTOGRAPHER_MAPPING_INTERNAL_OPTIMIZATION_COST_FUNCTIONS_LANDMARK_COST_FUNCTION_2D_H_
