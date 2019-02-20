/** @file dji_sdk_node_actions.cpp
 *  @version 3.1.8
 *  @date July 29th, 2016
 *
 *  @brief
 *  All the action callbacks are implemented here.
 *
 *  @copyright 2016 DJI. All rights reserved.
 *
 */

#include <dji_sdk/dji_sdk_node.h>
#include <algorithm>



bool DJISDKNode::drone_task_action_callback(const dji_sdk::DroneTaskGoalConstPtr &goal)
{
  uint8_t request_action = goal->task;

  if (request_action == 1)
  {
    //takeoff
    std::cout << "Request Control" << std::endl;
    rosAdapter->coreAPI->setControl(1);
    rosAdapter->flight->task(DJI::onboardSDK::Flight::TASK::TASK_TAKEOFF);
  }
  else if (request_action == 2)
  {
    //landing
    rosAdapter->flight->task(DJI::onboardSDK::Flight::TASK::TASK_LANDING);
    std::cout << "Release Control" << std::endl;
    rosAdapter->coreAPI->setControl(0);
  }
  else if (request_action == 3)
  {
    //gohome
    rosAdapter->flight->task(DJI::onboardSDK::Flight::TASK::TASK_GOHOME);
  }

  drone_task_feedback.progress = 1;
  drone_task_action_server->publishFeedback(drone_task_feedback);
  drone_task_action_server->setSucceeded();

  return true;
}

bool DJISDKNode::local_position_navigation_action_callback(const dji_sdk::LocalPositionNavigationGoalConstPtr &goal)
{
  /*IMPORTANT*/
  /*
     There has been declared a pointer `local_navigation_action` as the function parameter,
     However, it is the `local_navigation_action_server` that we should use.
     If `local_navigation_action` is used instead, there will be a runtime sengmentation fault.

     so interesting
  */

  float dst_x = goal->x;
  float dst_y = goal->y;
  float dst_z = goal->z;

  float org_x = odometry.pose.pose.position.x;
  float org_y = odometry.pose.pose.position.y;
  float org_z = odometry.pose.pose.position.z;

  float dis_x = dst_x - org_x;
  float dis_y = dst_y - org_y;
  float dis_z = dst_z - org_z;

  float det_x, det_y, det_z;

  DJI::onboardSDK::FlightData flight_ctrl_data;
  flight_ctrl_data.flag = 0b01001011; // Velocities in all X, Y and Z directions, yaw rate in world frame

  flight_ctrl_data.yaw = 0;

  int x_progress = 0;
  int y_progress = 0;
  int z_progress = 0;
  ros::Rate loop_rate(50);

  while (x_progress < 100 || y_progress < 100 || z_progress < 100)
  {
    float lo = -3.0;
    float hi = 3.0;


    float commandX = dst_x - odometry.pose.pose.position.x - odometry.twist.twist.linear.x;
    float commandY = dst_y - odometry.pose.pose.position.y - odometry.twist.twist.linear.y;
    float commandZ = dst_z - odometry.pose.pose.position.z - odometry.twist.twist.linear.z;
    commandX = clip(commandX, lo, hi);
    commandY = clip(commandY, lo, hi);
    //float commandZ = dst_z;
    
    tf::Vector3 c_world(commandX, commandY, commandZ);
    tf::Quaternion q(odometry.pose.pose.orientation.x,
      odometry.pose.pose.orientation.y,
      odometry.pose.pose.orientation.z,
      odometry.pose.pose.orientation.w);
      tf::Vector3 c_body = c_world.rotate(q.getAxis(), -q.getAngle());
      flight_ctrl_data.x = c_body.x();
      flight_ctrl_data.y = -c_body.y();
      flight_ctrl_data.z = commandZ; // should be in world (odom) frame
    rosAdapter->flight->setFlight(&flight_ctrl_data);

    det_x = (100 * (dst_x - odometry.pose.pose.position.x)) / dis_x;
    det_y = (100 * (dst_y - odometry.pose.pose.position.y)) / dis_y;
    det_z = (100 * (dst_z - odometry.pose.pose.position.z)) / dis_z;

    x_progress = 100 - (int)det_x;
    y_progress = 100 - (int)det_y;
    z_progress = 100 - (int)det_z;

    //lazy evaluation
    // TODO: Fix add rosparam for indicating that it has reached the waypoint
    if (std::abs(dst_x - odometry.pose.pose.position.x) < 0.5)
      x_progress = 100;
    if (std::abs(dst_y - odometry.pose.pose.position.y) < 0.5)
      y_progress = 100;
    if (std::abs(dst_z - odometry.pose.pose.position.z) < 0.5)
      z_progress = 100;

    local_position_navigation_feedback.x_progress = x_progress;
    local_position_navigation_feedback.y_progress = y_progress;
    local_position_navigation_feedback.z_progress = z_progress;
    local_position_navigation_action_server->publishFeedback(local_position_navigation_feedback);
    loop_rate.sleep();
  }

  local_position_navigation_result.result = true;
  local_position_navigation_action_server->setSucceeded(local_position_navigation_result);

  return true;
}

bool DJISDKNode::external_position_navigation_action_callback(const dji_sdk::LocalPositionNavigationGoalConstPtr &goal)
{

  // Goal position
  float dst_x = goal->x;
  float dst_y = goal->y;
  float dst_z = goal->z;

  std::cout << "Destination: x = " << dst_x << ", y = " << dst_y << ", z = " << dst_z << std::endl;

  // Current position
  float org_x = external_position.x;
  float org_y = external_position.y;
  float org_z = external_position.z;

  float dis_x = dst_x - org_x;
  float dis_y = dst_y - org_y;
  float dis_z = dst_z - org_z;

  float old_err_x = 0;
  float old_err_y = 0;
  float old_err_z = 0;

  float err_x, err_y, err_z;
  float det_x, det_y, det_z;
  float i_part_x, i_part_y, i_part_z;
  float d_part_x, d_part_y, d_part_z;

  float Kp_x = 0.7;
  float Kp_y = 0.7;
  float Kp_z = 3;

  float Ki_x = 0;
  float Ki_y = 0;
  float Ki_z = 0;

  float Kd_x = 0;
  float Kd_y = 0;
  float Kd_z = 0;

  int x_progress = 0;
  int y_progress = 0;
  int z_progress = 0;

  DJI::onboardSDK::FlightData flight_ctrl_data;
  flight_ctrl_data.flag = 0x90;
  flight_ctrl_data.yaw = 0;

  while (1)
  {

    // Compute progress, send action feedback
    det_x = (100 * (dst_x - external_position.x)) / dis_x;
    det_y = (100 * (dst_y - external_position.y)) / dis_y;
    det_z = (100 * (dst_z - external_position.z)) / dis_z;

    x_progress = 100 - (int)det_x;
    y_progress = 100 - (int)det_y;
    z_progress = 100 - (int)det_z;

    external_position_navigation_feedback.x_progress = x_progress;
    external_position_navigation_feedback.y_progress = y_progress;
    external_position_navigation_feedback.z_progress = z_progress;
    external_position_navigation_action_server->publishFeedback(external_position_navigation_feedback);

    // Errors in x-y-z
    err_x = dst_x - external_position.x;
    err_y = dst_y - external_position.y;
    err_z = dst_z - external_position.z;

    i_part_x += err_x;
    i_part_y += err_y;
    i_part_z += err_z;

    d_part_x = err_x - old_err_x;
    d_part_y = err_y - old_err_y;
    d_part_z = err_z - old_err_z;

    flight_ctrl_data.x = Kp_x * err_x + Ki_x * i_part_x + Kd_x * d_part_x;
    flight_ctrl_data.y = Kp_y * err_y + Ki_y * i_part_y + Kd_y * d_part_y;
    flight_ctrl_data.z = Kp_z * err_z + Ki_z * i_part_z + Kd_z * d_part_z;
    rosAdapter->flight->setFlight(&flight_ctrl_data);

    old_err_x = err_x;
    old_err_y = err_y;
    old_err_z = err_z;

    if ((std::abs(dst_x - external_position.x) < 0.1) && (std::abs(dst_y - external_position.y) < 0.1) && (std::abs(dst_z - external_position.z) < 0.1))
    {
      break;
    }

    usleep(20000);
  }

  external_position_navigation_result.result = true;
  external_position_navigation_action_server->setSucceeded(external_position_navigation_result);

  return true;
}

bool DJISDKNode::global_position_navigation_action_callback(const dji_sdk::GlobalPositionNavigationGoalConstPtr &goal)
{
  double dst_latitude = goal->latitude;
  double dst_longitude = goal->longitude;
  float dst_altitude = goal->altitude;

  double org_latitude = global_position.latitude;
  double org_longitude = global_position.longitude;
  float org_altitude = global_position.altitude;

  double dis_x, dis_y;
  float dis_z;

  dis_x = dst_latitude - org_latitude;
  dis_y = dst_longitude - org_longitude;
  dis_z = dst_altitude - org_altitude;

  double det_x, det_y;
  float det_z;

  DJI::onboardSDK::FlightData flight_ctrl_data;
  flight_ctrl_data.flag = 0x81;
  flight_ctrl_data.yaw = 0;

  int latitude_progress = 0;
  int longitude_progress = 0;
  int altitude_progress = 0;
  ros::Rate loop_rate(50);

  while (latitude_progress < 100 || longitude_progress < 100 || altitude_progress < 100)
  {

    double d_lon = dst_longitude - global_position.longitude;
    double d_lat = dst_latitude - global_position.latitude;

    flight_ctrl_data.x = ((d_lat)*C_PI / 180) * C_EARTH - velocity.vx;
    flight_ctrl_data.y = ((d_lon)*C_PI / 180) * C_EARTH * cos((dst_latitude)*C_PI / 180) - velocity.vy;
    flight_ctrl_data.z = dst_altitude - global_position.altitude - velocity.vz;
    rosAdapter->flight->setFlight(&flight_ctrl_data);

    det_x = (100 * (dst_latitude - global_position.latitude)) / dis_x;
    det_y = (100 * (dst_longitude - global_position.longitude)) / dis_y;
    det_z = (100 * (dst_altitude - global_position.altitude)) / dis_z;

    latitude_progress = 100 - (int)det_x;
    longitude_progress = 100 - (int)det_y;
    altitude_progress = 100 - (int)det_z;

    //lazy evaluation
    if (std::abs(dst_latitude - global_position.latitude) < 0.00001)
      latitude_progress = 100;
    if (std::abs(dst_longitude - global_position.longitude) < 0.00001)
      longitude_progress = 100;
    if (std::abs(dst_altitude - global_position.altitude) < 0.12)
      altitude_progress = 100;

    global_position_navigation_feedback.latitude_progress = latitude_progress;
    global_position_navigation_feedback.longitude_progress = longitude_progress;
    global_position_navigation_feedback.altitude_progress = altitude_progress;
    global_position_navigation_action_server->publishFeedback(global_position_navigation_feedback);
    loop_rate.sleep();
  }

  global_position_navigation_result.result = true;
  global_position_navigation_action_server->setSucceeded(global_position_navigation_result);

  return true;
}

bool DJISDKNode::process_gps_waypoint(dji_sdk::Waypoint new_waypoint)
{
  double dst_latitude = new_waypoint.latitude;
  double dst_longitude = new_waypoint.longitude;
  float dst_altitude = new_waypoint.altitude;

  double org_latitude = global_position.latitude;
  double org_longitude = global_position.longitude;
  float org_altitude = global_position.altitude;

  double dis_x, dis_y;
  float dis_z;

  dis_x = dst_latitude - org_latitude;
  dis_y = dst_longitude - org_longitude;
  dis_z = dst_altitude - org_altitude;

  double det_x, det_y;
  float det_z;

  DJI::onboardSDK::FlightData flight_ctrl_data;
  flight_ctrl_data.flag = 0x81;
  flight_ctrl_data.yaw = new_waypoint.heading;

  int latitude_progress = 0;
  int longitude_progress = 0;
  int altitude_progress = 0;
  ros::Rate loop_rate(50);

  while (latitude_progress < 100 || longitude_progress < 100 || altitude_progress < 100)
  {
    if (waypoint_navigation_action_server->isPreemptRequested())
    {
      return false;
    }

    double d_lon = dst_longitude - global_position.longitude;
    double d_lat = dst_latitude - global_position.latitude;

    flight_ctrl_data.x = ((d_lat)*C_PI / 180) * C_EARTH - velocity.vx;
    flight_ctrl_data.y = ((d_lon)*C_PI / 180) * C_EARTH * cos((dst_latitude)*C_PI / 180) - velocity.vy;
    flight_ctrl_data.z = dst_altitude - global_position.altitude - velocity.vz;

    rosAdapter->flight->setFlight(&flight_ctrl_data);

    det_x = (100 * (dst_latitude - global_position.latitude)) / dis_x;
    det_y = (100 * (dst_longitude - global_position.longitude)) / dis_y;
    det_z = (100 * (dst_altitude - global_position.altitude)) / dis_z;

    latitude_progress = 100 - std::abs((int)det_x);
    longitude_progress = 100 - std::abs((int)det_y);
    altitude_progress = 100 - std::abs((int)det_z);

    //lazy evaluation
    //need to find a better way
    if (std::abs(dst_latitude - global_position.latitude) < 0.00001)
      latitude_progress = 100;
    if (std::abs(dst_longitude - global_position.longitude) < 0.00001)
      longitude_progress = 100;
    if (std::abs(dst_altitude - global_position.altitude) < 0.12)
      altitude_progress = 100;

    waypoint_navigation_feedback.latitude_progress = latitude_progress;
    waypoint_navigation_feedback.longitude_progress = longitude_progress;
    waypoint_navigation_feedback.altitude_progress = altitude_progress;
    waypoint_navigation_action_server->publishFeedback(waypoint_navigation_feedback);
    loop_rate.sleep();
  }
  ros::Duration(new_waypoint.staytime).sleep();
  return true;
}

bool DJISDKNode::waypoint_navigation_action_callback(const dji_sdk::WaypointNavigationGoalConstPtr &goal)
{
  dji_sdk::WaypointList new_waypoint_list;
  new_waypoint_list = goal->waypoint_list;

  bool isSucceeded;
  for (int i = 0; i < new_waypoint_list.waypoint_list.size(); i++)
  {
    const dji_sdk::Waypoint new_waypoint = new_waypoint_list.waypoint_list[i];
    waypoint_navigation_feedback.index_progress = i;
    isSucceeded = process_gps_waypoint(new_waypoint);
    if (!isSucceeded)
    {
      waypoint_navigation_result.result = false;
      waypoint_navigation_action_server->setPreempted(waypoint_navigation_result);
      return false;
    }
  }

  waypoint_navigation_result.result = true;
  waypoint_navigation_action_server->setSucceeded(waypoint_navigation_result);

  return true;
}

bool DJISDKNode::local_waypoint_mission_action_callback(const dji_sdk::LocalWaypointMissionGoalConstPtr &goal)
{
  dji_sdk::MissionWaypointTask waypoint_task;
  dji_sdk::MissionWaypoint 	 waypoint;
    /*  
      dji_sdk::WaypointList new_waypoint_list;
      new_waypoint_list = goal->waypoint_list;
    */
  bool isSucceeded;
  // Clear the vector of previous waypoints 
  waypoint_task.mission_waypoint.clear();
  //mission waypoint upload
  waypoint_task.velocity_range = 10;
  waypoint_task.idle_velocity = 3;
  waypoint_task.action_on_finish = 2;
  waypoint_task.mission_exec_times = 1;
  waypoint_task.yaw_mode = 4; // 0 -> head towards next wp, 4 -> do not change heading
  waypoint_task.trace_mode = 1;
  waypoint_task.action_on_rc_lost = 0;
  waypoint_task.gimbal_pitch_mode = 0;
  waypoint.damping_distance = 1;
  waypoint.target_yaw = 0;
  waypoint.target_gimbal_pitch = 0;
  waypoint.turn_mode = 0;
  waypoint.has_action = 0;
  
  double orig_lat, orig_long, orig_alt;
  geodetic_converter.getReference(&orig_lat, &orig_long, &orig_alt);
  ROS_INFO("Lat: %f", orig_lat);
  ROS_INFO("Lon: %f", orig_long);
  ROS_INFO("Alt: %f", orig_alt);

  for (int i = 0; i < goal->waypoint_list.points.size(); i++) // 
  {
    geometry_msgs::Vector3 point = goal->waypoint_list.points[i].transforms[0].translation;
    double east = point.x;
    double north = point.y;
    double up = point.z;
    double lat, lon, alt;
    geodetic_converter.enu2Geodetic(east, north, up, &lat, &lon,&alt);
    waypoint.latitude = (float)lat; waypoint.longitude= (float)lon; waypoint.altitude = (float)(alt-orig_alt); // Need to be floats, otherwise out of memory errors may occur
    waypoint_task.mission_waypoint.push_back(waypoint);
    ROS_INFO_STREAM("Added " << i << "th waypoint. Lat: " << lat << " Lon: " <<  lon << "Alt: " << alt);
  }
  ros::NodeHandle nh;
  rosAdapter->coreAPI->setControl(1);
  sleep(1);
  rosAdapter->coreAPI->setControl(1);
  sleep(1); // To obtain control robustly
  dji_sdk::MissionWpUpload mission_waypoint_task;
  ros::ServiceClient mission_wp_upload_service = nh.serviceClient<dji_sdk::MissionWpUpload>("dji_sdk/mission_waypoint_upload");  
  mission_waypoint_task.request.waypoint_task = waypoint_task;
  bool uploadSuccess = mission_wp_upload_service.call(mission_waypoint_task);
  if (!(uploadSuccess && mission_waypoint_task.response.result));
  {
    local_waypoint_mission_result.result = false;
    local_waypoint_mission_action_server->setPreempted(local_waypoint_mission_result);
    return false;
  }
  sleep( goal->waypoint_list.points.size());

  waypoint_navigation_result.result = true;
  waypoint_navigation_action_server->setSucceeded(waypoint_navigation_result);

  return true;
}
