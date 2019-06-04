#pragma once

/************************************************************************/
/* Customized active stereo sensor with different intrinsics and API    */
/************************************************************************/

#include "DepthSensor.h"




class ChristophSensor : public DepthSensor
{
public:

	//! Constructor
	ChristophSensor();

	//! Destructor; releases allocated ressources
	~ChristophSensor();

	//! Initializes the sensor
	createFirstConnected();

	//! Reads the next depth (and color) frame
	processDepth();

	processColor() {
		//everything done in process depth since order is relevant (color must be read first)
		return S_OK;
	}

	ChristophSensor::toggleNearMode() {
		return S_OK;
	}

	//! Toggle enable auto white balance
	toggleAutoWhiteBalance() {
		return S_OK;
	}

	bool isKinect4Windows() {
		return true;
	}

private:
	StringCounter* m_StringCounter;
};
