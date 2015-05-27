#include "Body.h"
#include "ofMain.h"

#define CHECK_OPEN if(!this->reader) { OFXKINECTFORWINDOWS2_ERROR << "Failed : Reader is not open"; }

namespace ofxKinectForWindows2 {
	namespace Source {
		//----------
		string Body::getTypeName() const {
			return "Body";
		}

		//----------
		const vector<Data::Body> & Body::getBodies() const {
			return bodies;
		}

		//----------
		const vector< pair<JointType, JointType> > & Body::getBonesDef() const {
			return bonesDef;
		}

		//----------
		ofMatrix4x4 Body::getFloorTransform() {
			ofNode helper;
			helper.lookAt(ofVec3f(floorClipPlane.x, floorClipPlane.z, -floorClipPlane.y));
			helper.boom(-floorClipPlane.w);
			ofMatrix4x4 transform = helper.getGlobalTransformMatrix().getInverse();
			return transform;
		}

		//----------
		void Body::init(IKinectSensor * sensor) {
			this->reader = NULL;
			try {
				IBodyFrameSource * source = NULL;

				if (FAILED(sensor->get_BodyFrameSource(&source))) {
					throw(Exception("Failed to initialise BodyFrame source"));
				}

				if (FAILED(source->OpenReader(&this->reader))) {
					throw(Exception("Failed to initialise BodyFrame reader"));
				}

				SafeRelease(source);

				if (FAILED(sensor->get_CoordinateMapper(&this->coordinateMapper))) {
					throw(Exception("Failed to acquire coordinate mapper"));
				}

				bodies.resize(BODY_COUNT);
				gestureResults.resize(BODY_COUNT);
				for (int i =0; i < BODY_COUNT; i++) { 
					gestureResults[i].id = -1;
					gestureResults[i].value =  false; 
				}
				useGesture = false;
				initBonesDefinition();
			}
			catch (std::exception & e) {
				SafeRelease(this->reader);
				throw (e);
			}
		}

		//----------
		void Body::initBonesDefinition() {
#define BONEDEF_ADD(J1, J2) bonesDef.push_back( make_pair<JointType, JointType>(JointType_ ## J1, JointType_ ## J2) )
			// Torso
			BONEDEF_ADD	(Head,			Neck);
			BONEDEF_ADD	(Neck,			SpineShoulder);
			BONEDEF_ADD	(SpineShoulder,	SpineMid);
			BONEDEF_ADD	(SpineMid,		SpineBase);
			BONEDEF_ADD	(SpineShoulder,	ShoulderRight);
			BONEDEF_ADD	(SpineShoulder,	ShoulderLeft);
			BONEDEF_ADD	(SpineBase,		HipRight);
			BONEDEF_ADD	(SpineBase,		HipLeft);

			// Right Arm
			BONEDEF_ADD	(ShoulderRight,	ElbowRight);
			BONEDEF_ADD	(ElbowRight,	WristRight);
			BONEDEF_ADD	(WristRight,	HandRight);
			BONEDEF_ADD	(HandRight,		HandTipRight);
			BONEDEF_ADD	(WristRight,	ThumbRight);

			// Left Arm
			BONEDEF_ADD	(ShoulderLeft,	ElbowLeft);
			BONEDEF_ADD	(ElbowLeft,		WristLeft);
			BONEDEF_ADD	(WristLeft,		HandLeft);
			BONEDEF_ADD	(HandLeft,		HandTipLeft);
			BONEDEF_ADD	(WristLeft,		ThumbLeft);

			// Right Leg
			BONEDEF_ADD	(HipRight,		KneeRight);
			BONEDEF_ADD	(KneeRight,		AnkleRight);
			BONEDEF_ADD	(AnkleRight,	FootRight);

			// Left Leg
			BONEDEF_ADD	(HipLeft,	KneeLeft);
			BONEDEF_ADD	(KneeLeft,	AnkleLeft);
			BONEDEF_ADD	(AnkleLeft,	FootLeft);
#undef BONEDEF_ADD
		}
		//----------
		
	bool Body::setupVGBF(IKinectSensor * sensor, wstring gbd){
		wstring file = gbd.append(L".gbd");
			if(SUCCEEDED(CreateVisualGestureBuilderDatabaseInstanceFromFile(file.c_str(), &database))){
				for (int i = 0; i < BODY_COUNT; i++){
					if(FAILED(CreateVisualGestureBuilderFrameSource(sensor, 0, &pGestureSource[i] ))){
						throw(Exception("Failed to create VisualGestureBuilderFrameSource"));
						return false;
					}

					if(FAILED(pGestureSource[i]->OpenReader(&pGestureReader[i]))){
						throw(Exception("Failed to open reader"));
						return false;
					}
				}
				UINT counts;
				database->get_AvailableGesturesCount(&counts);
				pGesture.resize(counts);
				database->get_AvailableGestures(counts, &pGesture[0]);

				for (int i = 0; i < counts; i++){
					GestureType t;
					pGesture[i]->get_GestureType(&t);
					if(pGesture[i] != nullptr ){
						for (int j = 0; j < BODY_COUNT; j++){
							if(FAILED(pGestureSource[j]->AddGesture(pGesture[i]))){
								throw(Exception("Failed to add gesture"));
								return false;
							}
							if(FAILED(pGestureSource[j]->SetIsEnabled(pGesture[i], true))){
								throw(Exception("Failed to setup"));
								return false;
							}
						}
					}else{
						throw(Exception("gesture is null"));
						return false;
					}
				}
				useGesture = true;
				return useGesture;
			}
			return false;
		}
		
		//----------
		void Body::update() {
			CHECK_OPEN
			
			IBodyFrame * frame = NULL;
			IFrameDescription * frameDescription = NULL;
			try {
				//acquire frame
				if (FAILED(this->reader->AcquireLatestFrame(&frame))) {
					return; // we often throw here when no new frame is available
				}
				INT64 nTime = 0;
				if (FAILED(frame->get_RelativeTime(&nTime))) {
					throw Exception("Failed to get relative time");
				}
				
				if (FAILED(frame->get_FloorClipPlane(&floorClipPlane))){
					throw(Exception("Failed to get floor clip plane"));
				}

				IBody* ppBodies[BODY_COUNT] = {0};
				if (FAILED(frame->GetAndRefreshBodyData(_countof(ppBodies), ppBodies))){
					throw Exception("Failed to refresh body data");
				}

				for (int i = 0; i < BODY_COUNT; ++i)
				{
					auto & body = bodies[i];
					body.clear();

					IBody* pBody = ppBodies[i];
					if (pBody)
					{
						BOOLEAN bTracked = false;
						if (FAILED(pBody->get_IsTracked(&bTracked))) {
							throw Exception("Failed to get tracking status");
						}
						body.tracked = bTracked;

						if (bTracked)
						{
							// retrieve tracking id

							UINT64 trackingId = -1;

							if (FAILED(pBody->get_TrackingId(&trackingId))) {
								throw Exception("Failed to get tracking id");
							}

							body.trackingId = trackingId;
							if (useGesture){ pGestureSource[i]->put_TrackingId(trackingId); }
							// retrieve joint position & orientation

							_Joint joints[JointType_Count];
							_JointOrientation jointsOrient[JointType_Count];

							if (FAILED(pBody->GetJoints(JointType_Count, joints))){
								throw Exception("Failed to get joints");
							}
							if (FAILED(pBody->GetJointOrientations(JointType_Count, jointsOrient))){
								throw Exception("Failed to get joints orientation");
							}

							for (int j = 0; j < JointType_Count; ++j) {
								body.joints[joints[j].JointType] = Data::Joint(joints[j], jointsOrient[j]);
							}

							// retrieve hand states

							HandState leftHandState = HandState_Unknown;
							HandState rightHandState = HandState_Unknown;

							if (FAILED(pBody->get_HandLeftState(&leftHandState))){
								throw Exception("Failed to get left hand state");
							}
							if (FAILED(pBody->get_HandRightState(&rightHandState))){
								throw Exception("Failed to get right hand state");
							}

							body.leftHandState = leftHandState;
							body.rightHandState = rightHandState;
							if(useGesture){
								IVisualGestureBuilderFrame* pGestureFrame = nullptr;
								if(SUCCEEDED(pGestureReader[i]->CalculateAndAcquireLatestFrame(&pGestureFrame))){
									BOOLEAN bGestureTracked = false;
									pGestureFrame->get_IsTrackingIdValid(&bGestureTracked);
									if(bGestureTracked){
										IDiscreteGestureResult* pGestureResult = nullptr;
										for (int j = 0; j < pGesture.size(); j++){
											if(SUCCEEDED(pGestureFrame->get_DiscreteGestureResult(pGesture[j], &pGestureResult))){
												BOOLEAN bDetected = false;
												pGestureResult->get_Detected(&bDetected);
												gestureResults[i].value = bDetected;
												if(bDetected){
													gestureResults[i].id = j;
													UINT64 num;
													pGestureFrame->get_TrackingId(&num);
													ofLog(OF_LOG_VERBOSE, "gesture:"+ofToString(j)+", id:"+ofToString(num));
												}
											}
										}
										SafeRelease( pGestureResult );
									}
								}
								SafeRelease( pGestureFrame );
							}
							//---------
						}
					}
				}

				for (int i = 0; i < _countof(ppBodies); ++i)
				{
					SafeRelease(ppBodies[i]);
				}
			}
			catch (std::exception & e) {
				OFXKINECTFORWINDOWS2_ERROR << e.what();
			}
			SafeRelease(frameDescription);
			SafeRelease(frame);
		}

		//----------
		void Body::drawBodies() {
			int bodyIndex = 0;
			for (auto & body : this->bodies) {
				if (!body.tracked) {
					continue;
				}

				ofPushStyle();
				ofSetLineWidth(3.0f);

				auto col = ofColor(200, 100, 100);
				col.setHue(bodyIndex * 60);
				ofSetColor(col);

				for (auto & bone : this->bonesDef) {
					ofLine(body.joints[bone.first].getPosition(), body.joints[bone.second].getPosition());
				}
				ofPopStyle();

				bodyIndex++;
			}
		}

		//----------
		void Body::drawFloor() {
			ofPushMatrix();
			ofRotate(90, 0, 0, 1);
			ofMultMatrix(this->getFloorTransform());
			ofDrawGridPlane(5.0f);
			ofPopMatrix();
		}

		//----------
		void Body::drawProjected(int x, int y, int width, int height, ProjectionCoordinates proj) {
			ofPushStyle();
			int w, h;
			switch (proj) {
			case ColorCamera: w = 1920; h = 1080; break;
			case DepthCamera: w = 512; h = 424; break;
			}

			for (auto & body : bodies) {
				if (!body.tracked) continue;

				map<JointType, ofVec2f> jntsProj;

				for (auto & j : body.joints) {
					ofVec2f & p = jntsProj[j.second.getType()] = ofVec2f();

					TrackingState state = j.second.getTrackingState();
					if (state == TrackingState_NotTracked) continue;

					p.set(j.second.getProjected(coordinateMapper, proj));
					p.x = x + p.x / w * width;
					p.y = y + p.y / h * height;

					int radius = (state == TrackingState_Inferred) ? 2 : 8;
					ofSetColor(0, 255, 0);
					ofCircle(p.x, p.y, radius);
				}
				
				for (auto & bone : bonesDef) {
					drawProjectedBone(body.joints, jntsProj, bone.first, bone.second);
				}

				drawProjectedHand(body.leftHandState, jntsProj[JointType_HandLeft]);
				drawProjectedHand(body.rightHandState, jntsProj[JointType_HandRight]);
			}

			ofPopStyle();
		}

		//----------
		void Body::drawProjectedBone(map<JointType, Data::Joint> & pJoints, map<JointType, ofVec2f> & pJointPoints, JointType joint0, JointType joint1){
			TrackingState ts1 = pJoints[joint0].getTrackingState();
			TrackingState ts2 = pJoints[joint1].getTrackingState();
			if (ts1 == TrackingState_NotTracked || ts2 == TrackingState_NotTracked) return;
			if (ts1 == TrackingState_Inferred && ts2 == TrackingState_Inferred) return;

			int thickness = 5;
			ofSetColor(0, 255, 0);
			if (ts1 == TrackingState_Inferred || ts2 == TrackingState_Inferred) {
				thickness = 2;
				ofSetColor(0, 128, 0);
			}
			ofSetLineWidth(thickness);
			ofLine(pJointPoints[joint0], pJointPoints[joint1]);
		}

		//----------
		void Body::drawProjectedHand(HandState handState, ofVec2f & handPos){
			ofColor color;
			switch (handState)
			{
			case HandState_Unknown: case HandState_NotTracked:
				return;
			case HandState_Open:
				color = ofColor(0, 255, 0, 80);
				break;
			case HandState_Closed :
				color = ofColor(255, 255, 0, 80);
				break;
			case HandState_Lasso:
				color = ofColor(0, 255, 255, 80);
				break;
			}
			ofEnableAlphaBlending();
			ofSetColor(color);
			ofCircle(handPos, 50);
			ofDisableAlphaBlending();
		}

		//----------
		ofVec2f Body::getJoint(int i, JointType j, ProjectionCoordinates proj) {
			return this->bodies[i].joints[j].getProjected(coordinateMapper, proj);
		}
	}
}