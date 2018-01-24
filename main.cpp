#include "Timer.h"
#include "PointCloudProcess.h"
#include "Kinect2Grabber.h"
#include "SceneRegistration.h"
#include <pcl/gpu/features/features.hpp>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/io/pcd_io.h>
#include <pcl/compression/octree_pointcloud_compression.h>
#include <pcl/gpu/utils/safe_call.hpp>

//#define CREATE_EXE

boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer;
pcl::Kinect2Grabber* grabber;
Eigen::Matrix4f transformation;
pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr sceneLocal;
pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr sceneRemote;
pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr sceneMerged;

void keyboardEventOccurred(const pcl::visualization::KeyboardEvent& event) {
	if (event.getKeySym() == "r" && event.keyDown()) {
		transformation = SceneRegistration::align(sceneRemote, sceneLocal);
	}
}

void startViewer() {
	viewer = boost::shared_ptr<pcl::visualization::PCLVisualizer>(new pcl::visualization::PCLVisualizer("Point Cloud Viewer"));
	viewer->setCameraPosition(0.0, 0.0, -2.0, 0.0, 0.0, 0.0);
	viewer->registerKeyboardCallback(keyboardEventOccurred);
}

void start() {
	cudaSetDevice(1);
	omp_set_num_threads(4);

	grabber = new pcl::Kinect2Grabber();
	sceneLocal = pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr(new pcl::PointCloud<pcl::PointXYZRGBNormal>());
	sceneRemote = pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr(new pcl::PointCloud<pcl::PointXYZRGBNormal>());
	sceneMerged = pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr(new pcl::PointCloud<pcl::PointXYZRGBNormal>());

	transformation.setIdentity();
	pcl::io::loadPCDFile("view_remote.pcd", *sceneRemote);
	pcl::io::loadPCDFile("view_local.pcd", *sceneLocal);
}

void update() {
	pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr transformedRemote(new pcl::PointCloud<pcl::PointXYZRGBNormal>());

	#pragma omp parallel sections
	{
		#pragma	omp section
		{
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr kinectCloud(new pcl::PointCloud<pcl::PointXYZRGB>());
			kinectCloud = grabber->getPointCloud();
			PointCloudProcess::pointCloud2PCNormal(sceneLocal, kinectCloud);
		}
		#pragma	omp section
		{
			pcl::transformPointCloud(*sceneRemote, *transformedRemote, transformation);
		}
	}

	PointCloudProcess::merge2PointClouds(sceneMerged, sceneLocal, transformedRemote);
}

#ifdef CREATE_EXE
int main(int argc, char *argv[]) {
	start();
	startViewer();

	while (!viewer->wasStopped()) {
		viewer->spinOnce();

		update();

		pcl::PointCloud<pcl::PointXYZRGB>::Ptr viewCloud(new pcl::PointCloud<pcl::PointXYZRGB>());
		pcl::copyPointCloud(*sceneMerged, *viewCloud);
		if (!viewer->updatePointCloud(viewCloud, "result")) {
			viewer->addPointCloud(viewCloud, "result");
		}
	}

	return 0;
}

#else
const int BUFFER_SIZE = 12000000;

byte buffer[BUFFER_SIZE];

void loadBuffer(byte* dst, void* src, int size) {
	byte* pt = (byte*)src;
	for (int i = 0; i < size; i++) {
		dst[i] = pt[i];
	}
}

extern "C" {
	__declspec(dllexport) void callStart() {
		start();
	}

	__declspec(dllexport) byte* callUpdate() {
		update();

		int size = sceneMerged->size();
		loadBuffer(buffer, &size, 4);
#pragma omp parallel for
		for (int i = 0; i < size; i++) {
			int id = i * 27 + 4;
			loadBuffer(buffer + id + 0, &(sceneMerged->points[i].x), 4);
			loadBuffer(buffer + id + 4, &(sceneMerged->points[i].y), 4);
			loadBuffer(buffer + id + 8, &(sceneMerged->points[i].z), 4);
			loadBuffer(buffer + id + 12, &(sceneMerged->points[i].r), 1);
			loadBuffer(buffer + id + 13, &(sceneMerged->points[i].g), 1);
			loadBuffer(buffer + id + 14, &(sceneMerged->points[i].b), 1);
			loadBuffer(buffer + id + 15, &(sceneMerged->points[i].normal_x), 4);
			loadBuffer(buffer + id + 19, &(sceneMerged->points[i].normal_y), 4);
			loadBuffer(buffer + id + 23, &(sceneMerged->points[i].normal_z), 4);
		}

		return buffer;
	}

	__declspec(dllexport) void callRegistration() {
		transformation = SceneRegistration::align(sceneRemote, sceneLocal);
	}

	__declspec(dllexport) void callStop() {

	}
}
#endif
