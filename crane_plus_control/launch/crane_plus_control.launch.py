# Copyright 2020 RT Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import ExecuteProcess
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    declare_loaded_description = DeclareLaunchArgument(
        'loaded_description',
        default_value='',
        description='Set robot_description text.  \
                     It is recommended to use RobotDescriptionLoader() in crane_plus_description.'
    )

    crane_plus_controllers = os.path.join(
        get_package_share_directory('crane_plus_control'),
        'config',
        'crane_plus_controllers.yaml'
        )

    controller_manager = Node(
        package='controller_manager',
        executable='ros2_control_node',
        parameters=[{'robot_description': LaunchConfiguration('loaded_description')},
                    crane_plus_controllers],
        output={
          'stdout': 'screen',
          'stderr': 'screen',
          },
        )

    spawn_joint_state_controller = ExecuteProcess(
                cmd=['ros2 run controller_manager spawner.py joint_state_controller'],
                shell=True,
                output='screen',
            )

    spawn_arm_controller = ExecuteProcess(
                cmd=['ros2 run controller_manager spawner.py crane_plus_arm_controller'],
                shell=True,
                output='screen',
            )

    spawn_gripper_controller = ExecuteProcess(
                cmd=['ros2 run controller_manager spawner.py crane_plus_gripper_controller'],
                shell=True,
                output='screen',
            )

    return LaunchDescription([
      declare_loaded_description,
      controller_manager,
      spawn_joint_state_controller,
      spawn_arm_controller,
      spawn_gripper_controller
    ])
