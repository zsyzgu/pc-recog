#ifndef POINT_CLOUD_FILTER_H
#define POINT_CLOUD_FILTER_H

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/PolygonMesh.h>

class PointCloudProcess {
public:
	static void mlsFiltering(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud);
	static void merge2PointClouds(pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr cloud, pcl::PointCloud<pcl::PointXYZRGBNormal>::ConstPtr cloud1, pcl::PointCloud<pcl::PointXYZRGBNormal>::ConstPtr cloud2);
	static void pointCloud2Mesh(pcl::PolygonMesh::Ptr mesh, pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud);
	static void pointCloud2PCNormal(pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr pcNormal, pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud);
	static float squaredDistance(pcl::PointXYZ& pt1, pcl::PointXYZ& pt2);
};

#endif