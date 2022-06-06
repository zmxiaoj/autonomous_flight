/*
	FILE: inspeciton.cpp
	-----------------------------
	Implementation of autonomous inspection 
*/

#include <autonomous_flight/px4/inspection.h>

namespace AutoFlight{
	inspector::inspector(const ros::NodeHandle& nh) : flightBase(nh){
		this->loadParam();
		this->initPlanner();
		this->mapSub_ = this->nh_.subscribe("/octomap_full", 1, &inspector::mapCB, this);
			
		// visualization
		this->targetVisPub_ = this->nh_.advertise<visualization_msgs::MarkerArray>("/inspection_target", 100);
		this->targetVisWorker_ = std::thread(&inspector::publishTargetVis, this);
		this->targetVisWorker_.detach();

		this->pathPub_ = this->nh_.advertise<nav_msgs::Path>("/inspector/path", 100);
		this->pathVisWorker_ = std::thread(&inspector::publishPathVis, this);
		this->pathVisWorker_.detach();
	}

	void inspector::loadParam(){
		// Collision box
		if (not this->nh_.getParam("collision_box", this->collisionBox_)){
			this->collisionBox_ = std::vector<double> {0.75, 0.75, 0.3};
			cout << "[AutoFlight]: No Collision box param. Use default {0.75, 0.75, 0.3}." << endl;
		}
		else{
			cout << "[AutoFlight]: Collision Box: [" << this->collisionBox_[0] 
				 << ", " << this->collisionBox_[1]
			 	 << ", " << this->collisionBox_[2] << "]" << endl; 
		}

		// Safe distance
		if (not this->nh_.getParam("safe_distance", this->safeDist_)){
			this->safeDist_ = 1.0;
			cout << "[AutoFlight]: No safe distance param. Use default 1.0m." << endl;
		}
		else{
			cout << "[AutoFlight]: Safe distance to wall is set to: " << this->safeDist_ << "m." << endl;
		}

		// min target area
		if (not this->nh_.getParam("min_target_area", this->minTargetArea_)){
			this->minTargetArea_ = 10;
			cout << "[AutoFlight]: No min target area for inspection. Use default: 10.0m^2" << endl;
		}
		else{
			cout << "[AutoFlight]: Min target area for inspection: " << this->minTargetArea_ << "m" << endl;
		}

		// max height
		if (not this->nh_.getParam("max_inspection_target_height", this->maxTargetHgt_)){
			this->maxTargetHgt_ = 3.0;
			cout << "[AutoFlight]: No max height for inspection. Use default: 3.0m" << endl;
		}
		else{
			cout << "[AutoFlight]: Max height for inspection: " << this->maxTargetHgt_ << "m" << endl;
		}


		// max width
		if (not this->nh_.getParam("max_inspection_target_width", this->maxTargetWidth_)){
			this->descendHgt_ = 5.0;
			cout << "[AutoFlight]: No max width for inspection. Use default: 5.0m" << endl;
		}
		else{
			
			cout << "[AutoFlight]: Max width for inspection: " << this->maxTargetWidth_ << "m" << endl;
		}

		// descend height
		if (not this->nh_.getParam("inspection_descend_height", this->descendHgt_)){
			this->descendHgt_ = 3.0;
			cout << "[AutoFlight]: No descend height for inspection. Use default: 0.3m" << endl;
		}
		else{
			cout << "[AutoFlight]: Descend height for inspection: " << this->descendHgt_ << "m" << endl;
		}

		// desired velocity
		if (not this->nh_.getParam("desired_velocity", this->desiredVel_)){
			this->desiredVel_ = 0.3;
			cout << "[AutoFlight]: No desired velocity. Use default: 0.3 m/s" << endl;
		}
		else{
			cout << "[AutoFlight]: Desired velocity: " << this->desiredVel_ << " m/s." << endl;
		}

		// desired angular velocity
		if (not this->nh_.getParam("desired_angular_velocity", this->desiredAngularVel_)){
			this->desiredAngularVel_ = 0.3;
			cout << "[AutoFlight]: No desired angular velocity. Use default: 0.3 rad/s" << endl;
		}
		else{
			cout << "[AutoFlight]: Desired angular velocity: " << this->desiredAngularVel_ << " rad/s." << endl;
		}

		// NBV sample number
		if (not this->nh_.getParam("nbv_sample_num", this->nbvSampleNum_)){
			this->nbvSampleNum_ = 10;
			cout << "[AutoFlight]: No NBV sample number. Use default: 10" << endl;
		}
		else{
			cout << "[AutoFlight]: NBV sample number: " << this->nbvSampleNum_ << endl;
		}

		// Sensor Range
		if (not this->nh_.getParam("sensor_range", this->sensorRange_)){
			this->sensorRange_ = 5.0;
			cout << "[AutoFlight]: No sensor range parameter. Use default: 5.0m" << endl;
		}
		else{
			cout << "[AutoFlight]: Sensor range: " << this->sensorRange_ << "m." << endl;
		}

		// Sensor vertical angle
		if (not this->nh_.getParam("sensor_vertical_angle", this->sensorVerticalAngle_)){
			this->sensorVerticalAngle_ = PI_const/4;
			cout << "[AutoFlight]: No sensor vertical angle parameter. Use default: 45 degree." << endl;
		}
		else{
			cout << "[AutoFlight]: Sensor vertical angle: " << this->sensorVerticalAngle_ << " degree." << endl;
			this->sensorVerticalAngle_ *= PI_const/180.0;
		}

		// minimum forward distance
		if (not this->nh_.getParam("forward_min_distance", this->forwardMinDist_)){
			this->forwardMinDist_ = 0.5;
			cout << "[AutoFlight]: No minumum forward distance param. Use default: 0.5 m." << endl;
		}
		else{
			cout << "[AutoFlight]: Minumum Forward Distance: " << this->forwardMinDist_ << endl;
		}

		// step ascend distance
		if (not this->nh_.getParam("step_ascend_delta", this->stepAscendDelta_)){
			this->stepAscendDelta_ = 3.0;
			cout << "[AutoFlight]: No step ascend delta param. Use default: 3.0 m." << endl;
		}
		else{
			cout << "[AutoFlight]: Step Ascend Delta: " << this->stepAscendDelta_ << " m." << endl;
		}

		// NBV sample timeout
		if (not this->nh_.getParam("nbv_sample_time_out", this->sampleTimeout_)){
			this->sampleTimeout_ = 1.0;
			cout << "[AutoFlight]: No NBV sample timeout param. Use default: 1.0 s." << endl;
		}
		else{
			cout << "[AutoFlight]: NBV sample timeout: " << this->sampleTimeout_ << " s." << endl;  
		}

		// NBV safety reduce factor
		if (not this->nh_.getParam("safe_reduce_factor", this->reduceFactor_)){
			this->reduceFactor_ = 0.5;
			cout << "[AutoFlight]: No safe reduce factor: Use default: 0.5." << endl;
		}
		else{
			cout << "[AutoFlight]: Safe reduce factor: " << this->reduceFactor_ << endl;
		}
	}

	void inspector::initPlanner(){
		this->pwlPlanner_ = new trajPlanner::pwlTraj (this->nh_);
		this->rrtPlanner_ = new globalPlanner::rrtOctomap<3> (this->nh_);
	}

	void inspector::run(){
		cout << "[AutoFlight]: Please double check all parameters. Then PRESS ENTER to continue or PRESS CTRL+C to land." << endl;
		std::cin.get();
		this->takeoff();
		

		cout << "[AutoFlight]: Ready to start please check hover conditions. Then PRESS ENTER to continue or PRESS CTRL+C to land." << endl;
		std::cin.get();

		// STEP 1: APPROACH TARGET
		this->lookAround();
		bool targetReach = false;
		while (ros::ok() and not targetReach){
			bool forwardSuccess = this->forward();
			if (not forwardSuccess){
				this->forwardNBV();
			}
			else{
				this->lookAround(); // check the wall condition	
			}
			

			targetReach = this->hasReachTarget();
		}

		cout << "[AutoFlight]: Please make sure UAV arrive the target. Then PRESS ENTER to continue or PRESS CTRL+C to land." << endl;
		std::cin.get();
		// STEP 2: EXPLORE TARGET
		double height = this->takeoffHgt_; // current height
		bool reachTargetHgt = false;
		while (ros::ok() and not reachTargetHgt){
			this->checkSurroundings(); // check surroundings and dimensions of the surface and back to center

			height += this->stepAscendDelta_;
			if (height >= this->maxTargetHgt_){
				height = this->maxTargetHgt_;
				reachTargetHgt = true;
			}

			this->moveUp(height);
			this->lookAround();
			targetReach = this->hasReachTarget();
		}

		
		// STEP 3: INSPECTION
		this->inspect(); // inspect the surface by zig-zag path
	
		// STEP 4: RETURN
		this->backward();
		cout << "[AutoFlight]: Mission Complete. PRESS CTRL+C to land." << endl;
	}


	void inspector::lookAround(){
		nav_msgs::Path lookAroundPath;
		geometry_msgs::PoseStamped ps;
		ps.pose = this->odom_.pose.pose;
		double currYaw = trajPlanner::rpy_from_quaternion(ps.pose.orientation);
		double targetYaw1 = currYaw + M_PI/2;
		double targetYaw2 = currYaw - M_PI/2;
		geometry_msgs::PoseStamped ps1, ps2;
		ps1.pose.position = ps.pose.position;
		ps2.pose.position = ps.pose.position;
		ps1.pose.orientation = AutoFlight::quaternion_from_rpy(0, 0, targetYaw1);
		ps2.pose.orientation = AutoFlight::quaternion_from_rpy(0, 0, targetYaw2);
		std::vector<geometry_msgs::PoseStamped> lookAroundPathVec = std::vector<geometry_msgs::PoseStamped> {ps, ps1, ps, ps2, ps};
		lookAroundPath.poses = lookAroundPathVec;
		this->pwlPlanner_->updatePath(lookAroundPath, true); // true: use yaw angle in the message

		this->updatePathVis(lookAroundPath);

		cout << "[AutoFlight]: Start looking around..." << endl;
		double t = 0.0;
		ros::Rate r (1.0/this->sampleTime_);
		ros::Time tStart = ros::Time::now();
		while (ros::ok() and (not this->isReach(ps) or t < this->pwlPlanner_->getDuration())){
			ros::Time tCurr = ros::Time::now();
			t = (tCurr - tStart).toSec();
			geometry_msgs::PoseStamped targetPose = this->pwlPlanner_->getPose(t);
			this->updateTarget(targetPose);
			r.sleep();
		}
		cout << "[AutoFlight]: Done." << endl;
	}


	bool inspector::forward(){
		bool success;
		nav_msgs::Path forwardPath = this->getForwardPath(success);
		if (not success){
			cout << "[AutoFlight]: Cannot directly forward..." << endl;
			return false;
		}
		

		this->updatePathVis(forwardPath);

		cout << "[AutoFlight]: Start direct forwarding..." << endl;
		this->executeWaypointPath(forwardPath);
		cout << "[AutoFlight]: Done." << endl;
		return true;
	}

	void inspector::forwardNBV(){
		nav_msgs::Path forwardNBVPath;
		// first sample goal point
		octomap::point3d pBestView = this->sampleNBVGoal();
		geometry_msgs::Quaternion quatStart = this->odom_.pose.pose.orientation;

		// then use RRT to find path
		std::vector<double> startVec = this->getVecPos();
		std::vector<double> goalVec {pBestView.x(), pBestView.y(), pBestView.z()};
		this->rrtPlanner_->updateStart(startVec);
		this->rrtPlanner_->updateGoal(goalVec);
		this->rrtPlanner_->makePlan(forwardNBVPath);
		this->pwlPlanner_->updatePath(forwardNBVPath);

		this->updatePathVis(forwardNBVPath);

		cout << "[AutoFlight]: NBV Forward for obstacle avoidance..." << endl; 
		// adjust angle
		this->moveToAngle(this->pwlPlanner_->getFirstPose().pose.orientation);

		double t = 0.0;
		ros::Rate r (1.0/this->sampleTime_);
		ros::Time tStart = ros::Time::now();
		geometry_msgs::PoseStamped pGoal = forwardNBVPath.poses.back();
		while (ros::ok() and not this->isReach(pGoal, false)){
			ros::Time tCurr = ros::Time::now();
			t = (tCurr - tStart).toSec();
			geometry_msgs::PoseStamped ps = this->pwlPlanner_->getPose(t);
			this->updateTarget(ps);
			r.sleep();
		}

		this->moveToAngle(quatStart);
		cout << "[AutoFlight]: Done." << endl;
	}

	void inspector::moveUp(double height){
		nav_msgs::Path upwardPath;
		geometry_msgs::PoseStamped pCurr;
		geometry_msgs::PoseStamped pHgt;
		pCurr.pose = this->odom_.pose.pose;
		pHgt = pCurr;
		pHgt.pose.position.z = height;
		std::vector<geometry_msgs::PoseStamped> upwardPathVec {pCurr, pHgt};
		upwardPath.poses = upwardPathVec;

		this->updatePathVis(upwardPath);

		cout << "[AutoFlight]: Moving up..." << endl;
		this->executeWaypointPath(upwardPath);
		cout << "[AutoFlight]: Done." << endl;
	}

	void inspector::checkSurroundings(){
		cout << "[AutoFlight]: Start checking inspection target dimensions..." << endl;
		cout << "[AutoFlight]: Check Left Side..." << endl;
		// check surroundings and go back to center position
		octomap::point3d pLeftOrigin = this->getPoint3dPos();
		octomap::point3d leftDirection (0.0, 1.0, 0.0);
		octomap::point3d leftEnd;

		bool leftFirstTime = true;
		bool leftDone = this->map_->castRay(pLeftOrigin, leftDirection, leftEnd);
		while (ros::ok() and not leftDone){

			if (leftFirstTime){
				this->moveToAngle(AutoFlight::quaternion_from_rpy(0, 0, PI_const/2));
				leftFirstTime = false;
			}
			// find left most point to go which keeps safe distance
			nav_msgs::Path leftCheckPath = this->checkSurroundingsLeft();
			this->pwlPlanner_->updatePath(leftCheckPath);

			this->updatePathVis(leftCheckPath);

			double t = 0.0;
			ros::Rate r (1.0/this->sampleTime_);
			ros::Time tStart = ros::Time::now();
			geometry_msgs::PoseStamped pGoal = leftCheckPath.poses.back();
			while (ros::ok() and not this->isReach(pGoal)){
				ros::Time tCurr = ros::Time::now();
				t = (tCurr - tStart).toSec();
				geometry_msgs::PoseStamped ps = this->pwlPlanner_->getPose(t);
				this->updateTarget(ps);
				r.sleep();
			}
			leftDone = this->map_->castRay(pLeftOrigin, leftDirection, leftEnd);
		}
		cout << "[AutoFlight]: Left is Okay!" << endl;
		// back to origin angle
		this->moveToAngle(AutoFlight::quaternion_from_rpy(0, 0, 0));


		// Right
		cout << "[AutoFlight]: Check Right Side..." << endl;
		octomap::point3d pRightOrigin = this->getPoint3dPos();
		octomap::point3d rightDirection (0.0, -1.0, 0.0);
		octomap::point3d rightEnd;

		bool rightFirstTime = true;
		bool rightDone = this->map_->castRay(pRightOrigin, rightDirection, rightEnd);
		while (ros::ok() and not rightDone){
			if (rightFirstTime){
				this->moveToAngle(AutoFlight::quaternion_from_rpy(0, 0, -PI_const/2));
				rightFirstTime = false;
			}
			// find left most point to go which keeps safe distance
			nav_msgs::Path rightCheckPath = this->checkSurroundingsRight();
			this->pwlPlanner_->updatePath(rightCheckPath);

			this->updatePathVis(rightCheckPath);

			double t = 0.0;
			ros::Rate r (1.0/this->sampleTime_);
			ros::Time tStart = ros::Time::now();
			geometry_msgs::PoseStamped pGoal = rightCheckPath.poses.back();
			while (ros::ok() and not this->isReach(pGoal)){
				ros::Time tCurr = ros::Time::now();
				t = (tCurr - tStart).toSec();
				geometry_msgs::PoseStamped ps = this->pwlPlanner_->getPose(t);
				this->updateTarget(ps);
				r.sleep();
			}

			rightDone = this->map_->castRay(pRightOrigin, rightDirection, rightEnd);
		}
		cout << "[AutoFlight]: Right is Okay!" << endl;
		cout << "[AutoFlight]: Left Target Limit: " << leftEnd.y() << " m, Right Target Limit: " << rightEnd.y() << " m." << endl;


		double centerY = (leftEnd.y() + rightEnd.y())/2.0;
		cout << "[AutoFlight]: Going to the center of the target: " <<  centerY  << "..." << endl;
		geometry_msgs::Point pos;
		pos = this->odom_.pose.pose.position;
		pos.y = centerY;
		this->moveToAngle(AutoFlight::quaternion_from_rpy(0, 0, 0));
		this->moveToPos(pos);
		cout << "[AutoFlight]: Done." << endl;
	}

	void inspector::inspect(){
		nav_msgs::Path zigZagPath = this->generateZigZagPath();
		
		this->updatePathVis(zigZagPath);

		cout << "[AutoFlight]: Ready for Inpsection please check the zig-zag path. PRESS ENTER to continue or PRESS CTRL+C to land." << endl;
		std::cin.get();
		cout << "[AutoFlight]: Start Inpection..." << endl;
		this->executeWaypointPath(zigZagPath, true);
		cout << "[AutoFlight]: Done." << endl;
	}

	void inspector::backward(){
		nav_msgs::Path backPath;
		std::vector<double> startVec = this->getVecPos();
		std::vector<double> goalVec {0.0, 0.0, this->takeoffHgt_};
		this->rrtPlanner_->updateStart(startVec);
		this->rrtPlanner_->updateGoal(goalVec);
		this->rrtPlanner_->makePlan(backPath);
		// modify back path to make it rotate at the start location

		this->updatePathVis(backPath);

		cout << "[AutoFlight]: Ready to return please check the back path. PRESS ENTER to continue or PRESS CTRL+C to land." << endl;
		std::cin.get();
		cout << "[AutoFlight]: Start Returning..." << endl;;
		// adjust heading first
		this->moveToAngle(this->pwlPlanner_->getFirstPose().pose.orientation);
		this->executeWaypointPath(backPath);
		cout << "[AutoFlight]: Done." << endl;
	}

	bool inspector::hasReachTarget(){
		std::vector<double> range;
		double area = this->findTargetRange(range);
		
		double distance = std::abs(range[0] - this->odom_.pose.pose.position.x);

		cout << "[AutoFlight]: Potential Area is: " << area << " m^2"<< endl; 
		cout << "[AutoFlight]: Distance to potential target is: " << distance << " m." << endl;

 		bool hasReachTarget;
		if (area >= this->minTargetArea_ and distance <= this->safeDist_ + 2*this->mapRes_){
			hasReachTarget = true;
			if (this->targetRange_.size() == 0){
				this->targetRange_ = range;
				cout << "[AutoFlight]: Inspection Target Found!" << endl;
			}
			else{
				this->targetRange_[0] = std::min(range[0], this->targetRange_[0]);
				this->targetRange_[1] = std::max(range[1], this->targetRange_[1]);
				this->targetRange_[2] = std::min(range[2], this->targetRange_[2]);
				this->targetRange_[3] = std::max(range[3], this->targetRange_[3]);
				this->targetRange_[4] = std::min(range[4], this->targetRange_[4]);
				this->targetRange_[5] = std::max(range[5], this->targetRange_[5]);
				range = this->targetRange_;
				cout << "[AutoFlight]: Updated target dimensions..." << endl;
			}
		}
		else{
			hasReachTarget = false;
			if (area >= this->minTargetArea_){
				cout << "[AutoFlight]: Potential Target Found! Need to get closer and check dimensions." << endl;
			}
			else{
				cout << "[AutoFlight]: This is not the inspection target. Continue..." << endl;
			}
		}



		this->updateTargetVis(range, hasReachTarget);
		return hasReachTarget;
	}

	void inspector::mapCB(const octomap_msgs::Octomap &msg){
    	octomap::OcTree* treePtr = dynamic_cast<octomap::OcTree*>(octomap_msgs::msgToMap(msg));
   	 	this->map_ = std::shared_ptr<octomap::OcTree>(treePtr);
   	 	this->mapRes_ = this->map_->getResolution();
   	 	this->setSurroundingFree(this->getPoint3dPos());
   	 	// cout << "here" << endl;
	}

	visualization_msgs::Marker inspector::getLineMarker(double x1, double y1, double z1, 
													    double x2, double y2, double z2,
													    int id, bool hasReachTarget){
		std::vector<geometry_msgs::Point> lineVec;
		geometry_msgs::Point p1, p2;
		p1.x = x1; p1.y = y1; p1.z = z1;
		p2.x = x2; p2.y = y2; p2.z = z2;
		lineVec.push_back(p1);
		lineVec.push_back(p2);

		visualization_msgs::Marker lineMarker;
		lineMarker.header.frame_id = "map";
		lineMarker.header.stamp = ros::Time::now();
		lineMarker.ns = "inspection_target";
		lineMarker.id = id;
		lineMarker.type = visualization_msgs::Marker::LINE_LIST;
		lineMarker.action = visualization_msgs::Marker::ADD;
		lineMarker.points = lineVec;
		lineMarker.scale.x = 0.1;
		lineMarker.scale.y = 0.1;
		lineMarker.scale.z = 0.1;
		lineMarker.color.a = 1.0; // Don't forget to set the alpha!
		if (hasReachTarget){
			lineMarker.color.r = 0.0;
			lineMarker.color.g = 1.0;
		}
		else{
			lineMarker.color.r = 1.0;
			lineMarker.color.g = 0.0;
		}
		lineMarker.color.b = 0.0;
		return lineMarker;
	}


	void inspector::updateTargetVis(const std::vector<double>& range, bool hasReachTarget){
		double xmin, xmax, ymin, ymax, zmin, zmax;
		xmin = range[0]; xmax = range[1];
		ymin = range[2]; ymax = range[3];
		zmin = range[4]; zmax = range[5];
		
		// 12 line
		std::vector<visualization_msgs::Marker> visVec;
		visualization_msgs::Marker l1 = this->getLineMarker(xmin, ymin, zmin, xmin, ymax, zmin, 1, hasReachTarget);
		visualization_msgs::Marker l2 = this->getLineMarker(xmin, ymax, zmin, xmax, ymax, zmin, 2, hasReachTarget);
		visualization_msgs::Marker l3 = this->getLineMarker(xmax, ymax, zmin, xmax, ymin, zmin, 3, hasReachTarget);
		visualization_msgs::Marker l4 = this->getLineMarker(xmin, ymin, zmin, xmax, ymin, zmin, 4, hasReachTarget);
		visualization_msgs::Marker l5 = this->getLineMarker(xmin, ymin, zmin, xmin, ymin, zmax, 5, hasReachTarget);
		visualization_msgs::Marker l6 = this->getLineMarker(xmin, ymax, zmin, xmin, ymax, zmax, 6, hasReachTarget);
		visualization_msgs::Marker l7 = this->getLineMarker(xmax, ymax, zmin, xmax, ymax, zmax, 7, hasReachTarget);
		visualization_msgs::Marker l8 = this->getLineMarker(xmax, ymin, zmin, xmax, ymin, zmax, 8, hasReachTarget);
		visualization_msgs::Marker l9 = this->getLineMarker(xmin, ymin, zmax, xmin, ymax, zmax, 9, hasReachTarget);
		visualization_msgs::Marker l10 = this->getLineMarker(xmin, ymax, zmax, xmax, ymax, zmax, 10, hasReachTarget);
		visualization_msgs::Marker l11 = this->getLineMarker(xmax, ymax, zmax, xmax, ymin, zmax, 11, hasReachTarget);
		visualization_msgs::Marker l12 = this->getLineMarker(xmin, ymin, zmax, xmax, ymin, zmax, 12, hasReachTarget);


		// line 1-12
		visVec.push_back(l1);
		visVec.push_back(l2);
		visVec.push_back(l3);
		visVec.push_back(l4);
		visVec.push_back(l5);
		visVec.push_back(l6);
		visVec.push_back(l7);
		visVec.push_back(l8);
		visVec.push_back(l9);
		visVec.push_back(l10);
		visVec.push_back(l11);
		visVec.push_back(l12);

		this->targetVisVec_ = visVec;
	}

	void inspector::updatePathVis(const nav_msgs::Path& path){
		this->inspectionPath_ = path;
	}

	void inspector::publishTargetVis(){
		ros::Rate r (1);
		while (ros::ok()){
			this->targetVisMsg_.markers = this->targetVisVec_;
			this->targetVisPub_.publish(this->targetVisMsg_);
			r.sleep();
		}
	}

	void inspector::publishPathVis(){
		ros::Rate r (10);
		while (ros::ok()){
			this->inspectionPath_.header.stamp = ros::Time::now(); 
			this->inspectionPath_.header.frame_id = "map";
			this->pathPub_.publish(this->inspectionPath_);
			r.sleep();
		}
	}

	geometry_msgs::PoseStamped inspector::getForwardGoal(bool& success){
		octomap::point3d p = this->getPoint3dPos();
	
		// cast a ray along the x direction and check collision
		float res = this->mapRes_;
		octomap::point3d pForward = p;
		while (ros::ok() and not this->checkCollision(pForward)){
			pForward.x() += res;
			// cout << res << endl;
			// cout << pForward << endl;
		}
		octomap::point3d pGoal = pForward;
		pGoal.x() -= res;
		pGoal.x() -= this->safeDist_;
		if (pGoal.x() <= p.x()){ // we don't let the robot move backward somehow
			pGoal.x() = p.x(); // at least stay at the same position
			success = false;
		}
		else{
			if (std::abs(pGoal.x() - p.x()) <= this->forwardMinDist_){ // 
				success = false;
			}
			else{
				success = true;
			}
		}

		geometry_msgs::PoseStamped ps;
		ps.header.frame_id = "map";
		ps.header.stamp = ros::Time::now();
		ps.pose.position.x = pGoal.x();
		ps.pose.position.y = pGoal.y();
		ps.pose.position.z = pGoal.z();
		ps.pose.orientation = this->odom_.pose.pose.orientation;
		
		return ps;
	}

	nav_msgs::Path inspector::getForwardPath(bool& success){
		geometry_msgs::PoseStamped goalPs = this->getForwardGoal(success);
		geometry_msgs::PoseStamped startPs; 
		startPs.pose = this->odom_.pose.pose;
		std::vector<geometry_msgs::PoseStamped> forwardPathVec;
		forwardPathVec.push_back(startPs);
		forwardPathVec.push_back(goalPs);
		nav_msgs::Path forwardPath;
		forwardPath.header.frame_id = "map";
		forwardPath.header.stamp = ros::Time::now();
		forwardPath.poses = forwardPathVec;
		return forwardPath;
	}

	octomap::point3d inspector::sampleNBVGoal(){
		cout << "[AutoFlight]: Start NBV sampling..." << endl;
		std::vector<octomap::point3d> candidates;
		
		// sample points in the space in front of current position
		double xmin, xmax, ymin, ymax, zmin, zmax;
		this->map_->getMetricMax(xmax, ymax, zmax);
		this->map_->getMetricMin(xmin, ymin, zmin);

		double xcurr = this->odom_.pose.pose.position.x;
		std::vector<double> bbox {xcurr + this->safeDist_, xmax, ymin, ymax, this->takeoffHgt_, this->takeoffHgt_};
		double totalReduceFactor = 1.0;
		for (int i=0; i < this->nbvSampleNum_; ++i){
			octomap::point3d pSample = this->randomSample(bbox, totalReduceFactor);
			candidates.push_back(pSample);
		}

		int bestUnknownNum = 0;
		octomap::point3d bestPoint;
		for (octomap::point3d pCandidate : candidates){
			int unknownNum = this->evaluateSample(pCandidate);
			// cout << "P candidate: " << pCandidate << endl;
			// cout << "unknownNum: " << unknownNum << endl;
			if (unknownNum > bestUnknownNum){
				bestPoint = pCandidate;
				bestUnknownNum = unknownNum;
			}
		}
		// cout << "bestPoint: " << bestPoint << endl;
		// cout << "best Unknown: " << bestUnknownNum << endl;
		cout << "[AutoFlight]: sampling done!" << endl;
		return bestPoint;
	}

	bool inspector::inSensorRange(const octomap::point3d& p, const octomap::point3d& pCheck){
		// first check distance
		if (p.distance(pCheck) >= this->sensorRange_){
			return false;
		}

		// then check angle for vertical plane
		octomap::point3d pCheckRay = pCheck - p;
		octomap::point3d pPlaneRay = pCheckRay;
		pPlaneRay.z() = p.z();
		double angleVertical = pCheckRay.angleTo(pPlaneRay);
		if (angleVertical >= this->sensorVerticalAngle_){
			return false;
		}

		return true;
	}

	bool inspector::hasOcclusion(const octomap::point3d& p, const octomap::point3d& pCheck){
		std::vector<octomap::point3d> ray;
		bool success = this->map_->computeRay(p, pCheck, ray); // success does not indicate something
		for (octomap::point3d pRay: ray){
			bool hasCollision = this->checkCollision(pRay);
			if (hasCollision){
				return false;
			}
		}

		return true;
	}

	int inspector::evaluateSample(const octomap::point3d& p){
		int countUnknown = 0;
		// count number of unknowns in the +x direction
		double xmin, xmax, ymin, ymax, zmin, zmax;
		xmin = p.x(); xmax = xmin + this->sensorRange_;
		ymin = p.y() - this->sensorRange_; ymax = p.y() + this->sensorRange_;
		double zRange = this->sensorRange_ * tan(this->sensorVerticalAngle_);
		zmin = p.z(); zmax = p.z() + zRange;
		int xCount = (int) (this->sensorRange_/this->mapRes_) + 1;
		int yCount = (int) (2 * this->sensorRange_/this->mapRes_) + 1;
		int zCount = (int) (zRange/this->mapRes_) + 1;

		for (int xID=0; xID < xCount; ++xID){
			for (int yID=0; yID < yCount; ++yID){
				for (int zID=0; zID < zCount; ++zID){
					octomap::point3d pCheck (xmin + xID * this->mapRes_,
											 ymin + yID * this->mapRes_,
											 zmin + zID * this->mapRes_);
					if (this->inSensorRange(p, pCheck)){
						octomap::OcTreeNode* nptr = this->map_->search(pCheck);
						if (nptr == NULL){ // we only care about unknowns
							if (not this->hasOcclusion(p, pCheck)){
								++countUnknown;
							}
						}
					}
				}
			}
		}
		return countUnknown;
	}

	bool inspector::checkPointSafe(const octomap::point3d& p, double sideSafeReduceFactor){
		bool hasCollision = this->checkCollision(p);
		if (hasCollision){
			return false;
		}

		// check positive x
		double xPlusRes = this->mapRes_;
		double xPlusForward = 0.0;
		octomap::point3d pXPlusCheck = p;
		while (ros::ok() and xPlusForward <= this->safeDist_ + xPlusRes){
			xPlusForward += xPlusRes;
			pXPlusCheck.x() = p.x() + xPlusForward;
			bool hasCollisionXPlus = this->checkCollision(pXPlusCheck);
			if (hasCollisionXPlus){
				return false;
			}
		}


		// check positive y direction (If timeout we have to reduce the criteria for two sides)
		double yPlusRes = this->mapRes_ * sideSafeReduceFactor;
		double yPlusForward = 0.0;
		octomap::point3d pYPlusCheck = p;
		ros::Time yPlusStart = ros::Time::now();
		while (ros::ok() and yPlusForward <= this->safeDist_ + yPlusRes){
			yPlusForward += yPlusRes;
			pYPlusCheck.y() = p.y() + yPlusForward;
			bool hasCollisionYPlus = this->checkCollision(pYPlusCheck);
			if (hasCollisionYPlus){
				return false;
			}
		}


		// check negative y direction
		double yMinusRes = this->mapRes_ * sideSafeReduceFactor;
		double yMinusForward = 0.0;
		octomap::point3d pYMinusCheck = p;
		ros::Time yMinusStart = ros::Time::now();
		while (ros::ok() and yMinusForward <= this->safeDist_ + yMinusRes){
			yMinusForward += yMinusRes;
			pYMinusCheck.y() = p.y() - yMinusForward;
			bool hasCollisionYMinus = this->checkCollision(pYMinusCheck);
			if (hasCollisionYMinus){
				return false;
			}
		}



		return true;
	}

	octomap::point3d inspector::randomSample(const std::vector<double>& bbox, double& totalReduceFactor){
		double xmin, xmax, ymin, ymax, zmin, zmax;
		xmin = bbox[0]; xmax = bbox[1];
		ymin = bbox[2]; ymax = bbox[3];
		zmin = bbox[4]; zmax = bbox[5];

		octomap::point3d safePoint;
		bool hasSafePoint = false;
		ros::Time sampleStart = ros::Time::now();
		int count = 0;
		while (ros::ok() and not hasSafePoint){
			ros::Time sampleCurr = ros::Time::now();
			double t = (sampleCurr - sampleStart).toSec();
			if (t >= this->sampleTimeout_){
				// cout << "total enter: " << count << endl;
				// cout << "total reduce factor: " << totalReduceFactor << endl;
				totalReduceFactor *= this->reduceFactor_;
				sampleStart = ros::Time::now();
				++count;
				cout << "[AutoFlight]: Sample timeout. Reduce side safety constraint to " << totalReduceFactor*100 << "%." << endl; 
			}

			safePoint.x() = randomNumber(xmin, xmax);
			safePoint.y() = randomNumber(ymin, ymax);
			safePoint.z() = randomNumber(zmin, zmax);

			// check safety of current point
			hasSafePoint = this->checkPointSafe(safePoint, totalReduceFactor);
		}
		return safePoint;
	}

	octomap::point3d inspector::getPoint3dPos(){
		float x, y, z;
		x = this->odom_.pose.pose.position.x;
		y = this->odom_.pose.pose.position.y;
		z = this->odom_.pose.pose.position.z;
		return octomap::point3d (x, y, z);
	}

	std::vector<double> inspector::getVecPos(){
		double x, y, z;
		x = this->odom_.pose.pose.position.x;
		y = this->odom_.pose.pose.position.y;
		z = this->odom_.pose.pose.position.z;
		std::vector<double> vecPos {x, y, z};
		return vecPos;
	}

	bool inspector::checkCollision(const octomap::point3d &p, bool ignoreUnknown){
		double xmin, xmax, ymin, ymax, zmin, zmax; // bounding box for collision checking
		xmin = p.x() - this->collisionBox_[0]/2.0; xmax = p.x() + this->collisionBox_[0]/2.0;
		ymin = p.y() - this->collisionBox_[1]/2.0; ymax = p.y() + this->collisionBox_[1]/2.0;
		zmin = p.z() - this->collisionBox_[2]/2.0; zmax = p.z() + this->collisionBox_[2]/2.0;

		int xNum = (xmax - xmin)/this->mapRes_;
		int yNum = (ymax - ymin)/this->mapRes_;
		int zNum = (zmax - zmin)/this->mapRes_;

		int xID, yID, zID;
		for (xID=0; xID<=xNum; ++xID){
			for (yID=0; yID<=yNum; ++yID){
				for (zID=0; zID<=zNum; ++zID){
					if (this->checkCollisionPoint(octomap::point3d(xmin+xID*this->mapRes_, ymin+yID*this->mapRes_, zmin+zID*this->mapRes_), ignoreUnknown)){
						return true;
					}
				}
			}
		}
		return false;
	}

	bool inspector::checkCollisionPoint(const octomap::point3d &p, bool ignoreUnknown){
		octomap::OcTreeNode* nptr = this->map_->search(p);
		if (nptr == NULL){
			if (not ignoreUnknown){
				return true;
			}
			else{
				return false;
			}
		}
		return this->map_->isNodeOccupied(nptr);
	}

	void inspector::setSurroundingFree(const octomap::point3d& p){
		const float logOddsFree = octomap::logodds(0.1);
		double xmin, xmax, ymin, ymax, zmin, zmax; // bounding box for collision checking
		xmin = p.x() - this->collisionBox_[0]/2; xmax = p.x() + this->collisionBox_[0]/2;
		ymin = p.y() - this->collisionBox_[1]/2; ymax = p.y() + this->collisionBox_[1]/2;
		zmin = p.z() - this->collisionBox_[2]/2; zmax = p.z() + this->collisionBox_[2]/2;

		int xNum = (xmax - xmin)/this->mapRes_;
		int yNum = (ymax - ymin)/this->mapRes_;
		int zNum = (zmax - zmin)/this->mapRes_;

		int xID, yID, zID;
		for (xID=0; xID<=xNum; ++xID){
			for (yID=0; yID<=yNum; ++yID){
				for (zID=0; zID<=zNum; ++zID){
					octomap::point3d pCheck (xmin+xID*this->mapRes_, ymin+yID*this->mapRes_, zmin+zID*this->mapRes_);
					octomap::OcTreeNode* nptr = this->map_->search(pCheck);
					if (nptr == NULL){
						this->map_->setNodeValue(pCheck, logOddsFree);
					}
				}
			}
		}		
	}


	double inspector::findTargetRangeAxis(const octomap::point3d& pStart, const octomap::point3d& direction, std::vector<octomap::point3d>& resultVec){
		resultVec.clear();
		bool stopCriteria = false;
		int count = 0;
		while (ros::ok() and not stopCriteria){
			float coeff = count * this->mapRes_;
			octomap::point3d delta = direction * coeff;
			octomap::point3d pCheck = pStart + delta;
			bool hasCollision = this->checkCollisionPoint(pCheck, true);
			if (hasCollision){
				resultVec.push_back(pCheck);
			}
			else{
				stopCriteria = true;
			}
			++count;
		}
		octomap::point3d pLast = resultVec[resultVec.size()-1];
		double dist = 0.0;
		if (direction.x() == 1){
			dist = pLast.x() + this->mapRes_;
		}
		else if (direction.x() == -1){
			dist = pLast.x() - this->mapRes_;
		}
		else if (direction.y() == 1){
			dist = pLast.y() + this->mapRes_;
		}
		else if (direction.y() == -1){
			dist = pLast.y() - this->mapRes_;
		}
		else if (direction.z() == 1){
			dist = pLast.z() + this->mapRes_;
			if (dist <= 0){
				dist = 0;
			}
		}
		else if (direction.z() == -1){
			dist = pLast.z() - this->mapRes_;
			if (dist <= 0){
				dist = 0;
			}
		}
		else{
			cout << "[AutoFlight]: Invalid Direction!" << endl;
		}

		return dist;
	}

	double inspector::findTargetRange(std::vector<double>& range){ // return the area of the target range
		// 1. find the collision point at current location
		octomap::point3d pCurr = this->getPoint3dPos();
		octomap::point3d xPlusDirection (1.0, 0, 0); // along the x axis
		octomap::point3d pCollision;
		bool hitTarget = this->map_->castRay(pCurr, xPlusDirection, pCollision);
		if (not hitTarget){
			range.push_back(0.0);
			range.push_back(0.0);
			range.push_back(0.0);
			range.push_back(0.0);
			range.push_back(0.0);
			range.push_back(0.0);
			return 0.0; // this means the front is unknown
		}
		else{
			// instead of only using the collision point, we check all occupied voxels after collision point
			std::vector<octomap::point3d> pCheckVec;
			this->findTargetRangeAxis(pCollision, xPlusDirection, pCheckVec);
	

			// go through y and z direction of all points in checklist and obtain the maximum range
			octomap::point3d yPlusDirection (0, 1.0, 0);
			octomap::point3d yMinusDirection = -yPlusDirection;
			octomap::point3d zPlusDirection (0, 0, 1.0);
			octomap::point3d zMinusDirection = -zPlusDirection;

			std::vector<double> yminVec, ymaxVec, zminVec, zmaxVec;
			for (octomap::point3d pCheck : pCheckVec){
				std::vector<octomap::point3d> resultVec; // dummy vairable
				double yminTemp = this->findTargetRangeAxis(pCheck, yMinusDirection, resultVec);
				double ymaxTemp = this->findTargetRangeAxis(pCheck, yPlusDirection, resultVec);
				double zminTemp = this->findTargetRangeAxis(pCheck, zMinusDirection, resultVec);
				double zmaxTemp = this->findTargetRangeAxis(pCheck, zPlusDirection, resultVec);
				yminVec.push_back(yminTemp);
				ymaxVec.push_back(ymaxTemp);
				zminVec.push_back(zminTemp);
				zmaxVec.push_back(zmaxTemp);
			}
			double xmin = pCheckVec[0].x() - this->mapRes_;
			double xmax = pCheckVec[pCheckVec.size()-1].x() + this->mapRes_;
			double ymin = *(std::min_element(yminVec.begin(), yminVec.end()));
			double ymax = *(std::max_element(ymaxVec.begin(), ymaxVec.end()));
			double zmin = *(std::min_element(zminVec.begin(), zminVec.end()));
			double zmax = *(std::max_element(zmaxVec.begin(), zmaxVec.end())); 

			range.clear();
			range.push_back(xmin);
			range.push_back(xmax);
			range.push_back(ymin);
			range.push_back(ymax);
			range.push_back(zmin);
			range.push_back(zmax);

			double crossSectionArea = (zmax - zmin) * (ymax - ymin);
			return crossSectionArea;
		}
	}

	octomap::point3d inspector::findInspectionStartPoint(){
		octomap::point3d pCurr = this->getPoint3dPos();

		// check along +y axis
		bool hasCollision = false;
		octomap::point3d pCheck = pCurr;
		while (ros::ok() and not hasCollision){
			pCheck.y() += this->mapRes_;
			hasCollision = this->checkCollision(pCheck);
		}
		octomap::point3d pChecklimit = pCheck;
		pChecklimit.y() -= this->mapRes_;
		pChecklimit.y() -= this->safeDist_; 

		return pChecklimit;
	}

	std::vector<octomap::point3d> inspector::getInspectionLimit(const octomap::point3d& p){
		std::vector<octomap::point3d> limitVec;

		// Plus Y direction
		octomap::point3d pCheckPlus = p;
		bool hasCollisionPlus = false;
		double res = this->mapRes_;
		while (ros::ok() and not hasCollisionPlus){
			pCheckPlus.y() += res;
			hasCollisionPlus = this->checkCollision(pCheckPlus, true);
		}
		octomap::point3d pLimitPlus = pCheckPlus;
		pLimitPlus.y() -= res;
		pLimitPlus.y() -= this->safeDist_;

		// Minus Y direction
		octomap::point3d pCheckMinus = p;
		bool hasCollisionMinus = false;
		while (ros::ok() and not hasCollisionMinus){
			pCheckMinus.y() -=  res;
			hasCollisionMinus = this->checkCollision(pCheckMinus, true);
		}
		octomap::point3d pLimitMinus = pCheckMinus;
		pLimitMinus.y() += res;
		pLimitMinus.y() += this->safeDist_;

		// check the distance between two limits and the closer one gets first in the vec
		double distPlus = p.distance(pLimitPlus);
		double distMinus = p.distance(pLimitMinus);
		if (distPlus >= distMinus){
			limitVec.push_back(pLimitMinus);
			limitVec.push_back(pLimitPlus);
		}
		else{
			limitVec.push_back(pLimitPlus);
			limitVec.push_back(pLimitMinus);
		}

		// Check whether it is larger than our predefined space
		double width = std::abs(pLimitPlus.y() - pLimitMinus.y());
		if (width >= this->maxTargetWidth_){
			cout << "[AutoFlight]: Manually entered width is less than actual width. Will adjust limits." << endl;
			double diff = width - this->maxTargetWidth_;
			pLimitPlus.y() -= diff/2;
			pLimitMinus.y() += diff/2;
		}

		// cout << "P Limit Plus: " << pLimitPlus << endl;
		// cout << "p Limit Minus: " << pLimitMinus << endl;
		return limitVec;
	}

	nav_msgs::Path inspector::generateZigZagPath(){
		octomap::point3d pCurr = this->getPoint3dPos();
		octomap::point3d pInspection = this->findInspectionStartPoint();
		nav_msgs::Path zzPath;
		std::vector<geometry_msgs::PoseStamped> zzPathVec;

		// add current pose and first inspection pose to the path
		geometry_msgs::PoseStamped psCurr = this->pointToPose(pCurr);
		geometry_msgs::PoseStamped psInspection = this->pointToPose(pInspection);
		zzPathVec.push_back(psCurr);
		zzPathVec.push_back(psInspection);

		octomap::point3d pInspectionHgt = pInspection;
		double height = pInspectionHgt.z();
		while (ros::ok() and not (height <= this->takeoffHgt_+this->mapRes_)){
			std::vector<octomap::point3d> pLimitCheck = this->getInspectionLimit(pInspectionHgt);
			geometry_msgs::PoseStamped psLimit1 = this->pointToPose(pLimitCheck[0]);
			geometry_msgs::PoseStamped psLimit2 = this->pointToPose(pLimitCheck[1]);
			zzPathVec.push_back(psLimit1);
			zzPathVec.push_back(psLimit2);
			pInspectionHgt = pLimitCheck[1];
			pInspectionHgt.z() -= this->descendHgt_;

			height = pInspectionHgt.z();
			geometry_msgs::PoseStamped psLimit2Lower = this->pointToPose(pInspectionHgt);
			if (not (height <= this->takeoffHgt_+this->mapRes_)){
				zzPathVec.push_back(psLimit2Lower); 
			}
		}
		
		zzPath.poses = zzPathVec;
		return zzPath;
	}

	geometry_msgs::PoseStamped inspector::pointToPose(const octomap::point3d& p){
		geometry_msgs::PoseStamped ps;
		ps.pose = this->odom_.pose.pose;
		ps.pose.position.x = p.x();
		ps.pose.position.y = p.y();
		ps.pose.position.z = p.z();
		return ps;
	}

	void inspector::moveToPos(const geometry_msgs::Point& position){
		geometry_msgs::PoseStamped psStart, psGoal;
		psGoal.pose.position = position;
		psGoal.pose.orientation = this->odom_.pose.pose.orientation;
		psStart.pose = this->odom_.pose.pose;

		std::vector<geometry_msgs::PoseStamped> linePathVec;
		linePathVec.push_back(psStart);
		linePathVec.push_back(psGoal);
		nav_msgs::Path linePath;
		linePath.poses = linePathVec;

		this->updatePathVis(linePath);

		this->executeWaypointPath(linePath, true);
	}

	void inspector::moveToAngle(const geometry_msgs::Quaternion& quat){
		double yawTgt = AutoFlight::rpy_from_quaternion(quat);
		double yawCurr = AutoFlight::rpy_from_quaternion(this->odom_.pose.pose.orientation);
		geometry_msgs::PoseStamped ps;
		ps.pose = this->odom_.pose.pose;
		ps.pose.orientation = quat;

		double yawDiff = yawTgt - yawCurr; // difference between yaw
		double direction;
		double yawDiffAbs = std::abs(yawDiff);
		if ((yawDiffAbs <= PI_const) and (yawDiff>0)){
			direction = 1.0; // counter clockwise
		} 
		else if ((yawDiffAbs <= PI_const) and (yawDiff<0)){
			direction = -1.0; // clockwise
		}
		else if ((yawDiffAbs > PI_const) and (yawDiff>0)){
			direction = -1.0; // rotate in clockwise direction
			yawDiffAbs = 2 * PI_const - yawDiffAbs;
		}
		else if ((yawDiffAbs > PI_const) and (yawDiff<0)){
			direction = 1.0; // counter clockwise
			yawDiffAbs = 2 * PI_const - yawDiffAbs;
		}


		double t = 0.0;
		double startTime = 0.0; double endTime = yawDiffAbs/this->desiredAngularVel_;
		ros::Time tStart = ros::Time::now();
		ros::Rate r (1.0/this->sampleTime_);
		while (ros::ok() and not this->isReach(ps)){
			if (t >= endTime){
				this->updateTarget(ps);
				r.sleep();
				continue;
			}

			ros::Time tCurr = ros::Time::now();
			t = (tCurr - tStart).toSec();
			double currYawTgt = yawCurr + (double) direction * (t-startTime)/(endTime-startTime) * yawDiffAbs;
			geometry_msgs::Quaternion quatT = trajPlanner::quaternion_from_rpy(0, 0, currYawTgt);
			geometry_msgs::PoseStamped psT = ps;
			psT.pose.orientation = quatT;
			this->updateTarget(psT);
			r.sleep();
		}
	}

	nav_msgs::Path inspector::checkSurroundingsLeft(){
		octomap::point3d pLeftOrigin = this->getPoint3dPos();
		bool hasCollision = false;
		double res = this->mapRes_;
		octomap::point3d pCheck = pLeftOrigin;
		while (ros::ok() and not hasCollision){ 
			pCheck.y() += res;
			hasCollision = this->checkCollision(pCheck); 
		}
		octomap::point3d pLeftGoal = pCheck;
		pLeftGoal.y() -= res;
		pLeftGoal.y() -= this->safeDist_;

		if (pLeftGoal.y() <= pLeftOrigin.y()){
			pLeftGoal.y() = pLeftOrigin.y();
		}

		nav_msgs::Path leftCheckPath;
		std::vector<geometry_msgs::PoseStamped> leftCheckPathVec;
		geometry_msgs::PoseStamped psStart, psGoal;
		psStart.pose = this->odom_.pose.pose;
		psGoal.pose = this->odom_.pose.pose;
		psGoal.pose.position.x = pLeftGoal.x();
		psGoal.pose.position.y = pLeftGoal.y();
		psGoal.pose.position.z = pLeftGoal.z();
		leftCheckPathVec.push_back(psStart);
		leftCheckPathVec.push_back(psGoal);
		leftCheckPath.poses = leftCheckPathVec;
		return leftCheckPath;
	}

	nav_msgs::Path inspector::checkSurroundingsRight(){
		octomap::point3d pRightOrigin = this->getPoint3dPos();
		bool hasCollision = false;
		double res = this->mapRes_;
		octomap::point3d pCheck = pRightOrigin;
		while (ros::ok() and not hasCollision){ 
			pCheck.y() -= res;
			hasCollision = this->checkCollision(pCheck); 
		}
		octomap::point3d pRightGoal = pCheck;
		pRightGoal.y() += res;
		pRightGoal.y() += this->safeDist_;

		if (pRightGoal.y() >= pRightOrigin.y()){
			pRightGoal.y() = pRightOrigin.y();
		}

		nav_msgs::Path rightCheckPath;
		std::vector<geometry_msgs::PoseStamped> rightCheckPathVec;
		geometry_msgs::PoseStamped psStart, psGoal;
		psStart.pose = this->odom_.pose.pose;
		psGoal.pose = this->odom_.pose.pose;
		psGoal.pose.position.x = pRightGoal.x();
		psGoal.pose.position.y = pRightGoal.y();
		psGoal.pose.position.z = pRightGoal.z();
		rightCheckPathVec.push_back(psStart);
		rightCheckPathVec.push_back(psGoal);
		rightCheckPath.poses = rightCheckPathVec;
		return rightCheckPath;
	}

	void inspector::executeWaypointPath(const nav_msgs::Path& path, bool useYaw){
		this->pwlPlanner_->updatePath(path, useYaw);

		double t = 0.0;
		ros::Time tStart = ros::Time::now();
		ros::Rate r (1.0/this->sampleTime_);
		geometry_msgs::PoseStamped psGoal = path.poses.back();
		while (ros::ok() and not this->isReach(psGoal)){
			ros::Time tCurr = ros::Time::now();
			t = (tCurr - tStart).toSec();
			geometry_msgs::PoseStamped psT = this->pwlPlanner_->getPose(t);
			this->updateTarget(psT);
			r.sleep();
		}
	}
}

