/*
 * op3_walking_module.cpp
 *
 *  Created on: 2016. 5. 16.
 *      Author: JungKM
 */


#include <stdio.h>

#include "op3_walking_module/op3_walking_module.h"

using namespace ROBOTIS;

WalkingMotionModule *WalkingMotionModule::unique_instance_ = new WalkingMotionModule();

WalkingMotionModule::WalkingMotionModule()
: control_cycle_msec_(8)
, walking_state_(WalkingReady)
, DEBUG(false)
, init_count_(0)
{
    enable          = false;
    module_name     = "walking_module";
    control_mode    = POSITION_CONTROL;

    op3_kd_ = new ROBOTIS::OP3KinematicsDynamics(ROBOTIS::WHOLE_BODY);

    // result
    result["r_hip_yaw"  ] = new DynamixelState();
    result["r_hip_roll" ] = new DynamixelState();
    result["r_hip_pitch"] = new DynamixelState();
    result["r_knee"     ] = new DynamixelState();
    result["r_ank_pitch"] = new DynamixelState();
    result["r_ank_roll" ] = new DynamixelState();

    result["l_hip_yaw"  ] = new DynamixelState();
    result["l_hip_roll" ] = new DynamixelState();
    result["l_hip_pitch"] = new DynamixelState();
    result["l_knee"     ] = new DynamixelState();
    result["l_ank_pitch"] = new DynamixelState();
    result["l_ank_roll" ] = new DynamixelState();

    result["r_sho_pitch"] = new DynamixelState();
    result["l_sho_pitch"] = new DynamixelState();
    // result["head_pan"   ] = new DynamixelState();

    // joint table
    joint_table_["r_hip_yaw"  ] = 0;
    joint_table_["r_hip_roll" ] = 1;
    joint_table_["r_hip_pitch"] = 2;
    joint_table_["r_knee"     ] = 3;
    joint_table_["r_ank_pitch"] = 4;
    joint_table_["r_ank_roll" ] = 5;

    joint_table_["l_hip_yaw"  ] = 6;
    joint_table_["l_hip_roll" ] = 7;
    joint_table_["l_hip_pitch"] = 8;
    joint_table_["l_knee"     ] = 9;
    joint_table_["l_ank_pitch"] = 10;
    joint_table_["l_ank_roll" ] = 11;

    joint_table_["r_sho_pitch"] = 12;
    joint_table_["l_sho_pitch"] = 13;
    // joint_table_["head_pan"   ] = 14;

    target_position_      = Eigen::MatrixXd::Zero(1, result.size());
    goal_position_      = Eigen::MatrixXd::Zero(1, result.size());
    init_position_      = Eigen::MatrixXd::Zero(1, result.size());
    joint_axis_direction_      = Eigen::MatrixXi::Zero(1, result.size());
}

WalkingMotionModule::~WalkingMotionModule()
{
    queue_thread_.join();
}

void WalkingMotionModule::Initialize(const int control_cycle_msec, Robot *robot)
{
    queue_thread_ = boost::thread(boost::bind(&WalkingMotionModule::QueueThread, this));
    control_cycle_msec_ = control_cycle_msec;

    // m, s, rad
    // init pose
    walking_param_.init_x_offset = -0.010;
    walking_param_.init_y_offset = 0.005;
    walking_param_.init_z_offset = 0.020;
    walking_param_.init_roll_offset = 0.0;
    walking_param_.init_pitch_offset = 0.0 * deg2rad;
    walking_param_.init_yaw_offset = 0.0 * deg2rad;
    walking_param_.hip_pitch_offset = 13.0 * deg2rad;
    // time
    walking_param_.period_time = 600 * 0.001;
    walking_param_.dsp_ratio = 0.1;
    walking_param_.step_fb_ratio = 0.28;
    // walking
    walking_param_.x_move_amplitude = 0.0;
    walking_param_.y_move_amplitude = 0.0;
    walking_param_.z_move_amplitude = 0.040;    // foot height
    walking_param_.angle_move_amplitude = 0.0;
    // walking_param_.move_aim_on = false;
    // balance
    walking_param_.balance_enable = false;
    walking_param_.balance_hip_roll_gain = 0.5;
    walking_param_.balance_knee_gain = 0.3;
    walking_param_.balance_ankle_roll_gain = 1.0;
    walking_param_.balance_ankle_pitch_gain = 0.9;
    walking_param_.y_swap_amplitude = 0.020;
    walking_param_.z_swap_amplitude = 0.005;
    walking_param_.pelvis_offset = 3.0 * deg2rad;
    walking_param_.arm_swing_gain = 1.5;

    // member variable
    m_Body_Swing_Y = 0;
    m_Body_Swing_Z = 0;

    m_X_Swap_Phase_Shift = M_PI;
    m_X_Swap_Amplitude_Shift = 0;
    m_X_Move_Phase_Shift = M_PI / 2;
    m_X_Move_Amplitude_Shift = 0;
    m_Y_Swap_Phase_Shift = 0;
    m_Y_Swap_Amplitude_Shift = 0;
    m_Y_Move_Phase_Shift = M_PI / 2;
    m_Z_Swap_Phase_Shift = M_PI * 3 / 2;
    m_Z_Move_Phase_Shift = M_PI / 2;
    m_A_Move_Phase_Shift = M_PI / 2;

    m_Ctrl_Running = false;
    m_Real_Running = false;
    m_Time = 0;

    //                     R_HIP_YAW, R_HIP_ROLL, R_HIP_PITCH, R_KNEE, R_ANKLE_PITCH, R_ANKLE_ROLL, L_HIP_YAW, L_HIP_ROLL, L_HIP_PITCH, L_KNEE, L_ANKLE_PITCH, L_ANKLE_ROLL, R_ARM_SWING, L_ARM_SWING
    joint_axis_direction_   <<    -1,        -1,          1,         1,         -1,            1,          -1,        -1,         -1,         -1,         1,            1,           1,           -1;
    init_position_          <<   0.0,       0.0,        0.0,       0.0,        0.0,          0.0,         0.0,       0.0,        0.0,        0.0,       0.0,          0.0,       -38.0,         38.0;
    init_position_ *= deg2rad;

    ros::NodeHandle _ros_node;

    std::string _path = ros::package::getPath("op3_walking_module") + "/config/param.yaml";
    _ros_node.param<std::string>("walking_param_path", param_path_, _path);

    loadWalkingParam(param_path_);

    updateTimeParam();
    updateMovementParam();
}

void    WalkingMotionModule::QueueThread()
{
    ros::NodeHandle     _ros_node;
    ros::CallbackQueue  _callback_queue;

    _ros_node.setCallbackQueue(&_callback_queue);

    /* publish topics */
    status_msg_pub_ = _ros_node.advertise<robotis_controller_msgs::StatusMsg>("robotis/status", 1);


    /* ROS Service Callback Functions */
    ros::ServiceServer get_walking_param_server = _ros_node.advertiseService("/robotis/walking/get_params", &WalkingMotionModule::getWalkigParameterCallback, this);

    /* sensor topic subscribe */
    ros::Subscriber _imu_data_sub = _ros_node.subscribe("/robotis/sensor/imu/imu", 0, &WalkingMotionModule::IMUDataOutputCallback, this);
    ros::Subscriber _walking_command_sub = _ros_node.subscribe("/robotis/walking/command", 0, &WalkingMotionModule::walkingCommandCallback, this);
    ros::Subscriber _walking_param_sub = _ros_node.subscribe("/robotis/walking/set_params", 0, &WalkingMotionModule::walkingParameterCallback, this);

    while(_ros_node.ok())
    {
        _callback_queue.callAvailable();

        usleep(100);
    }
}

void    WalkingMotionModule::PublishRobotPose(void)
{
    /*
    publish_mutex_.lock();
    robot_pose_msg_.global_to_center_of_body.position.x = desired_matrix_g_to_cob_.coeff(0, 3);
    robot_pose_msg_.global_to_center_of_body.position.y = desired_matrix_g_to_cob_.coeff(1, 3);
    robot_pose_msg_.global_to_center_of_body.position.z = desired_matrix_g_to_cob_.coeff(2, 3);
    Eigen::Quaterniond quaterniond_g_to_cob(desired_matrix_g_to_cob_.block<3, 3>(0, 0));


    robot_pose_msg_.global_to_right_foot.position.x = desired_matrix_g_to_rfoot_.coeff(0, 3);
    robot_pose_msg_.global_to_right_foot.position.y = desired_matrix_g_to_rfoot_.coeff(1, 3);
    robot_pose_msg_.global_to_right_foot.position.z = desired_matrix_g_to_rfoot_.coeff(2, 3);
    Eigen::Quaterniond quaterniond_g_to_rf(desired_matrix_g_to_rfoot_.block<3, 3>(0, 0));

    robot_pose_msg_.global_to_left_foot.position.x = desired_matrix_g_to_lfoot_.coeff(0, 3);
    robot_pose_msg_.global_to_left_foot.position.y = desired_matrix_g_to_lfoot_.coeff(1, 3);
    robot_pose_msg_.global_to_left_foot.position.z = desired_matrix_g_to_lfoot_.coeff(2, 3);
    Eigen::Quaterniond quaterniond_g_to_lf(desired_matrix_g_to_lfoot_.block<3, 3>(0, 0));
    publish_mutex_.unlock();

    tf::quaternionEigenToMsg(quaterniond_g_to_cob, robot_pose_msg_.global_to_center_of_body.orientation);
    tf::quaternionEigenToMsg(quaterniond_g_to_rf,  robot_pose_msg_.global_to_right_foot.orientation);
    tf::quaternionEigenToMsg(quaterniond_g_to_lf,  robot_pose_msg_.global_to_left_foot.orientation);

    robot_pose_pub_.publish(robot_pose_msg_);
     */
}


void    WalkingMotionModule::PublishStatusMsg(unsigned int type, std::string msg)
{
    robotis_controller_msgs::StatusMsg _status;
    _status.header.stamp = ros::Time::now();
    _status.type = type;
    _status.module_name = "Walking";
    _status.status_msg = msg;

    status_msg_pub_.publish(_status);
}

void WalkingMotionModule::IMUDataOutputCallback(const sensor_msgs::Imu::ConstPtr &msg)
{
    // PreviewControlWalkingMotionModule::GetInstance()->current_gyro_roll_rad_per_sec  = -1.0*(msg->angular_velocity.x);
    // PreviewControlWalkingMotionModule::GetInstance()->current_gyro_pitch_rad_per_sec = -1.0*(msg->angular_velocity.y);


    Eigen::Quaterniond imu_quat;
    tf::quaternionMsgToEigen(msg->orientation, imu_quat);

    Eigen::MatrixXd imu_mat = (RX_PI_3x3*(imu_quat.toRotationMatrix()))*RZ_PI_3x3;

    double roll  = atan2( imu_mat.coeff(2,1), imu_mat.coeff(2,2));
    double pitch = atan2(-imu_mat.coeff(2,0), sqrt(powDI(imu_mat.coeff(2,1), 2) + powDI(imu_mat.coeff(2,2), 2)));
    double yaw   = atan2( imu_mat.coeff(1,0), imu_mat.coeff(0,0));

    // PreviewControlWalkingMotionModule::GetInstance()->current_imu_roll_rad = roll;
    // PreviewControlWalkingMotionModule::GetInstance()->current_imu_pitch_rad = pitch;
}

void WalkingMotionModule::walkingCommandCallback(const std_msgs::String::ConstPtr &msg)
{
    if(msg->data == "start")
        startWalking();
    else if(msg->data == "stop")
        Stop();
    else if(msg->data == "balance on")
        walking_param_.balance_enable = true;
    else if(msg->data == "balance off")
        walking_param_.balance_enable = false;
    else if(msg->data == "save")
        saveWalkingParam(param_path_);
}

void WalkingMotionModule::walkingParameterCallback(const op3_walking_module_msgs::WalkingParam::ConstPtr &msg)
{
    walking_param_ = *msg;
}

bool WalkingMotionModule::getWalkigParameterCallback(op3_walking_module_msgs::GetWalkingParam::Request &req, op3_walking_module_msgs::GetWalkingParam::Response &res)
{
    res.parameters = walking_param_;

    return true;
}

double WalkingMotionModule::wSin(double time, double period, double period_shift, double mag, double mag_shift)
{
    return mag * sin(2 * M_PI / period * time - period_shift) + mag_shift;
}

// m, rad
bool WalkingMotionModule::computeIK(double *out, double pos_x, double pos_y, double pos_z, double ori_roll, double ori_pitch, double ori_yaw)
{
    double THIGH_LENGTH = 93.0 * 0.001; //m
    double CALF_LENGTH = 93.0 * 0.001; //m
    double ANKLE_LENGTH = 33.5 * 0.001; //m
    double LEG_LENGTH = 219.5 * 0.001; //m (THIGH_LENGTH + CALF_LENGTH + ANKLE_LENGTH)

    Eigen::MatrixXd Tad, Tda, Tcd, Tdc, Tac;
    Eigen::Vector3d vec;
    double _Rac, _Acos, _Atan, _k, _l, _m, _n, _s, _c, _theta;

    // make transform matrix
    // Tad.SetTransform(Point3D(pos_x, pos_y, pos_z - LEG_LENGTH), Vector3D(ori_roll * 180.0 / M_PI, ori_pitch * 180.0 / PI, ori_yaw * 180.0 / PI));
    // Tad = transformationXYZRPY(pos_x, pos_y, pos_z - LEG_LENGTH, ori_roll, ori_pitch, ori_yaw);
    Tad = transformationXYZRPY(pos_x, pos_y, pos_z, ori_roll, ori_pitch, ori_yaw);

    vec << pos_x + Tad.coeff(0, 2) * ANKLE_LENGTH
            , pos_y + Tad.coeff(1, 2) * ANKLE_LENGTH
            , (pos_z - LEG_LENGTH) + Tad.coeff(2, 2) * ANKLE_LENGTH;

    // Get Knee
    _Rac = vec.norm();
    _Acos = acos((_Rac * _Rac - THIGH_LENGTH * THIGH_LENGTH - CALF_LENGTH * CALF_LENGTH) / (2 * THIGH_LENGTH * CALF_LENGTH));
    if(std::isnan(_Acos) == 1)
    {
        std::cout << "fail ik - 1" << std::endl;
        return false;
    }
    *(out + 3) = _Acos;

    // Get Ankle Roll
    Tda = InverseTransformation(Tad);
    // if(Tda.Inverse() == false)
    //     return false;
    // _k = sqrt(Tda.m[7] * Tda.m[7] + Tda.m[11] * Tda.m[11]);
    // _l = sqrt(Tda.m[7] * Tda.m[7] + (Tda.m[11] - ANKLE_LENGTH) * (Tda.m[11] - ANKLE_LENGTH));
    double _tda_y = Tda.coeff(1, 3);
    double _tda_z = Tda.coeff(2, 3);
    _k = sqrt(_tda_y * _tda_y + _tda_z * _tda_z);
    _l = sqrt(_tda_y * _tda_y + (_tda_z - ANKLE_LENGTH) * (_tda_z - ANKLE_LENGTH));
    _m = (_k * _k - _l * _l - ANKLE_LENGTH * ANKLE_LENGTH) / (2 * _l * ANKLE_LENGTH);
    if(_m > 1.0)
        _m = 1.0;
    else if(_m < -1.0)
        _m = -1.0;
    _Acos = acos(_m);
    if(std::isnan(_Acos) == 1)
    {
        std::cout << "fail ik - 2" << std::endl;
        return false;
    }
    if(_tda_y < 0.0)
        *(out + 5) = -_Acos;
    else
        *(out + 5) = _Acos;

    // Get Hip Yaw
    // Tcd.SetTransform(Point3D(0, 0, -ANKLE_LENGTH), Vector3D(*(out + 5) * 180.0 / PI, 0, 0));
    Tcd = transformationXYZRPY(0.0, 0.0, - ANKLE_LENGTH, *(out + 5), 0.0, 0.0);
    //Tdc = Tcd;
    //if(Tdc.Inverse() == false)
    //    return false;
    Tdc = InverseTransformation(Tcd);
    Tac = Tad * Tdc;
    // _Atan = atan2(-Tac.m[1] , Tac.m[5]);
    _Atan = atan2(-Tac.coeff(0, 1) , Tac.coeff(1, 1));
    if(std::isinf(_Atan) == 1)
    {
        std::cout << "fail ik - 3" << std::endl;
        return false;
    }
    *(out) = _Atan;

    // Get Hip Roll
    // _Atan = atan2(Tac.m[9], -Tac.m[1] * sin(*(out)) + Tac.m[5] * cos(*(out)));
    _Atan = atan2(Tac.coeff(2, 1), -Tac.coeff(0, 1) * sin(*(out)) + Tac.coeff(1, 1) * cos(*(out)));
    if(std::isinf(_Atan) == 1)
    {
        std::cout << "fail ik - 4" << std::endl;
        return false;
    }
    *(out + 1) = _Atan;

    // Get Hip Pitch and Ankle Pitch
    // _Atan = atan2(Tac.m[2] * cos(*(out)) + Tac.m[6] * sin(*(out)), Tac.m[0] * cos(*(out)) + Tac.m[4] * sin(*(out)));
    _Atan = atan2(Tac.coeff(0, 2) * cos(*(out)) + Tac.coeff(1, 2) * sin(*(out)), Tac.coeff(0, 0) * cos(*(out)) + Tac.coeff(1, 0) * sin(*(out)));
    if(std::isinf(_Atan) == 1)
    {
        std::cout << "fail ik - 5" << std::endl;
        return false;
    }
    _theta = _Atan;
    _k = sin(*(out + 3)) * CALF_LENGTH;
    _l = -THIGH_LENGTH - cos(*(out + 3)) * CALF_LENGTH;
    _m = cos(*(out)) * vec.x() + sin(*(out)) * vec.y();
    _n = cos(*(out + 1)) * vec.z() + sin(*(out)) * sin(*(out + 1)) * vec.x() - cos(*(out)) * sin(*(out + 1)) * vec.y();
    _s = (_k * _n + _l * _m) / (_k * _k + _l * _l);
    _c = (_n - _k * _s) / _l;
    _Atan = atan2(_s, _c);
    if(std::isinf(_Atan) == 1)
    {
        std::cout << "fail ik - 6" << std::endl;
        return false;
    }
    *(out + 2) = _Atan;
    *(out + 4) = _theta - *(out + 3) - *(out + 2);

    return true;
}

void WalkingMotionModule::updateTimeParam()
{
    m_PeriodTime = walking_param_.period_time; // * 1000;   // s -> ms
    m_DSP_Ratio = walking_param_.dsp_ratio;
    m_SSP_Ratio = 1 - m_DSP_Ratio;

    m_X_Swap_PeriodTime = m_PeriodTime / 2;
    m_X_Move_PeriodTime = m_PeriodTime * m_SSP_Ratio;
    m_Y_Swap_PeriodTime = m_PeriodTime;
    m_Y_Move_PeriodTime = m_PeriodTime * m_SSP_Ratio;
    m_Z_Swap_PeriodTime = m_PeriodTime / 2;
    m_Z_Move_PeriodTime = m_PeriodTime * m_SSP_Ratio / 2;
    m_A_Move_PeriodTime = m_PeriodTime * m_SSP_Ratio;

    m_SSP_Time = m_PeriodTime * m_SSP_Ratio;
    m_SSP_Time_Start_L = (1 - m_SSP_Ratio) * m_PeriodTime / 4;
    m_SSP_Time_End_L = (1 + m_SSP_Ratio) * m_PeriodTime / 4;
    m_SSP_Time_Start_R = (3 - m_SSP_Ratio) * m_PeriodTime / 4;
    m_SSP_Time_End_R = (3 + m_SSP_Ratio) * m_PeriodTime / 4;

    m_Phase_Time1 = (m_SSP_Time_Start_L + m_SSP_Time_End_L) / 2;
    m_Phase_Time2 = (m_SSP_Time_End_L + m_SSP_Time_Start_R) / 2;
    m_Phase_Time3 = (m_SSP_Time_Start_R + m_SSP_Time_End_R) / 2;

    // m_Pelvis_Offset = PELVIS_OFFSET*MX28::RATIO_ANGLE2VALUE;
    m_Pelvis_Offset = walking_param_.pelvis_offset;
    m_Pelvis_Swing = m_Pelvis_Offset * 0.35;
    m_Arm_Swing_Gain = walking_param_.arm_swing_gain;
}

void WalkingMotionModule::updateMovementParam()
{
    // Forward/Back
    m_X_Move_Amplitude = walking_param_.x_move_amplitude;
    m_X_Swap_Amplitude = walking_param_.x_move_amplitude * walking_param_.step_fb_ratio;

    // Right/Left
    m_Y_Move_Amplitude = walking_param_.y_move_amplitude / 2;
    if(m_Y_Move_Amplitude > 0)
        m_Y_Move_Amplitude_Shift = m_Y_Move_Amplitude;
    else
        m_Y_Move_Amplitude_Shift = -m_Y_Move_Amplitude;
    m_Y_Swap_Amplitude = walking_param_.y_swap_amplitude + m_Y_Move_Amplitude_Shift * 0.04;

    m_Z_Move_Amplitude = walking_param_.z_move_amplitude / 2;
    m_Z_Move_Amplitude_Shift = m_Z_Move_Amplitude / 2;
    m_Z_Swap_Amplitude = walking_param_.z_swap_amplitude;
    m_Z_Swap_Amplitude_Shift = m_Z_Swap_Amplitude;

    // Direction
    if(walking_param_.move_aim_on == false)
    {
        m_A_Move_Amplitude = walking_param_.angle_move_amplitude / 2;
        if(m_A_Move_Amplitude > 0)
            m_A_Move_Amplitude_Shift = m_A_Move_Amplitude;
        else
            m_A_Move_Amplitude_Shift = -m_A_Move_Amplitude;
    }
    else
    {
        m_A_Move_Amplitude = -walking_param_.angle_move_amplitude / 2;
        if(m_A_Move_Amplitude > 0)
            m_A_Move_Amplitude_Shift = -m_A_Move_Amplitude;
        else
            m_A_Move_Amplitude_Shift = m_A_Move_Amplitude;
    }
}

void WalkingMotionModule::updatePoseParam()
{
    m_X_Offset = walking_param_.init_x_offset;
    m_Y_Offset = walking_param_.init_y_offset;
    m_Z_Offset = walking_param_.init_z_offset;
    m_R_Offset = walking_param_.init_roll_offset;
    m_P_Offset = walking_param_.init_pitch_offset;
    m_A_Offset = walking_param_.init_yaw_offset;
    m_Hip_Pitch_Offset = walking_param_.hip_pitch_offset;
}

void WalkingMotionModule::startWalking()
{
    m_Ctrl_Running = true;
    m_Real_Running = true;

    PublishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_INFO, "Start walking");
}

void WalkingMotionModule::Stop()
{
    m_Ctrl_Running = false;
    PublishStatusMsg(robotis_controller_msgs::StatusMsg::STATUS_INFO, "Stop walking");
}

bool WalkingMotionModule::IsRunning()
{
    return m_Real_Running || (walking_state_ == WalkingInitPose);
}

// default [angle : radian, length : m]
void WalkingMotionModule::Process(std::map<std::string, Dynamixel *> dxls, std::map<std::string, double> sensors)
{
    if(enable == false)
        return;

    //    ros::Time _start_time = ros::Time::now();

    const double _time_unit = control_cycle_msec_ * 0.001;  // ms -> s
    int _joint_size = result.size();
    double _angle[_joint_size];
    double _balance_angle[_joint_size];

    if(walking_state_ == WalkingInitPose)
    {
        int _total_count = calc_joint_tra_.rows();
        for ( int id = 1; id <= result.size(); id++ )
            target_position_.coeffRef(0, id) = calc_joint_tra_( init_count_ , id );

        init_count_ += 1;
        if(init_count_ >= _total_count)
        {
            walking_state_ = WalkingReady;
            if(DEBUG) std::cout << "End moving : " << init_count_ << std::endl;
        }

    }
    else if(walking_state_ == WalkingReady || walking_state_ == WalkingEnable)
    {
        // present angle
        for(std::map<std::string, DynamixelState *>::iterator state_iter = result.begin(); state_iter != result.end(); state_iter++)
        {
            std::string _joint_name = state_iter->first;
            int _index = joint_table_[_joint_name];

            Dynamixel *_dxl = NULL;
            std::map<std::string, Dynamixel*>::iterator _dxl_it = dxls.find(_joint_name);
            if(_dxl_it != dxls.end())
                _dxl = _dxl_it->second;
            else
                continue;

            goal_position_.coeffRef(0, _index)      = _dxl->dxl_state->goal_position;
        }

        processPhase(_time_unit);

        bool _get_angle = false;
        _get_angle = computeLegAngle(&_angle[0]);

        computeArmAngle(&_angle[12]);

        double rlGyroErr = sensors["gyro_x"] * deg2rad;
        double fbGyroErr = sensors["gyro_y"] * deg2rad;

        sensoryFeedback(rlGyroErr, fbGyroErr, _balance_angle);

        double _err_total = 0.0, _err_max = 0.0;
        // set goal position
        for(int idx = 0; idx < 14; idx++)
        {
            double _goal_position = 0.0;
            if(_get_angle == false && idx < 12)
                _goal_position =  goal_position_.coeff(0, idx);
            else
                _goal_position =  init_position_.coeff(0, idx) + _angle[idx] + _balance_angle[idx];

            target_position_.coeffRef(0, idx) = _goal_position;

            double _err = fabs(target_position_.coeff(0, idx) - goal_position_.coeff(0, idx)) * rad2deg;
            if(_err > _err_max) _err_max = _err;
            _err_total += _err;
        }
        // head joint
        //goal_position_.coeffRef(0, joint_table_["head_pan"]) = A_MOVE_AMPLITUDE;

        // Check Enable
        if(walking_state_ == WalkingEnable && _err_total > 5.0)
        {
            if(DEBUG) std::cout << "Check Err : " << _err_max << std::endl;
            // make trajecotry for init pose
            int _mov_time = _err_max / 30;
            IniposeTraGene(_mov_time < 1 ? 1 : _mov_time);

            // set target to goal
            target_position_ = goal_position_;
        }
        else
        {
            walking_state_ = WalkingReady;
        }
    }

    // set result
    for(std::map<std::string, DynamixelState *>::iterator state_iter = result.begin(); state_iter != result.end(); state_iter++)
    {
        std::string _joint_name = state_iter->first;
        int _index = joint_table_[_joint_name];

        result[_joint_name]->goal_position = target_position_.coeff(0, _index);
    }

    // Todo : pid gain
    // for(int id = JointData::ID_R_HIP_YAW; id <= JointData::ID_L_ANKLE_ROLL; id++)
    // {
    //     m_Joint.SetPGain(id, P_GAIN);
    //     m_Joint.SetIGain(id, I_GAIN);
    //     m_Joint.SetDGain(id, D_GAIN);
    // }

    // time
    if(m_Real_Running == true)
    {
        // m_Time += control_cycle_msec_;
        m_Time += _time_unit;
        if(m_Time >= m_PeriodTime)
            m_Time = 0;
    }

    //    ros::Duration _dur = ros::Time::now() - _start_time;
    //    double _msec = _dur.sec * 1000 + _dur.nsec * 0.000001;
    //    std::cout << "Walking Process Time : " << _msec << std::endl;
}

void WalkingMotionModule::processPhase(const double &time_unit)
{
    // Update walk parameters
    if(m_Time == 0)
    {
        updateTimeParam();
        m_Phase = PHASE0;
        if(m_Ctrl_Running == false)
        {
            if(m_X_Move_Amplitude == 0 && m_Y_Move_Amplitude == 0 && m_A_Move_Amplitude == 0)
            {
                m_Real_Running = false;
            }
            else
            {
                // init walking param
                walking_param_.x_move_amplitude = 0;
                walking_param_.y_move_amplitude = 0;
                walking_param_.angle_move_amplitude = 0;
            }
        }
    }
    else if(m_Time >= (m_Phase_Time1 - time_unit/2) && m_Time < (m_Phase_Time1 + time_unit/2))  // the position of left foot is the highest.
    {
        updateMovementParam();
        m_Phase = PHASE1;
    }
    else if(m_Time >= (m_Phase_Time2 - time_unit/2) && m_Time < (m_Phase_Time2 + time_unit/2))  // middle of double support state
    {
        updateTimeParam();

        m_Time = m_Phase_Time2;
        m_Phase = PHASE2;
        if(m_Ctrl_Running == false)
        {
            if(m_X_Move_Amplitude == 0 && m_Y_Move_Amplitude == 0 && m_A_Move_Amplitude == 0)
            {
                m_Real_Running = false;
            }
            else
            {
                // init walking param
                walking_param_.x_move_amplitude = 0;
                walking_param_.y_move_amplitude = 0;
                walking_param_.angle_move_amplitude = 0;
            }
        }
    }
    else if(m_Time >= (m_Phase_Time3 - time_unit/2) && m_Time < (m_Phase_Time3 + time_unit/2))  // the position of right foot is the highest.
    {
        updateMovementParam();
        m_Phase = PHASE3;
    }
}

bool WalkingMotionModule::computeLegAngle(double *leg_angle)
{
    Pose3D _swap, _right_leg_move, _left_leg_move;
    double _pelvis_offset_r, _pelvis_offset_l;
    double ep[12];

    updatePoseParam();

    // Compute endpoints
    _swap.x = wSin(m_Time, m_X_Swap_PeriodTime, m_X_Swap_Phase_Shift, m_X_Swap_Amplitude, m_X_Swap_Amplitude_Shift);
    _swap.y = wSin(m_Time, m_Y_Swap_PeriodTime, m_Y_Swap_Phase_Shift, m_Y_Swap_Amplitude, m_Y_Swap_Amplitude_Shift);
    _swap.z = wSin(m_Time, m_Z_Swap_PeriodTime, m_Z_Swap_Phase_Shift, m_Z_Swap_Amplitude, m_Z_Swap_Amplitude_Shift);
    _swap.roll = 0.0;
    _swap.pitch = 0.0;
    _swap.yaw = 0.0;

    if(m_Time <= m_SSP_Time_Start_L)
    {
        _left_leg_move.x = wSin(m_SSP_Time_Start_L, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_L, m_X_Move_Amplitude, m_X_Move_Amplitude_Shift);
        _left_leg_move.y = wSin(m_SSP_Time_Start_L, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_L, m_Y_Move_Amplitude, m_Y_Move_Amplitude_Shift);
        _left_leg_move.z = wSin(m_SSP_Time_Start_L, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_L, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _left_leg_move.yaw = wSin(m_SSP_Time_Start_L, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_L, m_A_Move_Amplitude, m_A_Move_Amplitude_Shift);
        _right_leg_move.x = wSin(m_SSP_Time_Start_L, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_L, -m_X_Move_Amplitude, -m_X_Move_Amplitude_Shift);
        _right_leg_move.y = wSin(m_SSP_Time_Start_L, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_L, -m_Y_Move_Amplitude, -m_Y_Move_Amplitude_Shift);
        _right_leg_move.z = wSin(m_SSP_Time_Start_R, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_R, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _right_leg_move.yaw = wSin(m_SSP_Time_Start_L, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_L, -m_A_Move_Amplitude, -m_A_Move_Amplitude_Shift);
        _pelvis_offset_l = 0;
        _pelvis_offset_r = 0;
    }
    else if(m_Time <= m_SSP_Time_End_L)
    {
        _left_leg_move.x = wSin(m_Time, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_L, m_X_Move_Amplitude, m_X_Move_Amplitude_Shift);
        _left_leg_move.y = wSin(m_Time, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_L, m_Y_Move_Amplitude, m_Y_Move_Amplitude_Shift);
        _left_leg_move.z = wSin(m_Time, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_L, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _left_leg_move.yaw = wSin(m_Time, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_L, m_A_Move_Amplitude, m_A_Move_Amplitude_Shift);
        _right_leg_move.x = wSin(m_Time, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_L, -m_X_Move_Amplitude, -m_X_Move_Amplitude_Shift);
        _right_leg_move.y = wSin(m_Time, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_L, -m_Y_Move_Amplitude, -m_Y_Move_Amplitude_Shift);
        _right_leg_move.z = wSin(m_SSP_Time_Start_R, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_R, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _right_leg_move.yaw = wSin(m_Time, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_L, -m_A_Move_Amplitude, -m_A_Move_Amplitude_Shift);
        _pelvis_offset_l = wSin(m_Time, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_L, m_Pelvis_Swing / 2, m_Pelvis_Swing / 2);
        _pelvis_offset_r = wSin(m_Time, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_L, -m_Pelvis_Offset / 2, -m_Pelvis_Offset / 2);
    }
    else if(m_Time <= m_SSP_Time_Start_R)
    {
        _left_leg_move.x = wSin(m_SSP_Time_End_L, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_L, m_X_Move_Amplitude, m_X_Move_Amplitude_Shift);
        _left_leg_move.y = wSin(m_SSP_Time_End_L, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_L, m_Y_Move_Amplitude, m_Y_Move_Amplitude_Shift);
        _left_leg_move.z = wSin(m_SSP_Time_End_L, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_L, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _left_leg_move.yaw = wSin(m_SSP_Time_End_L, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_L, m_A_Move_Amplitude, m_A_Move_Amplitude_Shift);
        _right_leg_move.x = wSin(m_SSP_Time_End_L, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_L, -m_X_Move_Amplitude, -m_X_Move_Amplitude_Shift);
        _right_leg_move.y = wSin(m_SSP_Time_End_L, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_L, -m_Y_Move_Amplitude, -m_Y_Move_Amplitude_Shift);
        _right_leg_move.z = wSin(m_SSP_Time_Start_R, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_R, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _right_leg_move.yaw = wSin(m_SSP_Time_End_L, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_L, -m_A_Move_Amplitude, -m_A_Move_Amplitude_Shift);
        _pelvis_offset_l = 0;
        _pelvis_offset_r = 0;
    }
    else if(m_Time <= m_SSP_Time_End_R)
    {
        _left_leg_move.x = wSin(m_Time, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, m_X_Move_Amplitude, m_X_Move_Amplitude_Shift);
        _left_leg_move.y = wSin(m_Time, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, m_Y_Move_Amplitude, m_Y_Move_Amplitude_Shift);
        _left_leg_move.z = wSin(m_SSP_Time_End_L, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_L, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _left_leg_move.yaw = wSin(m_Time, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, m_A_Move_Amplitude, m_A_Move_Amplitude_Shift);
        _right_leg_move.x = wSin(m_Time, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, -m_X_Move_Amplitude, -m_X_Move_Amplitude_Shift);
        _right_leg_move.y = wSin(m_Time, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, -m_Y_Move_Amplitude, -m_Y_Move_Amplitude_Shift);
        _right_leg_move.z = wSin(m_Time, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_R, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _right_leg_move.yaw = wSin(m_Time, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, -m_A_Move_Amplitude, -m_A_Move_Amplitude_Shift);
        _pelvis_offset_l = wSin(m_Time, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_R, m_Pelvis_Offset / 2, m_Pelvis_Offset / 2);
        _pelvis_offset_r = wSin(m_Time, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_R, -m_Pelvis_Swing / 2, -m_Pelvis_Swing / 2);
    }
    else
    {
        _left_leg_move.x = wSin(m_SSP_Time_End_R, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, m_X_Move_Amplitude, m_X_Move_Amplitude_Shift);
        _left_leg_move.y = wSin(m_SSP_Time_End_R, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, m_Y_Move_Amplitude, m_Y_Move_Amplitude_Shift);
        _left_leg_move.z = wSin(m_SSP_Time_End_L, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_L, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _left_leg_move.yaw = wSin(m_SSP_Time_End_R, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, m_A_Move_Amplitude, m_A_Move_Amplitude_Shift);
        _right_leg_move.x = wSin(m_SSP_Time_End_R, m_X_Move_PeriodTime, m_X_Move_Phase_Shift + 2 * M_PI / m_X_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, -m_X_Move_Amplitude, -m_X_Move_Amplitude_Shift);
        _right_leg_move.y = wSin(m_SSP_Time_End_R, m_Y_Move_PeriodTime, m_Y_Move_Phase_Shift + 2 * M_PI / m_Y_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, -m_Y_Move_Amplitude, -m_Y_Move_Amplitude_Shift);
        _right_leg_move.z = wSin(m_SSP_Time_End_R, m_Z_Move_PeriodTime, m_Z_Move_Phase_Shift + 2 * M_PI / m_Z_Move_PeriodTime * m_SSP_Time_Start_R, m_Z_Move_Amplitude, m_Z_Move_Amplitude_Shift);
        _right_leg_move.yaw = wSin(m_SSP_Time_End_R, m_A_Move_PeriodTime, m_A_Move_Phase_Shift + 2 * M_PI / m_A_Move_PeriodTime * m_SSP_Time_Start_R + M_PI, -m_A_Move_Amplitude, -m_A_Move_Amplitude_Shift);
        _pelvis_offset_l = 0;
        _pelvis_offset_r = 0;
    }

    _left_leg_move.roll = 0;
    _left_leg_move.pitch = 0;
    _right_leg_move.roll = 0;
    _right_leg_move.pitch = 0;

    double _leg_length = op3_kd_->thigh_length_m + op3_kd_->calf_length_m + op3_kd_->ankle_length_m;

    // mm, rad
    ep[0] = _swap.x + _right_leg_move.x + m_X_Offset;
    ep[1] = _swap.y + _right_leg_move.y - m_Y_Offset / 2;
    ep[2] = _swap.z + _right_leg_move.z + m_Z_Offset - _leg_length;
    ep[3] = _swap.roll + _right_leg_move.roll - m_R_Offset / 2;
    ep[4] = _swap.pitch + _right_leg_move.pitch + m_P_Offset;
    ep[5] = _swap.yaw + _right_leg_move.yaw - m_A_Offset / 2;
    ep[6] = _swap.x + _left_leg_move.x + m_X_Offset;
    ep[7] = _swap.y + _left_leg_move.y + m_Y_Offset / 2;
    ep[8] = _swap.z + _left_leg_move.z + m_Z_Offset - _leg_length;
    ep[9] = _swap.roll + _left_leg_move.roll + m_R_Offset / 2;
    ep[10] = _swap.pitch + _left_leg_move.pitch + m_P_Offset;
    ep[11] = _swap.yaw + _left_leg_move.yaw + m_A_Offset / 2;

    // Compute body swing
    if(m_Time <= m_SSP_Time_End_L)
    {
        m_Body_Swing_Y = -ep[7];
        m_Body_Swing_Z = ep[8];
    }
    else
    {
        m_Body_Swing_Y = -ep[1];
        m_Body_Swing_Z = ep[2];
    }
    m_Body_Swing_Z -= _leg_length;


    // right leg
    if(op3_kd_->InverseKinematicsforRightLeg(&leg_angle[0], ep[0], ep[1], ep[2], ep[3], ep[4], ep[5]) == false)
    {
        printf("IK not Solved EPR : %f %f %f %f %f %f\n", ep[0], ep[1], ep[2], ep[3], ep[4], ep[5]);
        return false;
    }

    if(op3_kd_->InverseKinematicsforLeftLeg(&leg_angle[6], ep[6], ep[7], ep[8], ep[9], ep[10], ep[11]) == false)
    {
        printf("IK not Solved EPL : %f %f %f %f %f %f\n", ep[6], ep[7], ep[8], ep[9], ep[10], ep[11]);
        return false;
    }
    // printf("EPR[R] : %f %f %f %f %f %f\n", ep[0], ep[1], ep[2], ep[3], ep[4], ep[5]);
    // printf("EPR[L] : %f %f %f %f %f %f\n", ep[6], ep[7], ep[8], ep[9], ep[10], ep[11]);

    /*
    // Compute angles
    if(computeIK(&angle[0], ep[0], ep[1], ep[2], ep[3], ep[4], ep[5]) == false)
    {
        std::cout << "fail ik - right" << std::endl;
        printf("IK not Solved EPR[R] : %f %f %f %f %f %f\n", ep[0], ep[1], ep[2], ep[3], ep[4], ep[5]);
        printf("Result : %f %f %f %f %f %f\n", angle[0], angle[1], angle[2], angle[3], angle[4], angle[5]);
        return;
    }
    else
        printf("Result[R 1] : %f %f %f %f %f %f\n", angle[0], angle[1], angle[2], angle[3], angle[4], angle[5]);

    if (computeIK(&angle[6], ep[6], ep[7], ep[8], ep[9], ep[10], ep[11]) == false)
    {
        std::cout << "fail ik" << std::endl;
        printf("IK not Solved EPL[L] : %f %f %f %f %f %f\n", ep[6], ep[7], ep[8], ep[9], ep[10], ep[11]);
        printf("Result : %f %f %f %f %f %f\n", angle[6], angle[7], angle[8], angle[9], angle[10], angle[11]);
        return;
    }
    else
        printf("Result[L] : %f %f %f %f %f %f\n", angle[6], angle[7], angle[8], angle[9], angle[10], angle[11]);

     */

    // Compute dxls angle
    for(int i = 0; i < 12; i++)
    {
        // offset : rad
        // offset = (double)dir[i] * angle[i];
        double _offset = 0;

        if(i == joint_table_["r_hip_roll"]) // R_HIP_ROLL
            _offset += joint_axis_direction_(0, i) * _pelvis_offset_r;
        else if(i == joint_table_["l_hip_roll"]) // L_HIP_ROLL
            _offset += joint_axis_direction_(0, i) * _pelvis_offset_l;
        else if(i == joint_table_["r_hip_pitch"] || i == joint_table_["l_hip_pitch"]) // R_HIP_PITCH or L_HIP_PITCH
            _offset -= joint_axis_direction_(0, i) * m_Hip_Pitch_Offset;

        leg_angle[i] += _offset;
    }

    return true;
}

void WalkingMotionModule::computeArmAngle(double *arm_angle)
{
    // Compute arm swing
    if(m_X_Move_Amplitude == 0)
    {
        arm_angle[0] = 0; // Right
        arm_angle[1] = 0; // Left
    }
    else
    {
        arm_angle[0] = wSin(m_Time, m_PeriodTime, M_PI * 1.5, -m_X_Move_Amplitude * m_Arm_Swing_Gain * 1000, 0)
                                                                                        * joint_axis_direction_(0, joint_table_["r_sho_pitch"]) * deg2rad;
        arm_angle[1] = wSin(m_Time, m_PeriodTime, M_PI * 1.5, m_X_Move_Amplitude * m_Arm_Swing_Gain * 1000, 0)
                                                                                        * joint_axis_direction_(0, joint_table_["l_sho_pitch"]) * deg2rad;
    }
}

void WalkingMotionModule::sensoryFeedback(const double &rlGyroErr, const double &fbGyroErr, double *balance_angle)
{
    // adjust balance offset
    if(walking_param_.balance_enable == false)
        return;

    balance_angle[joint_table_["r_hip_roll"]] += (joint_axis_direction_.coeff(0, joint_table_["r_hip_roll"]) * rlGyroErr * walking_param_.balance_hip_roll_gain); // R_HIP_ROLL
    balance_angle[joint_table_["l_hip_roll"]] += (joint_axis_direction_.coeff(0, joint_table_["l_hip_roll"]) * rlGyroErr * walking_param_.balance_hip_roll_gain); // L_HIP_ROLL

    balance_angle[joint_table_["r_knee"]] -= (joint_axis_direction_.coeff(0, joint_table_["r_knee"]) * fbGyroErr * walking_param_.balance_knee_gain); // R_KNEE
    balance_angle[joint_table_["l_knee"]] -= (joint_axis_direction_.coeff(0, joint_table_["l_knee"]) * fbGyroErr * walking_param_.balance_knee_gain); // L_KNEE

    balance_angle[joint_table_["r_ank_pitch"]] -= (joint_axis_direction_.coeff(0, joint_table_["r_ank_pitch"]) * fbGyroErr * walking_param_.balance_ankle_pitch_gain); // R_ANKLE_PITCH
    balance_angle[joint_table_["l_ank_pitch"]] -= (joint_axis_direction_.coeff(0, joint_table_["l_ank_pitch"]) * fbGyroErr * walking_param_.balance_ankle_pitch_gain); // L_ANKLE_PITCH

    balance_angle[joint_table_["r_ank_roll"]] -= (joint_axis_direction_.coeff(0, joint_table_["r_ank_roll"]) * rlGyroErr * walking_param_.balance_ankle_roll_gain); // R_ANKLE_ROLL
    balance_angle[joint_table_["l_ank_roll"]] -= (joint_axis_direction_.coeff(0, joint_table_["l_ank_roll"]) * rlGyroErr * walking_param_.balance_ankle_roll_gain); // L_ANKLE_ROLL
}

void WalkingMotionModule::loadWalkingParam(const std::string &path)
{
    YAML::Node doc;
    try
    {
        // load yaml
        doc = YAML::LoadFile( path.c_str() );
    }
    catch(const std::exception& e)
    {
        ROS_ERROR("Fail to load yaml file.");
        return ;
    }

    // parse movement time
    walking_param_.init_x_offset              = doc["x_offset"].as<double>();
    walking_param_.init_y_offset              = doc["y_offset"].as<double>();
    walking_param_.init_z_offset              = doc["z_offset"].as<double>();
    walking_param_.init_roll_offset           = doc["roll_offset"].as<double>() * deg2rad;
    walking_param_.init_pitch_offset          = doc["pitch_offset"].as<double>() * deg2rad;
    walking_param_.init_yaw_offset            = doc["yaw_offset"].as<double>() * deg2rad;
    walking_param_.hip_pitch_offset           = doc["hip_pitch_offset"].as<double>() * deg2rad;
    // time
    walking_param_.period_time                = doc["period_time"].as<double>() * 0.001;    // ms -> s
    walking_param_.dsp_ratio                  = doc["dsp_ratio"].as<double>();
    walking_param_.step_fb_ratio              = doc["step_forward_back_ratio"].as<double>();
    // walking
    // walking_param_.x_move_amplitude
    // walking_param_.y_move_amplitude
    walking_param_.z_move_amplitude           = doc["foot_height"].as<double>();
    // walking_param_.angle_move_amplitude
    // walking_param_.move_aim_on
    // balance
    // walking_param_.balance_enable
    walking_param_.balance_hip_roll_gain      = doc["balance_hip_roll_gain"].as<double>();
    walking_param_.balance_knee_gain          = doc["balance_knee_gain"].as<double>();
    walking_param_.balance_ankle_roll_gain    = doc["balance_ankle_roll_gain"].as<double>();
    walking_param_.balance_ankle_pitch_gain   = doc["balance_ankle_pitch_gain"].as<double>();
    walking_param_.y_swap_amplitude           = doc["swing_right_left"].as<double>();
    walking_param_.z_swap_amplitude           = doc["swing_top_down"].as<double>();
    walking_param_.pelvis_offset              = doc["pelvis_offset"].as<double>() * deg2rad;
    walking_param_.arm_swing_gain             = doc["arm_swing_gain"].as<double>();

    // gain
    walking_param_.p_gain = doc["p_gain"].as<int>();
    walking_param_.i_gain = doc["i_gain"].as<int>();
    walking_param_.d_gain = doc["d_gain"].as<int>();
}

void WalkingMotionModule::saveWalkingParam(std::string &path)
{
    YAML::Emitter _out;

    _out << YAML::BeginMap;
    _out << YAML::Key << "x_offset"                  << YAML::Value << walking_param_.init_x_offset;
    _out << YAML::Key << "y_offset"                  << YAML::Value << walking_param_.init_y_offset;
    _out << YAML::Key << "z_offset"                  << YAML::Value << walking_param_.init_z_offset;
    _out << YAML::Key << "roll_offset"               << YAML::Value << walking_param_.init_roll_offset * rad2deg;
    _out << YAML::Key << "pitch_offset"              << YAML::Value << walking_param_.init_pitch_offset * rad2deg;
    _out << YAML::Key << "yaw_offset"                << YAML::Value << walking_param_.init_yaw_offset * rad2deg;
    _out << YAML::Key << "hip_pitch_offset"          << YAML::Value << walking_param_.hip_pitch_offset * rad2deg;
    _out << YAML::Key << "period_time"               << YAML::Value << walking_param_.period_time * 1000;
    _out << YAML::Key << "dsp_ratio"                 << YAML::Value << walking_param_.dsp_ratio;
    _out << YAML::Key << "step_forward_back_ratio"   << YAML::Value << walking_param_.step_fb_ratio;
    _out << YAML::Key << "foot_height"               << YAML::Value << walking_param_.z_move_amplitude;
    _out << YAML::Key << "swing_right_left"          << YAML::Value << walking_param_.y_swap_amplitude;
    _out << YAML::Key << "swing_top_down"            << YAML::Value << walking_param_.z_swap_amplitude;
    _out << YAML::Key << "pelvis_offset"             << YAML::Value << walking_param_.pelvis_offset * rad2deg;
    _out << YAML::Key << "arm_swing_gain"            << YAML::Value << walking_param_.arm_swing_gain;
    _out << YAML::Key << "balance_knee_gain"         << YAML::Value << walking_param_.balance_hip_roll_gain;
    _out << YAML::Key << "balance_ankle_pitch_gain"  << YAML::Value << walking_param_.balance_knee_gain;
    _out << YAML::Key << "balance_hip_roll_gain"     << YAML::Value << walking_param_.balance_ankle_roll_gain;
    _out << YAML::Key << "balance_ankle_roll_gain"   << YAML::Value << walking_param_.balance_ankle_pitch_gain;

    _out << YAML::Key << "p_gain"                    << YAML::Value << walking_param_.p_gain;
    _out << YAML::Key << "i_gain"                    << YAML::Value << walking_param_.i_gain;
    _out << YAML::Key << "d_gain"                    << YAML::Value << walking_param_.d_gain;
    _out << YAML::EndMap;

    // output to file
    std::ofstream fout(path.c_str());
    fout << _out.c_str();
}

void WalkingMotionModule::OnEnable()
{
    walking_state_ = WalkingEnable;
    ROS_INFO("Walking Enable");
}

void WalkingMotionModule::OnDisable()
{
    ROS_INFO("Walking Disable");
    walking_state_ = WalkingDisable;
}

void WalkingMotionModule::IniposeTraGene(double mov_time)
{
    double _smp_time = control_cycle_msec_ * 0.001;
    int _all_time_steps = int( mov_time / _smp_time ) + 1;
    calc_joint_tra_.resize(_all_time_steps , result.size() + 1 );

    for ( int id = 0; id <= result.size(); id++ )
    {
        double ini_value = goal_position_.coeff(0, id);
        double tar_value = target_position_.coeff(0, id);

        Eigen::MatrixXd tra;

        tra = minimum_jerk_tra( ini_value , 0.0 , 0.0 ,
                tar_value , 0.0 , 0.0 ,
                _smp_time , mov_time );

        calc_joint_tra_.block( 0 , id , _all_time_steps , 1 ) = tra;
    }

    std::cout << "Generate Trajecotry : " << mov_time << "s [" << _all_time_steps << "]" << std::endl;

    walking_state_ = WalkingInitPose;
    init_count_ = 0;
}
