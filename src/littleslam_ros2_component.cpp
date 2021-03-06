#include "littleslam_ros2/littleslam_ros2_component.hpp"

#include <rclcpp/rclcpp.hpp>

#include <class_loader/register_macro.hpp>
#include <chrono>
#include <memory>

#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>

#include <iostream>
#include <sstream>

typedef pcl::PointCloud<pcl::PointXYZ> PointCloud;

boost::circular_buffer<Scan2D> scan_buf(1000);

using namespace std::chrono_literals;

namespace littleslam_ros2
{

Littleslam::Littleslam()
: Node("littleslam")
{ 

    use_odom=false;

    fc.setSlamFrontEnd(sf);
    fc.makeFramework();
    if(use_odom){
        fc.customizeI();
    }
    else fc.customizeG();
    
    laser_sub_ = this->create_subscription<sensor_msgs::msg::LaserScan>(
    "scan", 100, std::bind(&Littleslam::scan_cb, this, std::placeholders::_1));

    icp_map_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>("icp_map", 10); 

    path_pub_ = create_publisher<nav_msgs::msg::Path>("path", 10);

    current_pose_pub_ = create_publisher<geometry_msgs::msg::PoseStamped>("curent_pose", 10);

    timer_ = create_wall_timer(0.001s, std::bind(&Littleslam::broadcast_littleslam, this));
}


bool Littleslam::make_scan2d(Scan2D &scan2d, const sensor_msgs::msg::LaserScan::SharedPtr scan)
{   

    if(use_odom){
        tf2_ros::Buffer tfbuffer(this->get_clock());
        tf2_ros::TransformListener listener(tfbuffer);

        tf2::Stamped<tf2::Transform> tr;

        try{
            builtin_interfaces::msg::Time time_stamp = scan->header.stamp;
            tf2::TimePoint time_point = tf2::TimePoint(
                std::chrono::seconds(time_stamp.sec) +
                std::chrono::nanoseconds(time_stamp.nanosec));
            tf2::TimePoint time_out;

            geometry_msgs::msg::TransformStamped tf = tfbuffer.lookupTransform(
                "/odom", "/base_link", time_point);

            tf2::fromMsg(tf, tr);
        }
        catch (tf2::TransformException &ex) {
            RCLCPP_ERROR(this->get_logger(),"%s",ex.what());
            return false;
        }
        scan2d.pose.tx = tr.getOrigin().x();
        scan2d.pose.ty = tr.getOrigin().y();
        scan2d.pose.th = RAD2DEG(tf2::impl::getYaw(tr.getRotation()));
        scan2d.pose.calRmat();
    }
    else{
        if(map == nullptr){
            sf->process(scan2d);
            map = sf->getPointCloudMap();
            }
        Pose2D curPose = map->getLastPose();
        scan2d.pose = curPose;
    }

    scan2d.lps.clear();
    for(size_t i=0; i< scan->ranges.size(); ++i) {
        LPoint2D lp;
        double th = scan->angle_min + scan->angle_increment * i;
        double r = scan->ranges[i];
        if (scan->range_min < r && r < scan->range_max) {
            lp.x = r * cos(th); lp.y = r * sin(th);
            scan2d.lps.push_back(lp);
        }
    }

    return true;
}

void Littleslam::scan_cb(const sensor_msgs::msg::LaserScan::SharedPtr scan)
{   
    Scan2D scan2d;
    if (make_scan2d(scan2d, scan)) scan_buf.push_back(scan2d);
}

void Littleslam::broadcast_littleslam()
{
        if (scan_buf.size() == 0) return;

        Scan2D scan2d = scan_buf.front();
        scan_buf.pop_front();

        if (true) {
            sf->process(scan2d);
            map = sf->getPointCloudMap();
    
            PointCloud::Ptr msg(new PointCloud);

            msg->header.frame_id = "map";
            msg->height = msg->width = 1;
            for (auto lp: map->globalMap) msg->points.push_back(pcl::PointXYZ(lp.x, lp.y, 0));
            msg->width = msg->points.size();
            pcl::toROSMsg (*msg, cloud);
            icp_map_pub_->publish(cloud);
            
            nav_msgs::msg::Path path;
            path.header.frame_id = "map";
            for(auto p : map->poses) {
                geometry_msgs::msg::PoseStamped pose;
                pose.pose.position.x = p.tx;
                pose.pose.position.y = p.ty;
                pose.pose.position.z = 0;
                tf2::Quaternion quat_tf;
                quat_tf.setRPY(0.0, 0.0, DEG2RAD(p.th));
                geometry_msgs::msg::Quaternion quat_msg;
                quat_msg = tf2::toMsg(quat_tf);
                pose.pose.orientation = quat_msg;
                path.poses.push_back(pose);
                if(p.tx == map->lastPose.tx && 
                   p.ty == map->lastPose.ty && 
                   p.th == map->lastPose.th){
                    pose.header.frame_id = "map";
                    current_pose_pub_->publish(pose);
                   }
            }
            
            path_pub_->publish(path);
        }
}

}// littleslam_ros2

CLASS_LOADER_REGISTER_CLASS(littleslam_ros2::Littleslam, rclcpp::Node)
