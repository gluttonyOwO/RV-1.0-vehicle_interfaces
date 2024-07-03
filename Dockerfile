# This is an auto generated Dockerfile for ros:ros-base
# generated from docker_images_ros2/create_ros_image.Dockerfile.em
FROM ros:humble-ros-core-jammy

# install bootstrap tools
RUN apt-get update && apt-get install --no-install-recommends -y \
    build-essential \
    git \
    nano \
    python3-dev \
    python3-pip \
    python3-colcon-common-extensions \
    python3-colcon-mixin \
    python3-rosdep \
    python3-vcstool \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

# Script will automatically filled up the required packages under line 19
ADD requirements_apt.txt /
RUN apt-get update
RUN apt-get install --no-install-recommends -y $(cat requirement_apt.txt) | xargs




# bootstrap rosdep
RUN rosdep init && \
  rosdep update --rosdistro $ROS_DISTRO

# setup colcon mixin and metadata
RUN colcon mixin add default \
      https://raw.githubusercontent.com/colcon/colcon-mixin-repository/master/index.yaml && \
    colcon mixin update && \
    colcon metadata add default \
      https://raw.githubusercontent.com/colcon/colcon-metadata-repository/master/index.yaml && \
    colcon metadata update

# install ros2 packages
RUN apt-get update && apt-get install -y --no-install-recommends \
    ros-humble-ros-base=0.10.0-1* \
    && rm -rf /var/lib/apt/lists/*


RUN mkdir -p ros2_ws/src
RUN mkdir -p ros2_ws/launch/qos/
COPY vehicle_interfaces/ /ros2_ws/src/vehicle_interfaces/
WORKDIR /ros2_ws

RUN . /opt/ros/${ROS_DISTRO}/setup.sh && colcon build
