#include "RealsenseGrabber.h"
#include "Timer.h"
#include "Configuration.h"
#include "librealsense2/hpp/rs_sensor.hpp"
#include "librealsense2/hpp/rs_processing.hpp"

RealsenseGrabber::RealsenseGrabber()
{
	depthFilter = new DepthFilter();
	colorFilter = new ColorFilter();
	alignColorMap = new AlignColorMap();
	depth2color = new Extrinsics[MAX_CAMERAS];
	color2depth = new Extrinsics[MAX_CAMERAS];
	depthIntrinsics = new Intrinsics[MAX_CAMERAS];
	colorIntrinsics = new Intrinsics[MAX_CAMERAS];
	originColorIntrinsics = new Intrinsics[MAX_CAMERAS];
	depthImages = new float[MAX_CAMERAS * DEPTH_H * DEPTH_W];
	colorImages = new RGBQUAD[MAX_CAMERAS * COLOR_H * COLOR_W];
	originColorImages = new RGBQUAD[MAX_CAMERAS * COLOR_H * COLOR_W];

	rs2::context context;
	rs2::device_list deviceList = context.query_devices();
	for (int i = 0; i < deviceList.size(); i++) {
		enableDevice(deviceList[i]);
		std::cout << "Device " << i << " open." << std::endl;
	}

#if HD == false
	Configuration::loadBackground(alignColorMap);
#endif
}

RealsenseGrabber::~RealsenseGrabber()
{
	if (depthFilter != NULL) {
		delete depthFilter;
	}
	if (colorFilter != NULL) {
		delete colorFilter;
	}
	if (alignColorMap != NULL) {
		delete alignColorMap;
	}
	if (depth2color != NULL) {
		delete[] depth2color;
	}
	if (color2depth != NULL) {
		delete[] color2depth;
	}
	if (depthIntrinsics != NULL) {
		delete[] depthIntrinsics;
	}
	if (colorIntrinsics != NULL) {
		delete[] colorIntrinsics;
	}
	if (originColorIntrinsics != NULL) {
		delete[] originColorIntrinsics;
	}
	if (depthImages != NULL) {
		delete[] depthImages;
	}
	if (colorImages != NULL) {
		delete[] colorImages;
	}
	if (originColorImages != NULL) {
		delete[] originColorImages;
	}

	for (int i = 0; i < devices.size(); i++) {
		devices[i].stop();
	}
}

void RealsenseGrabber::enableDevice(rs2::device device)
{
	std::string serialNumber(device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER));

	if (strcmp(device.get_info(RS2_CAMERA_INFO_NAME), "Platform Camera") == 0) {
		return;
	}

	rs2::config cfg;
	cfg.enable_device(serialNumber);
	cfg.enable_stream(RS2_STREAM_DEPTH, DEPTH_W, DEPTH_H, RS2_FORMAT_Z16, CAMERA_FPS);
	cfg.enable_stream(RS2_STREAM_COLOR, COLOR_W, COLOR_H, RS2_FORMAT_YUYV, CAMERA_FPS);
	cfg.disable_stream(RS2_STREAM_INFRARED, 1);
	cfg.disable_stream(RS2_STREAM_INFRARED, 2);
	
	std::vector<rs2::sensor> sensors = device.query_sensors();
	float convertFactor = 0;
	for (int i = 0; i < sensors.size(); i++) {
		if (strcmp(sensors[i].get_info(RS2_CAMERA_INFO_NAME), "Stereo Module") == 0) {
			sensors[i].set_option(RS2_OPTION_ENABLE_AUTO_EXPOSURE, 0);
			float depth_unit = sensors[i].get_option(RS2_OPTION_DEPTH_UNITS);
			float stereo_baseline = sensors[i].get_option(RS2_OPTION_STEREO_BASELINE) * 0.001;
			convertFactor = stereo_baseline * (1 << 5) / depth_unit;
		}
		if (strcmp(sensors[i].get_info(RS2_CAMERA_INFO_NAME), "RGB Camera") == 0) {
			sensors[i].set_option(RS2_OPTION_ENABLE_AUTO_EXPOSURE, 0);
			sensors[i].set_option(RS2_OPTION_ENABLE_AUTO_WHITE_BALANCE, 0);
			sensors[i].set_option(RS2_OPTION_GAIN, 128);
			sensors[i].set_option(RS2_OPTION_SHARPNESS, 50);
			sensors[i].set_option(RS2_OPTION_EXPOSURE, 312);
		}
	}

	rs2::pipeline pipeline;
	pipeline.start(cfg);

	std::vector<rs2::stream_profile> streamProfiles = pipeline.get_active_profile().get_streams();
	rs2::stream_profile depthProfile;
	rs2::stream_profile colorProfile;
	for (int i = 0; i < streamProfiles.size(); i++) {
		rs2::stream_profile profile = streamProfiles[i];

		if (profile.is<rs2::video_stream_profile>()) {
			if (profile.stream_type() == RS2_STREAM_DEPTH) {
				depthProfile = profile;
			}
			if (profile.stream_type() == RS2_STREAM_COLOR) {
				colorProfile = profile;
			}
		}
	}
	int id = devices.size();
	rs2_intrinsics dIntrinsics = depthProfile.as<rs2::video_stream_profile>().get_intrinsics();
	depthIntrinsics[id].fx = dIntrinsics.fx;
	depthIntrinsics[id].fy = dIntrinsics.fy;
	depthIntrinsics[id].ppx = dIntrinsics.ppx;
	depthIntrinsics[id].ppy = dIntrinsics.ppy;
	depthFilter->setConvertFactor(id, dIntrinsics.fx * convertFactor); // convertFactor is for the transformation between Depth and Disparity (their product)
	rs2_intrinsics cIntrinsics = colorProfile.as<rs2::video_stream_profile>().get_intrinsics();
	originColorIntrinsics[id].fx = cIntrinsics.fx;
	originColorIntrinsics[id].fy = cIntrinsics.fy;
	originColorIntrinsics[id].ppx = cIntrinsics.ppx;
	originColorIntrinsics[id].ppy = cIntrinsics.ppy;
	colorIntrinsics[id] = depthIntrinsics[id].zoom((float)COLOR_W / DEPTH_W, (float)COLOR_H / DEPTH_H);
	rs2_extrinsics d2cExtrinsics = depthProfile.get_extrinsics_to(colorProfile);
	depth2color[id] = Extrinsics(d2cExtrinsics.rotation, d2cExtrinsics.translation);
	rs2_extrinsics c2dExtrinsics = colorProfile.get_extrinsics_to(depthProfile);
	color2depth[id] = Extrinsics(c2dExtrinsics.rotation, c2dExtrinsics.translation);

	devices.push_back(pipeline);
}

void RealsenseGrabber::updateRGBD()
{
	for (int deviceId = 0; deviceId < devices.size(); deviceId++) {
		rs2::pipeline pipeline = devices[deviceId];
		rs2::frameset frameset = pipeline.wait_for_frames();

		bool check = (frameset.size() == 2);

		if (check) {
			for (int i = 0; i < frameset.size(); i++) {
				rs2::frame frame = frameset[i];
				rs2::stream_profile profile = frame.get_profile();

				if (profile.stream_type() == RS2_STREAM_DEPTH) {
					depthFilter->process(deviceId, (UINT16*)frame.get_data());
				}
				if (profile.stream_type() == RS2_STREAM_COLOR) {
					colorFilter->process(deviceId, (UINT8*)frame.get_data());
				}
			}
		} else {
			std::cout << deviceId << " Failed" << std::endl;
		}
	}

	alignColorMap->alignColor2Depth(devices.size(), depthFilter->getCurrFrame_device(), colorFilter->getCurrFrame_device(), depthIntrinsics, originColorIntrinsics, depth2color);
}

void RealsenseGrabber::saveBackground() {
	if (alignColorMap->isBackgroundOn()) {
		alignColorMap->disableBackground();
	} else {
		alignColorMap->enableBackground(depthFilter->getCurrFrame_device());
	}
	Configuration::saveBackground(alignColorMap);
}

float* RealsenseGrabber::getDepthImages_host()
{
	HANDLE_ERROR(cudaMemcpy(depthImages, getDepthImages_device(), devices.size() * DEPTH_H * DEPTH_W * sizeof(float), cudaMemcpyDeviceToHost));
	return depthImages;
}

RGBQUAD* RealsenseGrabber::getColorImages_host()
{
	HANDLE_ERROR(cudaMemcpy(colorImages, getColorImages_device(), devices.size() * COLOR_H * COLOR_W * sizeof(RGBQUAD), cudaMemcpyDeviceToHost));
	return colorImages;
}

RGBQUAD* RealsenseGrabber::getOriginColorImages_host()
{
	HANDLE_ERROR(cudaMemcpy(originColorImages, getOriginColorImages_device(), devices.size() * COLOR_H * COLOR_W * sizeof(RGBQUAD), cudaMemcpyDeviceToHost));
	return originColorImages;
}
