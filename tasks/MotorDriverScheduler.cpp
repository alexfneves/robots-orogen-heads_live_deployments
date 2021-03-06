/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "MotorDriverScheduler.hpp"
#include <base-logging/Logging.hpp>
#include <canopen_master/StateMachine.hpp>

using namespace heads_live_deployments;

MotorDriverScheduler::MotorDriverScheduler(std::string const& name, TaskCore::TaskState initial_state)
    : MotorDriverSchedulerBase(name, initial_state)
{
    _resync_timeout = base::Time::fromMilliseconds(10);
}

MotorDriverScheduler::MotorDriverScheduler(std::string const& name, RTT::ExecutionEngine* engine, TaskCore::TaskState initial_state)
    : MotorDriverSchedulerBase(name, engine, initial_state)
{
    _resync_timeout = base::Time::fromMilliseconds(10);
}

MotorDriverScheduler::~MotorDriverScheduler()
{
}



/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See MotorDriverScheduler.hpp for more detailed
// documentation about them.

bool MotorDriverScheduler::configureHook()
{
    if (! MotorDriverSchedulerBase::configureHook())
        return false;

    mJointsSample.resize(1);
    mExportedJoints.resize(3);
    mExportedJoints.names[0] = "ship_and_heads::heads::joint_base_yaw";
    mExportedJoints.names[1] = "ship_and_heads::heads::joint_yaw_pitch";
    mExportedJoints.names[2] = "ship_and_heads::heads::joint_pitch_roll";
    return true;
}
bool MotorDriverScheduler::startHook()
{
    if (! MotorDriverSchedulerBase::startHook())
        return false;

    yaw_r_task   = getPeer("yaw_motor_r");
    pitch_r_task = getPeer("pitch_motor_r");
    roll_r_task  = getPeer("roll_motor_r");

    if (!yaw_r_task)
    {
        LOG_ERROR_S << "No yaw_motor_r peer" << std::endl;
        return false;
    }
    if (!pitch_r_task)
    {
        LOG_ERROR_S << "No pitch_motor_r peer" << std::endl;
        return false;
    }
    if (!roll_r_task)
    {
        LOG_ERROR_S << "No roll_motor_r peer" << std::endl;
        return false;
    }

    mReceivedJoints = RECEIVED_NONE;
    mPreviousSync = mLastSync = base::Time();
    mIgnoreCounter = 0;
    state(LOST_SYNC);
    mStats = MotorSyncStats();
    return true;
}

bool MotorDriverScheduler::updateJoint(
    RTT::InputPort<base::samples::Joints>& port,
    base::JointState& joint,
    base::Time syncTime)
{
    if (!port.connected())
    {
        joint = base::JointState();
        return true;
    }
    while (port.read(mJointsSample, false) == RTT::NewData)
    {
        if (mJointsSample.time > syncTime)
        {
            joint = mJointsSample.elements[0];
            return true;
        }
    }
    return false;
}

int MotorDriverScheduler::initialReceivedJoints() const
{
    int result = RECEIVED_NONE;
    if (!_yaw_joint.connected())
        result |= RECEIVED_YAW;
    if (!_pitch_joint.connected())
        result |= RECEIVED_PITCH;
    if (!_roll_joint.connected())
        result |= RECEIVED_ROLL;
    return result;
}

void MotorDriverScheduler::processTrigger()
{
    base::Time now = base::Time::now();
    auto sync_msg = canopen_master::StateMachine::sync();
    
    if (state() == LOST_SYNC)
    {
        mStats.lost_sync_periods++;
        mLastSync = now;
        _sync.write(sync_msg);
    }
    else if (!mLastSync.isNull())
    {
        if (now - mLastSync > _resync_timeout.get())
        {
            mStats.lost_sync_periods++;
            state(LOST_SYNC);
            _sync.write(sync_msg);
            mReceivedJoints = initialReceivedJoints();
        }
        else
        {
            mSkip.sync_time = now;
            mSkip.received_at = base::Time::now();
            mStats.skipped_sync++;
        }
    }
    else
    {
        mLastSync = now;
        _sync.write(sync_msg);
        mReceivedJoints = initialReceivedJoints();
    }

    mStats.time = base::Time::now();
    _stats.write(mStats);
}

void MotorDriverScheduler::updateHook()
{
    MotorDriverSchedulerBase::updateHook();

    yaw_r_task->update();
    pitch_r_task->update();
    roll_r_task->update();

    canbus::Message can_msg;

    // We ever only consider the latest message
    if (_imu_messages.connected())
    {
        base::samples::RigidBodyState rbs;
        if (_imu_messages.read(rbs, false) == RTT::NewData)
            processTrigger();
    }
    else if (_sync_messages.read(can_msg) == RTT::NewData)
        processTrigger();

    if (!(mReceivedJoints & RECEIVED_YAW))
    {
        if (updateJoint(_yaw_joint, mYawSample, mLastSync))
            mReceivedJoints |= RECEIVED_YAW;
    }

    if (!(mReceivedJoints & RECEIVED_PITCH))
    {
        if (updateJoint(_pitch_joint, mPitchSample, mLastSync))
            mReceivedJoints |= RECEIVED_PITCH;
    }

    if (!(mReceivedJoints & RECEIVED_ROLL))
    {
        if (updateJoint(_roll_joint, mRollSample, mLastSync))
            mReceivedJoints |= RECEIVED_ROLL;
    }

    if (mReceivedJoints == RECEIVED_ALL)
    {
        mReceivedJoints = RECEIVED_NONE;
        if (state() == LOST_SYNC)
        {
            usleep(5000);
            _yaw_joint.clear();
            _pitch_joint.clear();
            _roll_joint.clear();
            state(RUNNING);
            mIgnoreCounter = 1;
        }
        else if ((mIgnoreCounter == 0) || (mIgnoreCounter -= 1) == 0)
            outputJointState();

        if (!mSkip.sync_time.isNull())
        {
            mSkip.ready_at = base::Time::now();
            mSkip.sync_time = base::Time();
            _skipped_sync.write(mSkip);
        }
        
        mPreviousSync = mLastSync;
        mLastSync = base::Time();
    }
}

void MotorDriverScheduler::outputJointState()
{
    mExportedJoints.time = mPreviousSync;
    mExportedJoints.elements[0] = mYawSample;
    mExportedJoints.elements[1] = mPitchSample;
    mExportedJoints.elements[2] = mRollSample;
    _joint_samples.write(mExportedJoints);
}

void MotorDriverScheduler::errorHook()
{
    MotorDriverSchedulerBase::errorHook();
}
void MotorDriverScheduler::stopHook()
{
    MotorDriverSchedulerBase::stopHook();
}
void MotorDriverScheduler::cleanupHook()
{
    MotorDriverSchedulerBase::cleanupHook();
}
