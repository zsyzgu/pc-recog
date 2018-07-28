#include "Timer.h"
#include "SceneRegistration.h"
#include "TsdfVolume.h"
#include "Transmission.h"
#include "RealsenseGrabber.h"
#include "Parameters.h"
#include "Configuration.h"
#include <pcl/visualization/cloud_viewer.h>
#include <windows.h>

#define CREATE_EXE
//#define TRANSMISSION
#define IS_SERVER true

byte* buffer = NULL;
RealsenseGrabber* grabber = NULL;
TsdfVolume* volume = NULL;
pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer;
Transformation* world2color = NULL;
Transformation* world2depth = NULL;
Transmission* transmission = NULL;

void registration() {
	SceneRegistration::align(grabber, world2color);
}

void saveExtrinsics() {
	Configuration::saveExtrinsics(world2color);
}

void saveBackground() {
	grabber->saveBackground();
}

void keyboardEventOccurred(const pcl::visualization::KeyboardEvent& event) {
	if (event.getKeySym() == "r" && event.keyDown()) {
		registration();
	}
	if (event.getKeySym() == "s" && event.keyDown()) {
		saveExtrinsics();
	}
	if (event.getKeySym() == "b" && event.keyDown()) {
		saveBackground();
	}
}

void startViewer() {
	viewer = boost::shared_ptr<pcl::visualization::PCLVisualizer>(new pcl::visualization::PCLVisualizer("Point Cloud Viewer"));
	viewer->setCameraPosition(0.0, 0.0, -2.0, 0.0, 0.0, 0.0);
	viewer->registerKeyboardCallback(keyboardEventOccurred);
}

void start() {
	cudaSetDevice(0);
	omp_set_num_threads(2);
	
	grabber = new RealsenseGrabber();
	cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>());
	volume = new TsdfVolume(2, 2, 2, 0, 0, 1);
	buffer = new byte[MAX_VERTEX * sizeof(Vertex)];
	world2color = new Transformation[MAX_CAMERAS];
	world2depth = new Transformation[MAX_CAMERAS];
	Configuration::loadExtrinsics(world2color);
	grabber->loadBackground();

#ifdef TRANSMISSION
	transmission = new Transmission(IS_SERVER, 5);
#endif
}

float* depthImages_device = NULL;
RGBQUAD* colorImages_device;
Intrinsics* depthIntrinsics;
Intrinsics* colorIntrinsics;
bool* check;
int cameras;

void update() {
	if (depthImages_device == NULL) {
		cameras = grabber->getRGBD(check, depthImages_device, colorImages_device, world2depth, world2color, depthIntrinsics, colorIntrinsics);

		if (transmission != NULL && transmission->isConnected) {
			transmission->prepareSendFrame(cameras, check, depthImages_device, colorImages_device, world2depth, depthIntrinsics, colorIntrinsics);
		}
	}

	#pragma omp parallel sections
	{
		#pragma omp section
		{
			if (transmission != NULL && transmission->isConnected) {
				transmission->sendFrame();
				int remoteCameras = transmission->getFrame(depthImages_device + cameras * DEPTH_H * DEPTH_W, colorImages_device + cameras * COLOR_H * COLOR_W, world2depth + cameras, depthIntrinsics + cameras, colorIntrinsics + cameras);
				cameras += remoteCameras;
			}

			volume->integrate(buffer, cameras, depthImages_device, colorImages_device, world2depth, depthIntrinsics, colorIntrinsics);

			cameras = grabber->getRGBD(check, depthImages_device, colorImages_device, world2depth, world2color, depthIntrinsics, colorIntrinsics);

			if (transmission != NULL && transmission->isConnected) {
				transmission->prepareSendFrame(cameras, check, depthImages_device, colorImages_device, world2depth, depthIntrinsics, colorIntrinsics);
			}
		}
		#pragma omp section
		{
			if (transmission != NULL && transmission->isConnected) {
				transmission->recvFrame();
			}
		}
	}
}

void stop() {
	if (grabber != NULL) {
		delete grabber;
	}
	if (volume != NULL) {
		delete volume;
	}
	if (buffer != NULL) {
		delete[] buffer;
	}
	if (world2color != NULL) {
		delete[] world2color;
	}
	if (world2depth != NULL) {
		delete[] world2depth;
	}
	if (transmission != NULL) {
		delete transmission;
	}
	std::cout << "stopped" << std::endl;
}

#ifdef CREATE_EXE

int main(int argc, char *argv[]) {
	start();
	startViewer();

	Timer timer;
	while (!viewer->wasStopped()) {
		viewer->spinOnce();

		timer.reset();
		update();
		timer.outputTime();

		cloud = volume->getPointCloudFromMesh(buffer);
		if (!viewer->updatePointCloud(cloud, "cloud")) {
			viewer->addPointCloud(cloud, "cloud");
		}
	}
	stop();

	return 0;
}

#else
extern "C" {
	__declspec(dllexport) void callStart() {
		start();
	}

	__declspec(dllexport) byte* callUpdate() {
		update();
		return buffer;
	}

	__declspec(dllexport) void callRegistration() {
		registration();
	}

	__declspec(dllexport) void callSaveExtrinsics() {
		saveExtrinsics();
	}

	__declspec(dllexport) void callSaveBackground() {
		saveBackground();
	}

	__declspec(dllexport) void callStop() {
		stop();
	}
}
#endif
