/*
	FILE: dynamicNavigation.cpp
	------------------------
	dynamic navigation implementation file in real world
*/
#include <autonomous_flight/px4/dynamicNavigation.h>

namespace AutoFlight{
	dynamicNavigation::dynamicNavigation(const ros::NodeHandle& nh) : flightBase(nh){
		this->initParam();
		this->initModules();
		this->registerPub();
	}

	void dynamicNavigation::initParam(){
    	// parameters    
    	// use global planner or not	
		if (not this->nh_.getParam("autonomous_flight/use_global_planner", this->useGlobalPlanner_)){
			this->useGlobalPlanner_ = false;
			cout << "[AutoFlight]: No use global planner param found. Use default: false." << endl;
		}
		else{
			cout << "[AutoFlight]: Global planner use is set to: " << this->useGlobalPlanner_ << "." << endl;
		}

		// full state control (yaw)
		if (not this->nh_.getParam("autonomous_flight/use_yaw_control", this->useYawControl_)){
			this->useYawControl_ = false;
			cout << "[AutoFlight]: No yaw control param found. Use default: false." << endl;
		}
		else{
			cout << "[AutoFlight]: Yaw control use is set to: " << this->useYawControl_ << "." << endl;
		}		

    	// desired linear velocity    	
		if (not this->nh_.getParam("autonomous_flight/desired_velocity", this->desiredVel_)){
			this->desiredVel_ = 1.0;
			cout << "[AutoFlight]: No desired velocity param found. Use default: 1.0 m/s." << endl;
		}
		else{
			cout << "[AutoFlight]: Desired velocity is set to: " << this->desiredVel_ << "m/s." << endl;
		}

		// desired acceleration
		if (not this->nh_.getParam("autonomous_flight/desired_acceleration", this->desiredAcc_)){
			this->desiredAcc_ = 1.0;
			cout << "[AutoFlight]: No desired acceleration param found. Use default: 1.0 m/s^2." << endl;
		}
		else{
			cout << "[AutoFlight]: Desired acceleration is set to: " << this->desiredAcc_ << "m/s^2." << endl;
		}


    	// desired angular velocity    	
		if (not this->nh_.getParam("autonomous_flight/desired_angular_velocity", this->desiredAngularVel_)){
			this->desiredAngularVel_ = 1.0;
			cout << "[AutoFlight]: No desired angular velocity param found. Use default: 0.5 rad/s." << endl;
		}
		else{
			cout << "[AutoFlight]: Desired angular velocity is set to: " << this->desiredAngularVel_ << "rad/s." << endl;
		}		
	}

	void dynamicNavigation::initModules(){
		// initialize map
		this->map_.reset(new mapManager::dynamicMap (this->nh_));

		// initialize fake detector
		this->detector_.reset(new onboardVision::fakeDetector (this->nh_));		

		// initialize rrt planner
		this->rrtPlanner_.reset(new globalPlanner::rrtOccMap<3> (this->nh_));
		this->rrtPlanner_->setMap(this->map_);

		// initialize polynomial trajectory planner
		this->polyTraj_.reset(new trajPlanner::polyTrajOccMap (this->nh_));
		this->polyTraj_->setMap(this->map_);
		this->polyTraj_->updateDesiredVel(this->desiredVel_);
		this->polyTraj_->updateDesiredAcc(this->desiredAcc_);

		// initialize piecewise linear trajectory planner
		this->pwlTraj_.reset(new trajPlanner::pwlTraj (this->nh_));

		// initialize bspline trajectory planner
		this->bsplineTraj_.reset(new trajPlanner::bsplineTraj (this->nh_));
		this->bsplineTraj_->setMap(this->map_);
		this->bsplineTraj_->updateMaxVel(this->desiredVel_);
		this->bsplineTraj_->updateMaxAcc(this->desiredAcc_);
	}

	void dynamicNavigation::registerPub(){
		this->rrtPathPub_ = this->nh_.advertise<nav_msgs::Path>("dynamicNavigation/rrt_path", 10);
		this->polyTrajPub_ = this->nh_.advertise<nav_msgs::Path>("dynamicNavigation/poly_traj", 10);
		this->pwlTrajPub_ = this->nh_.advertise<nav_msgs::Path>("dynamicNavigation/pwl_trajectory", 10);
		this->bsplineTrajPub_ = this->nh_.advertise<nav_msgs::Path>("dynamicNavigation/bspline_trajectory", 10);
		this->inputTrajPub_ = this->nh_.advertise<nav_msgs::Path>("dynamicNavigation/input_trajectory", 10);
	}

	void dynamicNavigation::registerCallback(){
		// planner callback
		this->plannerTimer_ = this->nh_.createTimer(ros::Duration(0.02), &dynamicNavigation::plannerCB, this);

		// collision check callback
		this->replanCheckTimer_ = this->nh_.createTimer(ros::Duration(0.01), &dynamicNavigation::replanCheckCB, this);

		// trajectory execution callback
		this->trajExeTimer_ = this->nh_.createTimer(ros::Duration(0.01), &dynamicNavigation::trajExeCB, this);

		// state update callback (velocity and acceleration)
		this->stateUpdateTimer_ = this->nh_.createTimer(ros::Duration(0.033), &dynamicNavigation::stateUpdateCB, this);

		// visualization callback
		this->visTimer_ = this->nh_.createTimer(ros::Duration(0.033), &dynamicNavigation::visCB, this);

		// free map callback
		this->freeMapTimer_ = this->nh_.createTimer(ros::Duration(0.01), &dynamicNavigation::freeMapCB, this);
	}

	void dynamicNavigation::plannerCB(const ros::TimerEvent&){
		if (not this->firstGoal_) return;

		if (this->useGlobalPlanner_){
			// only if a new goal is received, the global planner will make plan
			if (this->goalReceived_){ 
			}
		}

		if (this->replan_){
			// get start and end condition for trajectory generation (the end condition is the final zero condition)
			std::vector<Eigen::Vector3d> startEndCondition;
			this->getStartEndCondition(startEndCondition); 

			// bspline trajectory generation
			nav_msgs::Path inputTraj;
			double dt; // dt for bspline
			double finalTime; // final time for bspline trajectory
			double initTs = this->bsplineTraj_->getInitTs();

			if (not this->trajectoryReady_){ // use polynomial trajectory as input
				nav_msgs::Path waypoints, polyTrajTemp;
				geometry_msgs::PoseStamped start, goal;
				start.pose = this->odom_.pose.pose; goal = this->goal_;
				waypoints.poses = std::vector<geometry_msgs::PoseStamped> {start, goal};
				this->polyTraj_->updatePath(waypoints, startEndCondition);
				this->polyTraj_->makePlan(false); // no corridor constraint
				
				nav_msgs::Path adjustedInputPolyTraj;
				bool satisfyDistanceCheck = false;
				double dtTemp = initTs;
				double finalTimeTemp;
				while (ros::ok()){
					nav_msgs::Path inputPolyTraj = this->polyTraj_->getTrajectory(dtTemp);
					satisfyDistanceCheck = this->bsplineTraj_->inputPathCheck(inputPolyTraj, adjustedInputPolyTraj, dtTemp, finalTimeTemp);
					if (satisfyDistanceCheck) break;
					
					dtTemp *= 0.8;
				}

				dt = dtTemp;
				inputTraj = adjustedInputPolyTraj;
				finalTime = finalTimeTemp;
				startEndCondition[2] = this->polyTraj_->getVel(finalTime);
				startEndCondition[3] = this->polyTraj_->getAcc(finalTime);
			}
			else{
				Eigen::Vector3d bsplineLastPos = this->trajectory_.at(this->trajectory_.getDuration());
				geometry_msgs::PoseStamped lastPs; lastPs.pose.position.x = bsplineLastPos(0); lastPs.pose.position.y = bsplineLastPos(1); lastPs.pose.position.z = bsplineLastPos(2);
				Eigen::Vector3d goalPos (this->goal_.pose.position.x, this->goal_.pose.position.y, this->goal_.pose.position.z);
				// check the distance between last point and the goal position
				if ((bsplineLastPos - goalPos).norm() >= 0.2){ // use polynomial trajectory to make the rest of the trajectory
					nav_msgs::Path waypoints, polyTrajTemp;
					waypoints.poses = std::vector<geometry_msgs::PoseStamped>{lastPs, this->goal_};
					std::vector<Eigen::Vector3d> polyStartEndCondition;
					Eigen::Vector3d polyStartVel = this->trajectory_.getDerivative().at(this->trajectory_.getDuration());
					Eigen::Vector3d polyEndVel (0.0, 0.0, 0.0);
					Eigen::Vector3d polyStartAcc = this->trajectory_.getDerivative().getDerivative().at(this->trajectory_.getDuration());
					Eigen::Vector3d polyEndAcc (0.0, 0.0, 0.0);
					polyStartEndCondition.push_back(polyStartVel);
					polyStartEndCondition.push_back(polyEndVel);
					polyStartEndCondition.push_back(polyStartAcc);
					polyStartEndCondition.push_back(polyEndAcc);
					this->polyTraj_->updatePath(waypoints, polyStartEndCondition);
					this->polyTraj_->makePlan(false); // no corridor constraint
					
					nav_msgs::Path adjustedInputCombinedTraj;
					bool satisfyDistanceCheck = false;
					double dtTemp = initTs;
					double finalTimeTemp;
					while (ros::ok()){
						nav_msgs::Path inputRestTraj = this->getCurrentTraj(dtTemp);
						nav_msgs::Path inputPolyTraj = this->polyTraj_->getTrajectory(dtTemp);
						nav_msgs::Path inputCombinedTraj;
						inputCombinedTraj.poses = inputRestTraj.poses;
						for (size_t i=1; i<inputPolyTraj.poses.size(); ++i){
							inputCombinedTraj.poses.push_back(inputPolyTraj.poses[i]);
						}
						
						satisfyDistanceCheck = this->bsplineTraj_->inputPathCheck(inputCombinedTraj, adjustedInputCombinedTraj, dtTemp, finalTimeTemp);
						if (satisfyDistanceCheck) break;
						
						dtTemp *= 0.8; // magic number 0.8
					}
					dt = dtTemp;
					inputTraj = adjustedInputCombinedTraj;
					finalTime = finalTimeTemp - this->trajectory_.getDuration(); // need to subtract prev time since it is combined trajectory
					startEndCondition[2] = this->polyTraj_->getVel(finalTime);
					startEndCondition[3] = this->polyTraj_->getAcc(finalTime);
				}
				else{
					nav_msgs::Path adjustedInputRestTraj;
					bool satisfyDistanceCheck = false;
					double dtTemp = initTs;
					double finalTimeTemp;
					while (ros::ok()){
						nav_msgs::Path inputRestTraj = this->getCurrentTraj(dtTemp);

						satisfyDistanceCheck = this->bsplineTraj_->inputPathCheck(inputRestTraj, adjustedInputRestTraj, dtTemp, finalTimeTemp);
						if (satisfyDistanceCheck) break;
						
						dtTemp *= 0.8;
					}
					dt = dtTemp;
					inputTraj = adjustedInputRestTraj;
					finalTime = finalTimeTemp;			
				}
			}

			// cout << "dt: " <gg< dt << endl;
			this->inputTrajMsg_ = inputTraj;
			bool updateSuccess = this->bsplineTraj_->updatePath(inputTraj, startEndCondition, dt);
			if (updateSuccess){
				std::vector<Eigen::Vector3d> obstaclesPos, obstaclesVel, obstaclesSize;
				// this->map_->getDynamicObstacles(obstaclesPos, obstaclesVel, obstaclesSize);
				this->getDynamicObstacles(obstaclesPos, obstaclesVel, obstaclesSize);
				if (obstaclesPos.size() != 0){
					this->bsplineTraj_->updateDynamicObstacles(obstaclesPos, obstaclesVel, obstaclesSize);
				}

				nav_msgs::Path bsplineTrajMsgTemp;
				bool planSuccess = this->bsplineTraj_->makePlan(bsplineTrajMsgTemp);
				if (planSuccess){
					this->bsplineTrajMsg_ = bsplineTrajMsgTemp;
					this->trajStartTime_ = ros::Time::now();
					this->trajectory_ = this->bsplineTraj_->getTrajectory();
					this->trajectoryReady_ = true;
					this->replan_ = false;
					cout << "[AutoFlight]: Trajectory generated successfully." << endl;
				}
				else{
					// if the current trajectory is still valid, then just ignore this iteration
					// if the current trajectory/or new goal point is assigned is not valid, then just stop
					if (this->hasCollision()){
						this->trajectoryReady_ = false;
						this->stop();
						cout << "[AutoFlight]: Stop!!! Trajectory generation fails." << endl;
					}
					else{
						if (this->trajectoryReady_){
							cout << "[AutoFlight]: Trajectory fail. Use trajectory from previous iteration." << endl;
						}
						else{
							cout << "[AutoFlight]: Unable to generate a feasible trajectory." << endl;
						}
						this->replan_ = false;
					}
				}
			}
		}
	}

	void dynamicNavigation::replanCheckCB(const ros::TimerEvent&){
		/*
			Replan if
			1. collision detected
			2. new goal point assigned
			3. fixed distance
		*/
		if (this->goalReceived_){
			this->replan_ = false;
			this->trajectoryReady_ = false;
			if (not this->useYawControl_){
				double yaw = atan2(this->goal_.pose.position.y - this->odom_.pose.pose.position.y, this->goal_.pose.position.x - this->odom_.pose.pose.position.x);
				this->moveToOrientation(yaw, this->desiredAngularVel_);
			}
			this->replan_ = true;
			this->goalReceived_ = false;

			cout << "[AutoFlight]: Replan for new goal position." << endl; 
			return;
		}

		if (this->trajectoryReady_){
			// check whether reach the trajectory goal
			Eigen::Vector3d currPos (this->odom_.pose.pose.position.x, this->odom_.pose.pose.position.y, this->odom_.pose.pose.position.z);
			Eigen::Vector3d goalPos (this->goal_.pose.position.x, this->goal_.pose.position.y, this->goal_.pose.position.z);
			if ((currPos - goalPos).norm() <= 0.2){
				this->replan_  = false;
				this->trajectoryReady_ = false;
				this->goalReceived_ = false;
				cout << "[AutoFlight]: Reach goal position." << endl;
				return;
			}


			if (this->hasCollision()){ // if trajectory not ready, do not replan
				this->replan_ = true;
				cout << "[AutoFlight]: Replan for collision." << endl;
				return;
			}


			if (this->computeExecutionDistance() >= 3.0){
				this->replan_ = true;
				cout << "[AutoFlight]: Regular replan." << endl;
				return;
			}

			// replan for dynamic obstacles
		
			if (this->hasDynamicObstacle()){
				this->replan_ = true;
				cout << "[AutoFlight]: Replan for dynamic obstacles." << endl;
				return;
			}
		}
	}

	void dynamicNavigation::trajExeCB(const ros::TimerEvent&){
		if (this->trajectoryReady_){
			ros::Time currTime = ros::Time::now();
			this->trajTime_ = (currTime - this->trajStartTime_).toSec();
			Eigen::Vector3d pos = this->trajectory_.at(this->trajTime_);
			Eigen::Vector3d vel = this->trajectory_.getDerivative().at(this->trajTime_);
			Eigen::Vector3d acc = this->trajectory_.getDerivative().getDerivative().at(this->trajTime_);
			
			tracking_controller::Target target;
			target.position.x = pos(0);
			target.position.y = pos(1);
			target.position.z = pos(2);
			target.velocity.x = vel(0);
			target.velocity.y = vel(1);
			target.velocity.z = vel(2);
			target.acceleration.x = acc(0);
			target.acceleration.y = acc(1);
			target.acceleration.z = acc(2);
			if (not this->useYawControl_){
				target.yaw = AutoFlight::rpy_from_quaternion(this->odom_.pose.pose.orientation);
			}
			else{
				target.yaw = atan2(vel(1), vel(0));
			}
			this->updateTargetWithState(target);			
		}
	}

	void dynamicNavigation::stateUpdateCB(const ros::TimerEvent&){
		Eigen::Vector3d currVelBody (this->odom_.twist.twist.linear.x, this->odom_.twist.twist.linear.y, this->odom_.twist.twist.linear.z);
		Eigen::Vector4d orientationQuat (this->odom_.pose.pose.orientation.w, this->odom_.pose.pose.orientation.x, this->odom_.pose.pose.orientation.y, this->odom_.pose.pose.orientation.z);
		Eigen::Matrix3d orientationRot = AutoFlight::quat2RotMatrix(orientationQuat);
		this->currVel_ = orientationRot * currVelBody;	
		ros::Time currTime = ros::Time::now();	
		if (this->stateUpdateFirstTime_){
			this->currAcc_ = Eigen::Vector3d (0.0, 0.0, 0.0);
			this->prevStateTime_ = currTime;
			this->stateUpdateFirstTime_ = false;
		}
		else{
			double dt = (currTime - this->prevStateTime_).toSec();
			this->currAcc_ = (this->currVel_ - this->prevVel_)/dt;
			this->prevVel_ = this->currVel_; 
			this->prevStateTime_ = currTime;
		}
	}

	void dynamicNavigation::visCB(const ros::TimerEvent&){
		if (this->rrtPathMsg_.poses.size() != 0){
			this->rrtPathPub_.publish(this->rrtPathMsg_);
		}
		if (this->polyTrajMsg_.poses.size() != 0){
			this->polyTrajPub_.publish(this->polyTrajMsg_);
		}
		if (this->pwlTrajMsg_.poses.size() != 0){
			this->pwlTrajPub_.publish(this->pwlTrajMsg_);
		}
		if (this->bsplineTrajMsg_.poses.size() != 0){
			this->bsplineTrajPub_.publish(this->bsplineTrajMsg_);
		}
		if (this->inputTrajMsg_.poses.size() != 0){
			this->inputTrajPub_.publish(this->inputTrajMsg_);
		}
	}

	void dynamicNavigation::freeMapCB(const ros::TimerEvent&){
		onboard_vision::ObstacleList msg;
		std::vector<std::pair<Eigen::Vector3d, Eigen::Vector3d>> freeRegions;
		this->detector_->getObstacles(msg);
		for (onboard_vision::Obstacle ob: msg.obstacles){
			Eigen::Vector3d lowerBound (ob.px-ob.xsize/2-0.3, ob.py-ob.ysize/2-0.3, ob.pz);
			Eigen::Vector3d upperBound (ob.px+ob.xsize/2+0.3, ob.py+ob.ysize/2+0.3, ob.pz+ob.zsize+0.2);
			freeRegions.push_back(std::make_pair(lowerBound, upperBound));
		}
		this->map_->updateFreeRegions(freeRegions);		
	}

	void dynamicNavigation::run(){
		// take off the drone
		this->takeoff();

		// register timer callback
		this->registerCallback();
	}

	void dynamicNavigation::getStartEndCondition(std::vector<Eigen::Vector3d>& startEndCondition){
		/*	
			1. start velocity
			2. start acceleration (set to zero)
			3. end velocity
			4. end acceleration (set to zero) 
		*/

		Eigen::Vector3d currVel, currAcc;
		if (this->trajectoryReady_){
			currVel = this->currVel_;
			currAcc = this->currAcc_;
		}
		else{
			// double targetYaw = AutoFlight::rpy_from_quaternion(this->odom_.pose.pose.orientation);
			// currVel = this->desiredVel_ * Eigen::Vector3d (cos(targetYaw), sin(targetYaw), 0.0);
			currVel = Eigen::Vector3d (0.0, 0.0, 0.0);
			currAcc = Eigen::Vector3d (0.0, 0.0, 0.0);			
		}
		Eigen::Vector3d endVel (0.0, 0.0, 0.0);
		Eigen::Vector3d endAcc (0.0, 0.0, 0.0);
		startEndCondition.push_back(currVel);
		startEndCondition.push_back(endVel);
		startEndCondition.push_back(currAcc);
		startEndCondition.push_back(endAcc);
	}

	bool dynamicNavigation::hasCollision(){
		if (this->trajectoryReady_){
			for (double t=this->trajTime_; t<=this->trajectory_.getDuration(); t+=0.1){
				Eigen::Vector3d p = this->trajectory_.at(t);
				bool hasCollision = this->map_->isInflatedOccupied(p);
				if (hasCollision){
					return true;
				}
			}
		}
		return false;
	}

	double dynamicNavigation::computeExecutionDistance(){
		if (this->trajectoryReady_ and not this->replan_){
			Eigen::Vector3d prevP, currP;
			bool firstTime = true;
			double totalDistance = 0.0;
			for (double t=0.0; t<=this->trajTime_; t+=0.1){
				currP = this->trajectory_.at(t);
				if (firstTime){
					firstTime = false;
				}
				else{
					totalDistance += (currP - prevP).norm();
				}
				prevP = currP;
			}
			return totalDistance;
		}
		return -1.0;
	}

	bool dynamicNavigation::hasDynamicObstacle(){
		std::vector<Eigen::Vector3d> obstaclesPos, obstaclesVel, obstaclesSize;
		// this->map_->getDynamicObstacles(obstaclesPos, obstaclesVel, obstaclesSize);	
		this->getDynamicObstacles(obstaclesPos, obstaclesVel, obstaclesSize);
		cout << "dynamic obstacle size: " << obstaclesPos.size() << endl;
		if (obstaclesPos.size() == 0){
			return false;
		}
		else{
			return true;
		}
	}

	nav_msgs::Path dynamicNavigation::getCurrentTraj(double dt){
		nav_msgs::Path currentTraj;
		currentTraj.header.frame_id = "map";
		currentTraj.header.stamp = ros::Time::now();
		if (this->trajectoryReady_){
			ros::Time currTime = ros::Time::now();
			double trajTime = (currTime - this->trajStartTime_).toSec();	
			for (double t=trajTime; t<=this->trajectory_.getDuration(); t+=dt){
				Eigen::Vector3d pos = this->trajectory_.at(t);
				geometry_msgs::PoseStamped ps;
				ps.pose.position.x = pos(0);
				ps.pose.position.y = pos(1);
				ps.pose.position.z = pos(2);
				currentTraj.poses.push_back(ps);
			}		
		}
		return currentTraj;
	}

	void dynamicNavigation::getDynamicObstacles(std::vector<Eigen::Vector3d>& obstaclesPos, std::vector<Eigen::Vector3d>& obstaclesVel, std::vector<Eigen::Vector3d>& obstaclesSize){
		onboard_vision::ObstacleList msg;
		this->detector_->getObstaclesInSensorRange(PI_const, msg);
		for (onboard_vision::Obstacle ob : msg.obstacles){
			Eigen::Vector3d pos (ob.px, ob.py, ob.pz);
			Eigen::Vector3d vel (ob.vx, ob.vy, ob.vz);
			Eigen::Vector3d size (ob.xsize, ob.ysize, ob.zsize);
			obstaclesPos.push_back(pos);
			obstaclesVel.push_back(vel);
			obstaclesSize.push_back(size);
		}
	}

}