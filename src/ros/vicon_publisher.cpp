#include "vicon_publisher.h"
#include <geometry_msgs/Pose.h>
#include <angles/angles.h>

whycon::ViconPublisher::ViconPublisher(ros::NodeHandle &n) {
    std::string vicon_quad_topic, vicon_payload_topic;
    n.param("vicon_quad_topic", vicon_quad_topic, std::string(""));
    n.param("vicon_payload_topic", vicon_payload_topic, std::string(""));
    n.param("cable_length", cable_length, 1.0);
    n.param("distance_tag_CoG", distance_tag_CoG, 0.0);
    n.param("transform_to_world_frame", transform_to_world_frame, false);

    // n.param("axis_length_tolerance", axis_length_tolerance, 0.05);
    // n.param("world_frame", world_frame, std::string("world"));
    // n.param("target_frame", target_frame, std::string("target"));

    // broadcaster = boost::make_shared<tf::TransformBroadcaster>();
    vicon_quad_sub = n.subscribe(vicon_quad_topic, 1, &whycon::ViconPublisher::vicon_quad_callback, this);
    ROS_INFO("subscribed to odometry msg quadrotor: %s", vicon_quad_topic.c_str());
    vicon_payload_sub = n.subscribe(vicon_payload_topic.c_str(), 1, &whycon::ViconPublisher::vicon_payload_callback,
                                    this);
    ROS_INFO("subscribed to odometry msg payload: %s", vicon_payload_topic.c_str());

    odom_vicon_pub = n.advertise<nav_msgs::Odometry>("odom_vicon", 1);
}

void whycon::ViconPublisher::vicon_payload_callback(const nav_msgs::Odometry &msg) {
    //ROS_INFO("v_payload %f", std::abs(time_new_vicon_payload_ - time_new_vicon_quad_));
    time_new_vicon_payload_ = msg.header.stamp.sec + msg.header.stamp.nsec * 1e-9;
    //ROS_INFO("v_payload +");

    vicon_payload_pos_ = {msg.pose.pose.position.x, msg.pose.pose.position.y, msg.pose.pose.position.z};
    vicon_payload_vel_ = {msg.twist.twist.linear.x, msg.twist.twist.linear.y, msg.twist.twist.linear.z};

    if (std::abs(time_new_vicon_payload_ - time_new_vicon_quad_) < 0.005)
        vicon_publish_msg(msg.header);
}

void whycon::ViconPublisher::vicon_quad_callback(const nav_msgs::Odometry &msg) {
    //ROS_INFO("q_payload %f", std::abs(time_new_vicon_payload_ - time_new_vicon_quad_));
    time_new_vicon_quad_ = msg.header.stamp.sec + msg.header.stamp.nsec * 1e-9;

    //ROS_INFO("q_payload +");
    R_WB_ = {1 - 2 * msg.pose.pose.orientation.y * msg.pose.pose.orientation.y -
             2 * msg.pose.pose.orientation.z * msg.pose.pose.orientation.z,
             2 * msg.pose.pose.orientation.x * msg.pose.pose.orientation.y -
             2 * msg.pose.pose.orientation.z * msg.pose.pose.orientation.w,
             2 * msg.pose.pose.orientation.x * msg.pose.pose.orientation.z +
             2 * msg.pose.pose.orientation.y * msg.pose.pose.orientation.w,
             2 * msg.pose.pose.orientation.x * msg.pose.pose.orientation.y +
             2 * msg.pose.pose.orientation.z * msg.pose.pose.orientation.w,
             1 - 2 * msg.pose.pose.orientation.x * msg.pose.pose.orientation.x -
             2 * msg.pose.pose.orientation.z * msg.pose.pose.orientation.z,
             2 * msg.pose.pose.orientation.y * msg.pose.pose.orientation.z -
             2 * msg.pose.pose.orientation.x * msg.pose.pose.orientation.w,
             2 * msg.pose.pose.orientation.x * msg.pose.pose.orientation.z -
             2 * msg.pose.pose.orientation.y * msg.pose.pose.orientation.w,
             2 * msg.pose.pose.orientation.y * msg.pose.pose.orientation.z +
             2 * msg.pose.pose.orientation.x * msg.pose.pose.orientation.w,
             1 - 2 * msg.pose.pose.orientation.x * msg.pose.pose.orientation.x -
             2 * msg.pose.pose.orientation.y * msg.pose.pose.orientation.y};

    vicon_quad_pos_ = {msg.pose.pose.position.x, msg.pose.pose.position.y, msg.pose.pose.position.z};
    vicon_quad_vel_ = {msg.twist.twist.linear.x, msg.twist.twist.linear.y, msg.twist.twist.linear.z};
    vicon_quad_angVel_ = {msg.twist.twist.angular.x, msg.twist.twist.angular.y, msg.twist.twist.angular.z};

    if (std::abs(time_new_vicon_payload_ - time_new_vicon_quad_) < 1.5*1.0/100.0)
        vicon_publish_msg(msg.header);
}

void whycon::ViconPublisher::vicon_publish_msg(const std_msgs::Header_<std::allocator<void>> &header) {
    //ROS_INFO("publish");
    if (transform_to_world_frame) // in world frame
        relative_pos_outputFrame = vicon_payload_pos_ - vicon_quad_pos_;
    else // in body frame
        relative_pos_outputFrame = R_WB_.t() * (vicon_payload_pos_ - vicon_quad_pos_);

    relative_pos_outputFrame = relative_pos_outputFrame *
                               (cable_length + distance_tag_CoG) / cv::norm(relative_pos_outputFrame);

    payload_vicon.pose.pose.position.x = relative_pos_outputFrame(0);
    payload_vicon.pose.pose.position.y = relative_pos_outputFrame(1);
    payload_vicon.pose.pose.position.z = relative_pos_outputFrame(2);

    // calculate relative angle
    payload_vicon.pose.pose.orientation.x =  atan2(relative_pos_outputFrame(1), -relative_pos_outputFrame(2));
    payload_vicon.pose.pose.orientation.y = -atan2(relative_pos_outputFrame(0), -relative_pos_outputFrame(2));
    payload_vicon.pose.pose.orientation.z = 0;
    payload_vicon.pose.pose.orientation.w = 0;

    // calculate relative velocity
    if (transform_to_world_frame) // in world frame
        relative_vel_outputFrame = vicon_payload_vel_ - vicon_quad_vel_;
    else // in body frame
        relative_vel_outputFrame = (R_WB_.t() * (vicon_payload_vel_ - vicon_quad_vel_)) + \
                                   (R_WB_.t() * (vicon_payload_pos_ - vicon_quad_pos_).cross(vicon_quad_angVel_));

    payload_vicon.twist.twist.linear.x = relative_vel_outputFrame(0);
    payload_vicon.twist.twist.linear.y = relative_vel_outputFrame(1);
    payload_vicon.twist.twist.linear.z = relative_vel_outputFrame(2);

    // calculate relative angular velocity
    relative_ang_vel = (vicon_payload_pos_ - vicon_quad_pos_).cross(vicon_payload_vel_ - vicon_quad_vel_) /
                       (vicon_payload_pos_ - vicon_quad_pos_).dot(vicon_payload_pos_ - vicon_quad_pos_);
    if (!transform_to_world_frame)
        relative_ang_vel = R_WB_ * (relative_ang_vel + vicon_quad_angVel_);

    payload_vicon.twist.twist.angular.x = relative_ang_vel(0);
    payload_vicon.twist.twist.angular.y = relative_ang_vel(1);
    payload_vicon.twist.twist.angular.z = 0;

    payload_vicon.header = header;
    odom_vicon_pub.publish(payload_vicon);
}

