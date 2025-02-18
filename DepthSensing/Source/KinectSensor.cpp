


#include "KinectSensor.h"

KinectSensor::KinectSensor()
{
	// get resolution as DWORDS, but store as LONGs to avoid casts later
	DWORD width = 0;
	DWORD height = 0;

	NuiImageResolutionToSize(cDepthResolution, width, height);
	unsigned int depthWidth = static_cast<unsigned int>(width);
	unsigned int depthHeight = static_cast<unsigned int>(height);

	NuiImageResolutionToSize(cColorResolution, width, height);
	unsigned int colorWidth  = static_cast<unsigned int>(width);
	unsigned int colorHeight = static_cast<unsigned int>(height);

	DepthSensor::init(depthWidth, depthHeight, colorWidth, colorHeight);

	m_colorToDepthDivisor = colorWidth/depthWidth;

	m_bDepthReceived = false;
	m_bColorReceived = false;

	m_hNextDepthFrameEvent = INVALID_HANDLE_VALUE;
	m_pDepthStreamHandle = INVALID_HANDLE_VALUE;
	m_hNextColorFrameEvent = INVALID_HANDLE_VALUE;
	m_pColorStreamHandle = INVALID_HANDLE_VALUE;

	m_colorCoordinates = new LONG[depthWidth*depthHeight*2];

	m_bDepthImageIsUpdated = false;
	m_bDepthImageCameraIsUpdated = false;
	m_bNormalImageCameraIsUpdated = false;

	initializeIntrinsics(2.0f*NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240, 2.0f*NUI_CAMERA_SKELETON_TO_DEPTH_IMAGE_MULTIPLIER_320x240, 320.0f, 240.0f);
}

KinectSensor::~KinectSensor()
{
	if (NULL != m_pNuiSensor)
	{
		m_pNuiSensor->NuiShutdown();
		m_pNuiSensor->Release();
	}

	CloseHandle(m_hNextDepthFrameEvent);
	CloseHandle(m_hNextColorFrameEvent);

	// done with pixel data
	delete[] m_colorCoordinates;
}

KinectSensor::createFirstConnected()
{
	INuiSensor* pNuiSensor = NULL;
	hr = S_OK;

	int iSensorCount = 0;
	hr = NuiGetSensorCount(&iSensorCount);
	if (FAILED(hr) ) { return hr; }

	// Look at each Kinect sensor
	for (int i = 0; i < iSensorCount; ++i)
	{
		// Create the sensor so we can check status, if we can't create it, move on to the next
		hr = NuiCreateSensorByIndex(i, &pNuiSensor);
		if (FAILED(hr))
		{
			continue;
		}

		// Get the status of the sensor, and if connected, then we can initialize it
		hr = pNuiSensor->NuiStatus();
		if (S_OK == hr)
		{
			m_pNuiSensor = pNuiSensor;
			break;
		}

		// This sensor wasn't OK, so release it since we're not using it
		pNuiSensor->Release();
	}

	if (NULL == m_pNuiSensor)
	{
		return E_FAIL;
	}

	// Initialize the Kinect and specify that we'll be using depth
	//hr = m_pNuiSensor->NuiInitialize(NUI_INITIALIZE_FLAG_USES_COLOR | NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX); 
	hr = m_pNuiSensor->NuiInitialize(NUI_INITIALIZE_FLAG_USES_COLOR | NUI_INITIALIZE_FLAG_USES_DEPTH); 
	if (FAILED(hr) ) { return hr; }

	// Create an event that will be signaled when depth data is available
	m_hNextDepthFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	// Open a depth image stream to receive depth frames
	hr = m_pNuiSensor->NuiImageStreamOpen(
		NUI_IMAGE_TYPE_DEPTH,
		//NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX,
		cDepthResolution,
		(8000 << NUI_IMAGE_PLAYER_INDEX_SHIFT),
		2,
		m_hNextDepthFrameEvent,
		&m_pDepthStreamHandle);
	if (FAILED(hr) ) { return hr; }

	// Create an event that will be signaled when color data is available
	m_hNextColorFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	// Open a color image stream to receive color frames
	hr = m_pNuiSensor->NuiImageStreamOpen(
		NUI_IMAGE_TYPE_COLOR,
		cColorResolution,
		0,
		2,
		m_hNextColorFrameEvent,
		&m_pColorStreamHandle );
	if (FAILED(hr) ) { return hr; }

	INuiColorCameraSettings* colorCameraSettings;
	hrFlag = m_pNuiSensor->NuiGetColorCameraSettings(&colorCameraSettings);

	if (hr != E_NUI_HARDWARE_FEATURE_UNAVAILABLE)
	{
		m_kinect4Windows = true;
	}

	// Get offset x, y coordinates for color in depth space
	// This will allow us to later compensate for the differences in location, angle, etc between the depth and color cameras
	m_pNuiSensor->NuiImageGetColorPixelCoordinateFrameFromDepthPixelFrameAtResolution(
		cColorResolution,
		cDepthResolution,
		getDepthWidth()*getDepthHeight(),
		m_depthD16,
		getDepthWidth()*getDepthHeight()*2,
		m_colorCoordinates
		);

	// Start with near mode on (if possible)
	m_bNearMode = false;
	if (m_kinect4Windows) {
		toggleNearMode();
	}

	return hr;
}

KinectSensor::processDepth()
{
	hr = S_OK;

	//wait until data is available
	if (!(WAIT_OBJECT_0 == WaitForSingleObject(m_hNextDepthFrameEvent, 0)))	return S_FALSE;

	// This code allows to get depth up to 8m
	BOOL bNearMode = false;
	if(m_kinect4Windows)
	{
		bNearMode = true;
	}

	INuiFrameTexture * pTexture = NULL;
	NUI_IMAGE_FRAME imageFrame;

	hr = m_pNuiSensor->NuiImageStreamGetNextFrame(m_pDepthStreamHandle, 0, &imageFrame);
	hr = m_pNuiSensor->NuiImageFrameGetDepthImagePixelFrameTexture(m_pDepthStreamHandle, &imageFrame, &bNearMode, &pTexture);

	NUI_LOCKED_RECT LockedRect;
	hr = pTexture->LockRect(0, &LockedRect, NULL, 0);
	if ( FAILED(hr) ) { return hr; }

	NUI_DEPTH_IMAGE_PIXEL * pBuffer =  (NUI_DEPTH_IMAGE_PIXEL *) LockedRect.pBits;

	USHORT* test = new USHORT[getDepthWidth()*getDepthHeight()];

//#pragma omp parallel for
	//for (int j = 0; j < (int)getDepthWidth()*(int)getDepthHeight(); j++)	{
	//	m_depthD16[j] = pBuffer[j].depth;
	//}
	for (unsigned int j = 0; j < getDepthHeight(); j++) {
		for (unsigned int i = 0; i < getDepthWidth(); i++) {
			unsigned int srcIdx = j*getDepthWidth() + (getDepthWidth() - 1 - i);
			unsigned int desIdx = j*getDepthWidth() + i;

			const USHORT& d = pBuffer[srcIdx].depth;
			m_depthD16[desIdx] = d;
			test[srcIdx] = d * 8;
		}
	}
	 
	m_bDepthReceived = true;

	hr = pTexture->UnlockRect(0);
	if ( FAILED(hr) ) { return hr; };

	hr = m_pNuiSensor->NuiImageStreamReleaseFrame(m_pDepthStreamHandle, &imageFrame);

	// Get offset x, y coordinates for color in depth space
	// This will allow us to later compensate for the differences in location, angle, etc between the depth and color cameras
	m_pNuiSensor->NuiImageGetColorPixelCoordinateFrameFromDepthPixelFrameAtResolution(
		cColorResolution,
		cDepthResolution,
		getDepthWidth()*getDepthHeight(),
		test,
		getDepthWidth()*getDepthHeight()*2,
		m_colorCoordinates
		);

	SAFE_DELETE_ARRAY(test);

	return hr;
}

KinectSensor::processColor()
{
	if (! (WAIT_OBJECT_0 == WaitForSingleObject(m_hNextColorFrameEvent, 0)) )	return S_FALSE;

	NUI_IMAGE_FRAME imageFrame;

	hr = S_OK;
	hr = m_pNuiSensor->NuiImageStreamGetNextFrame(m_pColorStreamHandle, 0, &imageFrame);
	if ( FAILED(hr) ) { return hr; }

	NUI_LOCKED_RECT LockedRect;
	hr = imageFrame.pFrameTexture->LockRect(0, &LockedRect, NULL, 0);
	if ( FAILED(hr) ) { return hr; }

	// loop over each row and column of the color
#pragma omp parallel for
	for (int yi = 0; yi < (int)getColorHeight(); ++yi) {
		LONG y = yi;

		LONG* pDest = ((LONG*)m_colorRGBX) + (int)getColorWidth() * y;
		for (LONG x = 0; x < (int)getColorWidth(); ++x) {
			// calculate index into depth array
			//int depthIndex = x/m_colorToDepthDivisor + y/m_colorToDepthDivisor * getDepthWidth();
			int depthIndex = (getDepthWidth() - 1 - x/m_colorToDepthDivisor) + y/m_colorToDepthDivisor * getDepthWidth();

			// retrieve the depth to color mapping for the current depth pixel
			LONG colorInDepthX = m_colorCoordinates[depthIndex * 2];
			LONG colorInDepthY = m_colorCoordinates[depthIndex * 2 + 1];

			// make sure the depth pixel maps to a valid point in color space
			if ( colorInDepthX >= 0 && colorInDepthX < (int)getColorWidth() && colorInDepthY >= 0 && colorInDepthY < (int)getColorHeight() ) {
				// calculate index into color array
				LONG colorIndex = colorInDepthX + colorInDepthY * (int)getColorWidth();

				// set source for copy to the color pixel
				LONG* pSrc = ((LONG *)LockedRect.pBits) + colorIndex;					
				LONG tmp = *pSrc;

				tmp|=0xFF000000; // Flag for is valid

				*pDest = tmp;
			} else {
				*pDest = 0x00000000;
			}
			pDest++;
		}
	}

	m_bColorReceived = true;

	hr = imageFrame.pFrameTexture->UnlockRect(0);
	if ( FAILED(hr) ) { return hr; };

	hr = m_pNuiSensor->NuiImageStreamReleaseFrame(m_pColorStreamHandle, &imageFrame);


	return hr;
}

KinectSensor::toggleNearMode()
{
	hr = E_FAIL;

	if ( m_pNuiSensor )
	{
		hr = m_pNuiSensor->NuiImageStreamSetImageFrameFlags(m_pDepthStreamHandle, m_bNearMode ? 0 : NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE);

		if ( SUCCEEDED(hr) )
		{
			m_bNearMode = !m_bNearMode;
		}
	}

	return hr;
}

KinectSensor::toggleAutoWhiteBalance()
{
	INuiColorCameraSettings* colorCameraSettings;
	hr = S_OK;
	hr = m_pNuiSensor->NuiGetColorCameraSettings(&colorCameraSettings);
	if (hr != E_NUI_HARDWARE_FEATURE_UNAVAILABLE) {	//feature only supported with windows Kinect

		BOOL ex;
		colorCameraSettings->GetAutoExposure(&ex);
		colorCameraSettings->SetAutoExposure(!ex);

		double exposure;
		colorCameraSettings->GetExposureTime(&exposure);

		double minExp; colorCameraSettings->GetMinExposureTime(&minExp);
		double maxExp; colorCameraSettings->GetMaxExposureTime(&maxExp);
		std::cout << exposure << std::endl;
		std::cout << minExp << std::endl;
		std::cout << maxExp << std::endl;
		colorCameraSettings->SetExposureTime(4000);

		double fr;
		colorCameraSettings->GetFrameInterval(&fr);
		std::cout << fr << std::endl;

		double gain;
		hr = colorCameraSettings->GetGain(&gain);
		std::cout << gain << std::endl;
		double minG; colorCameraSettings->GetMinGain(&minG);
		double maxG; colorCameraSettings->GetMaxGain(&maxG);
		std::cout << minG << std::endl;
		std::cout << maxG << std::endl;

		hr = colorCameraSettings->SetGain(4);

		BOOL ab;
		colorCameraSettings->GetAutoWhiteBalance(&ab);
		colorCameraSettings->SetAutoWhiteBalance(!ab);

		colorCameraSettings->SetWhiteBalance(4000);	//this is a wild guess; but it seems that the previously 'auto-set' value cannot be obtained
		LONG min; colorCameraSettings->GetMinWhiteBalance(&min);
		LONG max; colorCameraSettings->GetMaxWhiteBalance(&max);
		std::cout << min << std::endl;
		std::cout << max << std::endl;
	}

	return hr;
}
