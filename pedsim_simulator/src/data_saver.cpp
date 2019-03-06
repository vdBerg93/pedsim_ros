/*
 * Copyright (c) Social Robotics Laboratory
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \author Francisco J Marquez Bonilla <F.J.MarquezBonilla@student.tudelft.nl>
 */

#include <tf/transform_listener.h> // must come first due to conflict with Boost signals

/// ros
#include <ros/ros.h>

/// meta
#include <random>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <fstream>

/// data
//#include <sensor_msgs/PointData.h>
#include <nav_msgs/GridCells.h>
#include <nav_msgs/Odometry.h>
#include <geometry_msgs/Point.h>
#include <geometry_msgs/PoseStamped.h>
#include <spencer_tracking_msgs/TrackedPerson.h>
#include <spencer_tracking_msgs/TrackedPersons.h>
#include <pedsim_simulator/transpose.h>

/// -----------------------------------------------------------
/// \class PedsimData
/// \brief Receives data from pedsim containing
/// persons and save it as dataset for Social LSTM
/// -----------------------------------------------------------
class PedsimData {
public:
    PedsimData(const ros::NodeHandle& node)
            : nh_(node)
    {
        // set up subscribers
        sub_tracked_persons_ = nh_.subscribe("/pedsim/tracked_persons", 1, &PedsimData::callbackTrackedPersons, this);
        sub_robot_goal_ = nh_.subscribe("/pedsim/goal", 1, &PedsimData::callbackRobotGoal, this);
//        sub_robot_odom_ = nh_.subscribe("/pedsim/robot_position", 1, &PedsimData::callbackRobotOdom, this);

        // setup TF listener for obtaining robot position
        transform_listener_ = boost::make_shared<tf::TransformListener>();

        robot_position_.clear();
        robot_position_.resize(2);
        robot_position_ = { 0, 0 };

        robot_goal_.clear();
        robot_goal_.resize(2);
        robot_goal_ = { 0, 0 };


        nh_.param<std::string>("/data_saver/robot_frame", robot_frame_, "base_link");
//        robot_frame_ = "odom";

        // read local map dimensions
        nh_.param("/data_saver/local_width", local_width_, 12.0);
        nh_.param("/data_saver/local_height", local_height_, 12.0);

        nh_.param("/data_saver/global_width", global_width_, 50.0);
        nh_.param("/data_saver/global_height", global_height_, 50.0);

        // sampling rate
        nh_.param("/data_saver/rate", rate_, 2.5);

        // flip param
        nh_.param("/data_saver/flip", flip_, 1);

        // Dataset params
        nh_.param<std::string>("/data_saver/path", path_, "pedsim_pos");
        nh_.param("/data_saver/size", size_, 100.0);
        path_ = path_+ "_" + std::to_string(static_cast<int>(size_)) + std::to_string(static_cast<int>(flip_)) + ".csv";

        // Open dataset
        dataset_.open(path_);

        // Initialize counter
        counter_ = 0;
    }
    virtual ~PedsimData()
    {
//        sub_grid_cells_.shutdown();
//        pub_point_Data_global_.shutdown();
//        pub_point_Data_local_.shutdown();
//        pub_people_Data_global_.shutdown();
//        pub_people_Data_local_.shutdown();
        dataset_.close();
    }

    // control
    void run();

    // subscriber callbacks
//    void callbackGridCells(const nav_msgs::GridCells::ConstPtr& msg);
    void callbackTrackedPersons(const spencer_tracking_msgs::TrackedPersons::ConstPtr& msg);
    void callbackRobotGoal(const geometry_msgs::Point::ConstPtr& msg);
//    void callbackRobotOdom(const nav_msgs::Odometry::ConstPtr& msg);

private:
    ros::NodeHandle nh_;

    // robot position
    std::vector<double> robot_position_;
    std::vector<double> robot_goal_;

    // Dataset path
    std::string path_;

    // Callback counter
    int counter_=0;

    // local zone around robot (used in local costmaps)
    double local_width_;
    double local_height_;
    double global_width_;
    double global_height_;

    // Dataset
    std::ofstream dataset_;

    // Sampling rate
    double rate_;
    double size_;
    // flip param
    int flip_;

    std::string robot_frame_;

    // publishers

    // subscribers
    ros::Subscriber sub_tracked_persons_;
    ros::Subscriber sub_robot_goal_;


    // Transform listener coverting people poses to be relative to the robot
    boost::shared_ptr<tf::TransformListener> transform_listener_;

protected:
    // check if a point is in the local zone of the robot
    bool inLocalZone(const std::array<double, 2>& point);
};

/// -----------------------------------------------------------
/// \function run
/// \brief Run the node
/// -----------------------------------------------------------
void PedsimData::run()
{
    ros::Rate r(rate_); // Hz
    while (ros::ok() && counter_ < size_) {
        ros::spinOnce();
        r.sleep();
    }
    transpose_CSV(path_);
}

/// -----------------------------------------------------------
/// \function inLocalZone
/// \brief Check is a point (e.g. center of person or obstacle)
/// is within the local zone of the robot to be included in the
/// robot's local costmap for planning and other higher level
/// cognition
/// -----------------------------------------------------------
bool PedsimData::inLocalZone(const std::array<double, 2>& point)
{
    // NOTE - this hack expands the local map, but that should not cause any
    // issue since we are mainly interested in not missing anythin in the
    // local region
    double diff_width = std::abs(robot_position_[0] - point[0]) - local_width_/2.0;
    double diff_height = std::abs(robot_position_[1] - point[1]) - local_height_/2.0;

    const double dist = std::max(diff_width, diff_height);
    const double r = -0.00001;

    if (dist <= r)
        return true;
    else
        return false;
}

/// -----------------------------------------------------------
/// \function callbackTrackedPersons
/// \brief Receives tracked persons messages and store them into a dataset format
/// The scheme of the CSV file is: Frame_id | Ped_id | Pos_y | Pos_x | Twist_x | Twist_y | Or_z | Or_w | Goal_x | Goal_y
/// -----------------------------------------------------------
void PedsimData::callbackTrackedPersons(const spencer_tracking_msgs::TrackedPersons::ConstPtr& msg)
{

    spencer_tracking_msgs::TrackedPerson robot = msg->tracks[0];
    robot_position_[0] = robot.pose.pose.position.x;
    robot_position_[1] = robot.pose.pose.position.y;

    double ego_x = 2.0*robot.pose.pose.position.x/global_width_-1.0;
    double ego_y = 2.0*robot.pose.pose.position.y/global_height_-1.0;

    double goal_x = 2.0*robot_goal_[0]/global_width_-1.0;
    double goal_y = 2.0*robot_goal_[1]/global_height_-1.0;

    double vel_x = robot.twist.twist.linear.x;
    double vel_y = robot.twist.twist.linear.y;
    if (vel_x > 10)        vel_x = vel_x/1000.0;
    if (vel_y > 10)        vel_y = vel_y/1000.0;

    double aux=0;
    double angle = atan2(robot.pose.pose.orientation.z,robot.pose.pose.orientation.w);

    double quat_z = robot.pose.pose.orientation.z;
    double quat_w = robot.pose.pose.orientation.w;

    if ((goal_x > -0.99) || (goal_y > -0.99))
    {


        switch (flip_) {
            case 1 :
                break;
            case 2 : {      // flip 90 degrees
                aux = ego_x;
                ego_x = ego_y;
                ego_y = 1 - aux;

                aux = vel_x;
                vel_x = vel_y;
                vel_y = 1 - aux;

                aux = goal_x;
                goal_x = goal_y;
                goal_y = 1 - aux;

                quat_w = cos(angle + M_PI_4);
                quat_z = sin(angle + M_PI_4);

                break;
            }
            case 3 : {      // flip 180 degrees
                ego_x = 1 - ego_x;
                ego_y = 1 - ego_y;

                vel_x = 1 - vel_x;
                vel_y = 1 - vel_y;

                aux = goal_x;
                goal_x = 1 - goal_x;
                goal_y = 1 - goal_y;

                quat_w = cos(angle + M_PI_2);
                quat_z = sin(angle + M_PI_2);
                break;
            }
            case 4 : {      // flip 270 degrees
                aux = ego_x;
                ego_x = 1 - ego_y;
                ego_y = aux;

                aux = vel_x;
                vel_x = 1 - vel_y;
                vel_y = aux;

                aux = goal_x;
                goal_x = 1 - goal_y;
                goal_y = aux;

                quat_w = cos(angle - M_PI_4);
                quat_z = sin(angle - M_PI_4);
                break;
            }
        }
        dataset_ << counter_ << ',' << robot.track_id + 1 << ',' << ego_y << ',' << ego_x << ','
                 << vel_x << ',' << vel_y << ','
                 << quat_z << ',' << quat_w << ','
                 << goal_x << ',' << goal_y << ',' << std::endl;

        for (unsigned int i = 1; i < msg->tracks.size(); i++) {
            spencer_tracking_msgs::TrackedPerson p = msg->tracks[i];
            std::array<double, 2> person = {p.pose.pose.position.x, p.pose.pose.position.y};
            const bool inside = inLocalZone(person);
            if (inside) {
                double pos_x = 2.0 * p.pose.pose.position.x / global_width_ - 1.0;
                double pos_y = 2.0 * p.pose.pose.position.y / global_height_ - 1.0;
                double vel_x = p.twist.twist.linear.x;
                double vel_y = p.twist.twist.linear.y;
                if (vel_x > 10) vel_x = vel_x / 1000.0;
                if (vel_y > 10) vel_y = vel_y / 1000.0;
                dataset_ << counter_ << ',' << p.track_id + 1 << ',' << pos_y << ',' << pos_x << ','
                         << vel_x << ',' << vel_y << ','
                         << p.pose.pose.orientation.z << ',' << p.pose.pose.orientation.w << ','
                         << 0.0 << ',' << 0.0 << ',' << std::endl;
            }
        }
        counter_ += 1;
    }
}


/// -----------------------------------------------------------
/// \function callbackRobotOdom
/// \brief Receives robot position and cache it for use later
/// -----------------------------------------------------------
//void PedsimData::callbackRobotOdom(const nav_msgs::Odometry::ConstPtr& msg)
//{
//    robot_position_[0] = msg->pose.pose.position.x;
//    robot_position_[1] = msg->pose.pose.position.y;
//
//    robot_frame_ = msg->header.frame_id;
//}

/// -----------------------------------------------------------
/// \function callbackRobotOdom
/// \brief Receives robot position and cache it for use later
/// -----------------------------------------------------------
void PedsimData::callbackRobotGoal(const geometry_msgs::Point::ConstPtr& msg)
{
    robot_goal_[0] = msg->x;
    robot_goal_[1] = msg->y;
}

/// -----------------------------------------------------------
/// main
/// -----------------------------------------------------------
int main(int argc, char** argv)
{
    ros::init(argc, argv, "pedsim_data_saver");

    ros::NodeHandle n;

    PedsimData g(n);
    g.run();

    return EXIT_SUCCESS;
}