/*
	FILE: flightBase.h
	-------------------
	base implementation for autonomous flight
*/

#ifndef FLIGHTBASE_H
#define FLIGHTBASE_H
#include <ros/ros.h>
#include <autonomous_flight/px4/utils.h>
#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>
#include <geometry_msgs/PoseStamped.h>
#include <mavros_msgs/CommandBool.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/State.h>
#include <thread>
#include <mutex>

using std::cout; using std::endl;
namespace AutoFlight{
	struct trajData{
		nav_msgs::Path trajectory;
		nav_msgs::Path currTrajectory;
		ros::Time startTime;
		double tCurr;
		double duration;
		double timestep;
		bool init = false;
		int forwardIdx = 5;
		int minIdx = 3;

		void updateTrajectory(const nav_msgs::Path& _trajectory, double _duration){
			this->trajectory = _trajectory;
			this->currTrajectory = _trajectory;
			this->duration = _duration;
			this->tCurr = 0.0;
			this->startTime = ros::Time::now();
			this->timestep = this->duration/(this->trajectory.poses.size()-1);
			if (not this->init){
				this->init = true;
			}
		}

		int getCurrIdx(){
			ros::Time currTime = ros::Time::now();
			this->tCurr = (currTime - this->startTime).toSec() + this->timestep;
			if (this->tCurr > this->duration){
				this->tCurr = this->duration;
			}

			int idx = floor(this->tCurr/this->timestep);
			return idx;
		}

		geometry_msgs::PoseStamped getPose(){
			int idx = this->getCurrIdx();
			idx = std::max(idx+forwardIdx, minIdx);
			int newIdx = std::min(idx, int(this->trajectory.poses.size()-1));

			std::vector<geometry_msgs::PoseStamped> pathVec;
			for (size_t i=idx; i<this->trajectory.poses.size(); ++i){
				pathVec.push_back(this->trajectory.poses[i]);
			}
			this->currTrajectory.poses = pathVec;
			return this->trajectory.poses[newIdx];
		}

		geometry_msgs::PoseStamped getPose(const geometry_msgs::Pose& psCurr){
			int idx = this->getCurrIdx();
			idx = std::max(idx+forwardIdx, minIdx);
			int newIdx = std::min(idx, int(this->trajectory.poses.size()-1));

			std::vector<geometry_msgs::PoseStamped> pathVec;
			geometry_msgs::PoseStamped psFirst;
			psFirst.pose = psCurr;
			pathVec.push_back(psFirst);
			for (size_t i=idx; i<this->trajectory.poses.size(); ++i){
				pathVec.push_back(this->trajectory.poses[i]);
			}
			this->currTrajectory.poses = pathVec;
			return this->trajectory.poses[newIdx];
		}

		geometry_msgs::PoseStamped getPoseWithoutYaw(const geometry_msgs::Pose& psCurr){
			int idx = this->getCurrIdx();
			idx = std::max(idx+forwardIdx, minIdx);
			int newIdx = std::min(idx, int(this->trajectory.poses.size()-1));

			std::vector<geometry_msgs::PoseStamped> pathVec;
			geometry_msgs::PoseStamped psFirst;
			psFirst.pose = psCurr;
			pathVec.push_back(psFirst);
			for (size_t i=idx; i<this->trajectory.poses.size(); ++i){
				pathVec.push_back(this->trajectory.poses[i]);
			}
			this->currTrajectory.poses = pathVec;
			geometry_msgs::PoseStamped psTarget = this->trajectory.poses[newIdx];
			psTarget.pose.orientation = psCurr.orientation;
			return psTarget;	
		}

		void stop(const geometry_msgs::Pose& psCurr){
			std::vector<geometry_msgs::PoseStamped> pathVec;
			geometry_msgs::PoseStamped ps;
			ps.pose = psCurr;
			pathVec.push_back(ps);
			this->trajectory.poses = pathVec;
			this->currTrajectory = this->trajectory;
			this->tCurr = 0.0;
			this->startTime = ros::Time::now();
			this->duration = this->timestep; 
		}

		double getRemainTime(){
			ros::Time currTime = ros::Time::now();
			double tCurr = (currTime - this->startTime).toSec() + this->timestep;
			return this->duration - tCurr;
		}

		bool needReplan(double factor){
			if (this->getRemainTime() <= this->duration * (1 - factor)){
				return true;
			}
			else{
				return false;
			}
		}
	};


	class flightBase{
	private:

	protected:
		ros::NodeHandle nh_;
		ros::Subscriber stateSub_;
		ros::Subscriber odomSub_;
		ros::Subscriber clickSub_;
		ros::Publisher posePub_;
		ros::ServiceClient armClient_;
		ros::ServiceClient setModeClient_;
		
		nav_msgs::Odometry odom_;
		mavros_msgs::State mavrosState_;
		geometry_msgs::PoseStamped poseTgt_;
		geometry_msgs::PoseStamped goal_;
		
		// parameters
		double sampleTime_;
		double takeoffHgt_;

		// status
		bool odomReceived_;
		bool mavrosStateReceived_;
		bool firstGoal_ = false;
		bool goalReceived_ = false;


	public:
		std::thread posePubWorker_;

		flightBase(const ros::NodeHandle& nh);
		~flightBase();
		void takeoff();
		void updateTarget(const geometry_msgs::PoseStamped& ps);
		void run();
		void pubPose();
		void stateCB(const mavros_msgs::State::ConstPtr& state);
		void odomCB(const nav_msgs::Odometry::ConstPtr& odom);
		void clickCB(const geometry_msgs::PoseStamped::ConstPtr& cp);
		bool isReach(const geometry_msgs::PoseStamped& poseTgt, bool useYaw=true);
	};
}

#endif
