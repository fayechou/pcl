/*
 * Software License Agreement (BSD License)
 *
 *  Point Cloud Library (PCL) - www.pointclouds.org
 *  Copyright (c) 2010-2012, Willow Garage, Inc.
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$
 *
 */

#include <gtest/gtest.h>
#include <pcl/point_cloud.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/io/pcd_io.h>
#include <pcl/features/shot.h>
#include <pcl/features/shot_omp.h>
#include <pcl/features/3dsc.h>
#include <pcl/features/usc.h>

using namespace pcl;
using namespace pcl::io;
using namespace std;

typedef search::KdTree<PointXYZ>::Ptr KdTreePtr;

PointCloud<PointXYZ> cloud;
vector<int> indices;
KdTreePtr tree;

///////////////////////////////////////////////////////////////////////////////////
void
shotCopyPointCloud (const PointCloud<SHOT> &cloud_in, const std::vector<int> &indices,
                    PointCloud<SHOT> &cloud_out)
{
  // Allocate enough space and copy the basics
  cloud_out.points.resize (indices.size ());
  cloud_out.header   = cloud_in.header;
  cloud_out.width    = indices.size ();
  cloud_out.height   = 1;
  if (cloud_in.is_dense)
    cloud_out.is_dense = true;
  else
    // It's not necessarily true that is_dense is false if cloud_in.is_dense is false
    // To verify this, we would need to iterate over all points and check for NaNs
    cloud_out.is_dense = false;

  // Iterate over each point
  for (size_t i = 0; i < indices.size (); ++i)
  {
    std::copy (cloud_in.points[indices[i]].descriptor.begin (),
               cloud_in.points[indices[i]].descriptor.end (),
               std::back_inserter(cloud_out.points[i].descriptor));
    memcpy (cloud_out.points[i].rf, cloud_in.points[indices[i]].rf, sizeof (float) * 9);
  }
}

///////////////////////////////////////////////////////////////////////////////////
template <typename FeatureEstimation, typename PointT, typename NormalT, typename OutputT> FeatureEstimation
createSHOTDesc (const typename PointCloud<NormalT>::Ptr & normals,
                const int nr_shape_bins = 10,
                const int nr_color_bins = 30,
                const bool describe_shape = true,
                const bool describe_color = false)
{
  FeatureEstimation f (nr_shape_bins);
  f.setInputNormals (normals);
  return (f);
}

///////////////////////////////////////////////////////////////////////////////////
template <typename FeatureEstimation, typename NormalT, typename OutputT> FeatureEstimation
createSHOTDesc (const typename PointCloud<NormalT>::Ptr & normals,
                const int nr_shape_bins = 10,
                const int nr_color_bins = 30,
                const bool describe_shape = true,
                const bool describe_color = false)
{
  FeatureEstimation f (describe_shape, describe_color, nr_shape_bins,nr_color_bins);
  f.setInputNormals (normals);
  return (f);
}

///////////////////////////////////////////////////////////////////////////////////
template <> ShapeContext3DEstimation<PointXYZ, Normal, SHOT>
createSHOTDesc<ShapeContext3DEstimation<PointXYZ, Normal, SHOT>, PointXYZ, Normal, SHOT> (
    const PointCloud<Normal>::Ptr & normals,
    const int nr_shape_bins,
    const int nr_color_bins,
    const bool describe_shape,
    const bool describe_color)
{
  ShapeContext3DEstimation<PointXYZ, Normal, SHOT> sc3d;
  sc3d.setAzimuthBins (4);
  sc3d.setElevationBins (4);
  sc3d.setRadiusBins (4);
  sc3d.setMinimalRadius (0.004);
  sc3d.setPointDensityRadius (0.008);
  sc3d.setInputNormals (normals);
  return (sc3d);
}

///////////////////////////////////////////////////////////////////////////////////
template <> UniqueShapeContext<PointXYZ, SHOT>
createSHOTDesc<UniqueShapeContext<PointXYZ, SHOT>, PointXYZ, Normal, SHOT> (
    const PointCloud<Normal>::Ptr & normals,
    const int nr_shape_bins,
    const int nr_color_bins,
    const bool describe_shape,
    const bool describe_color)
{
  UniqueShapeContext<PointXYZ, SHOT> usc;
  usc.setAzimuthBins (4);
  usc.setElevationBins (4);
  usc.setRadiusBins (4);
  usc.setMinimalRadius (0.004);
  usc.setPointDensityRadius (0.008);
  usc.setLocalRadius (0.04);
  return (usc);
}

///////////////////////////////////////////////////////////////////////////////////
template <typename FeatureEstimation, typename PointT, typename NormalT, typename OutputT> void
testSHOTIndicesAndSearchSurface (const typename PointCloud<PointT>::Ptr & points,
                                 const typename PointCloud<NormalT>::Ptr & normals,
                                 const boost::shared_ptr<vector<int> > & indices,
                                 const int nr_shape_bins = 10,
                                 const int nr_color_bins = 30,
                                 const bool describe_shape = true,
                                 const bool describe_color = false)
{
  double radius = 0.04;
  //
  // Test setIndices and setSearchSurface
  //
  PointCloud<OutputT> full_output, output0, output1, output2;

  // Compute for all points and then subsample the results
  FeatureEstimation est0 = createSHOTDesc<FeatureEstimation, PointT, NormalT, OutputT>(normals, nr_shape_bins,nr_color_bins,describe_shape,describe_color);
  est0.setSearchMethod (typename search::KdTree<PointT>::Ptr (new search::KdTree<PointT>));
  est0.setRadiusSearch (radius);
  est0.setInputCloud (points);
  est0.compute (full_output);

  shotCopyPointCloud (full_output, *indices, output0);

  // Compute with all points as "search surface" and the specified sub-cloud as "input"
  typename PointCloud<PointT>::Ptr subpoints (new PointCloud<PointT>);
  copyPointCloud (*points, *indices, *subpoints);
  FeatureEstimation est1 = createSHOTDesc<FeatureEstimation, PointT, NormalT, OutputT>(normals, nr_shape_bins,nr_color_bins,describe_shape,describe_color);
  est1.setSearchMethod (typename search::KdTree<PointT>::Ptr (new search::KdTree<PointT>));
  est1.setRadiusSearch (radius);
  est1.setInputCloud (subpoints);
  est1.setSearchSurface (points);
  est1.compute (output1);

  //// Compute with all points as "input" and the specified indices
  FeatureEstimation est2 = createSHOTDesc<FeatureEstimation, PointT, NormalT, OutputT>(normals, nr_shape_bins,nr_color_bins,describe_shape,describe_color);
  est2.setSearchMethod (typename search::KdTree<PointT>::Ptr (new search::KdTree<PointT>));
  est2.setRadiusSearch (radius);
  est2.setInputCloud (points);
  est2.setIndices (indices);
  est2.compute (output2);

  // All three of the above cases should produce equivalent results
  ASSERT_EQ (output0.size (), output1.size ());
  ASSERT_EQ (output1.size (), output2.size ());
  for (size_t i = 0; i < output1.size (); ++i)
  {
    for (size_t j = 0; j < output0.points[i].descriptor.size(); ++j)
    {
      ASSERT_EQ (output0.points[i].descriptor[j], output1.points[i].descriptor[j]);
      ASSERT_EQ (output1.points[i].descriptor[j], output2.points[i].descriptor[j]);
    }
  }

  //
  // Test the combination of setIndices and setSearchSurface
  //
  PointCloud<OutputT> output3, output4;

  boost::shared_ptr<vector<int> > indices2 (new vector<int> (0));
  for (size_t i = 0; i < (indices->size ()/2); ++i)
    indices2->push_back (i);

  // Compute with all points as search surface + the specified sub-cloud as "input" but for only a subset of indices
  FeatureEstimation est3 = createSHOTDesc<FeatureEstimation, PointT, NormalT, OutputT>(normals, nr_shape_bins,nr_color_bins,describe_shape,describe_color);
  est3.setSearchMethod (typename search::KdTree<PointT>::Ptr (new search::KdTree<PointT>));
  est3.setRadiusSearch (0.04);
  est3.setSearchSurface (points);
  est3.setInputCloud (subpoints);
  est3.setIndices (indices2);
  est3.compute (output3);

  // Start with features for each point in "subpoints" and then subsample the results
  shotCopyPointCloud (output0, *indices2, output4); // (Re-using "output0" from above)

  // The two cases above should produce equivalent results
  ASSERT_EQ (output3.size (), output4.size ());
  for (size_t i = 0; i < output3.size (); ++i)
    for (size_t j = 0; j < output3.points[i].descriptor.size (); ++j)
      ASSERT_EQ (output3.points[i].descriptor[j], output4.points[i].descriptor[j]);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TEST (PCL, SHOTShapeEstimation)
{
  // Estimate normals first
  double mr = 0.002;
  NormalEstimation<PointXYZ, Normal> n;
  PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
  // set parameters
  n.setInputCloud (cloud.makeShared ());
  boost::shared_ptr<vector<int> > indicesptr (new vector<int> (indices));
  n.setIndices (indicesptr);
  n.setSearchMethod (tree);
  n.setRadiusSearch (20 * mr);
  n.compute (*normals);

  EXPECT_NEAR (normals->points[103].normal_x, 0.36683175, 1e-4);
  EXPECT_NEAR (normals->points[103].normal_y, -0.44696972, 1e-4);
  EXPECT_NEAR (normals->points[103].normal_z, -0.81587529, 1e-4);
  EXPECT_NEAR (normals->points[200].normal_x, -0.71414840, 1e-4);
  EXPECT_NEAR (normals->points[200].normal_y, -0.06002361, 1e-4);
  EXPECT_NEAR (normals->points[200].normal_z, -0.69741613, 1e-4);

  EXPECT_NEAR (normals->points[140].normal_x, -0.45109111, 1e-4);
  EXPECT_NEAR (normals->points[140].normal_y, -0.19499126, 1e-4);
  EXPECT_NEAR (normals->points[140].normal_z, -0.87091631, 1e-4);

  SHOTEstimation<PointXYZ, Normal, SHOT> shot;
  shot.setInputNormals (normals);
  EXPECT_EQ (shot.getInputNormals (), normals);
  shot.setRadiusSearch (20 * mr);

  // Object
  PointCloud<SHOT>::Ptr shots (new PointCloud<SHOT> ());

  // set parameters
  shot.setInputCloud (cloud.makeShared ());
  shot.setIndices (indicesptr);
  shot.setSearchMethod (tree);

  // estimate
  shot.compute (*shots);
  EXPECT_EQ (shots->points.size (), indices.size ());

  EXPECT_NEAR (shots->points[103].descriptor[9 ], 0.0072018504, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[10], 0.0023103887, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[11], 0.0024724449, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[19], 0.0031367359, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[20], 0.17439659, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[21], 0.070665278, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[42], 0.013304681, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[53], 0.0073520984, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[54], 0.013584172, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[55], 0.0050609680, 1e-4);


  // Test results when setIndices and/or setSearchSurface are used

  boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
  for (size_t i = 0; i < cloud.size (); i+=3)
    test_indices->push_back (i);

  testSHOTIndicesAndSearchSurface<SHOTEstimation<PointXYZ, Normal, SHOT>, PointXYZ, Normal, SHOT> (cloud.makeShared (), normals, test_indices);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TEST (PCL, GenericSHOTShapeEstimation)
{
  // SHOT length
  const int shapeStep_ = 20;
  //const int dim = 32*(shapeStep_+1);

  // Estimate normals first
  double mr = 0.002;
  NormalEstimation<PointXYZ, Normal> n;
  PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
  // set parameters
  n.setInputCloud (cloud.makeShared ());
  boost::shared_ptr<vector<int> > indicesptr (new vector<int> (indices));
  n.setIndices (indicesptr);
  n.setSearchMethod (tree);
  n.setRadiusSearch (20 * mr);
  n.compute (*normals);

  SHOTEstimation<PointXYZ, Normal, SHOT> shot (shapeStep_);
  shot.setInputNormals (normals);
  EXPECT_EQ (shot.getInputNormals (), normals);

  shot.setRadiusSearch (20 * mr);

  PointCloud< SHOT >::Ptr shots (new PointCloud< SHOT > ());

  // set parameters
  shot.setInputCloud (cloud.makeShared ());
  shot.setIndices (indicesptr);
  shot.setSearchMethod (tree);

  // estimate
  shot.compute (*shots);
  EXPECT_EQ (shots->points.size (), indices.size ());

  EXPECT_NEAR (shots->points[103].descriptor[18], 0.0077019366, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[19], 0.0024708188, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[21], 0.0079652183, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[38], 0.0067090928, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[39], 0.17498907, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[40], 0.078413926, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[81], 0.014228539, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[103], 0.022390056, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[105], 0.0058866320, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[123], 0.019105887, 1e-5);

  // Test results when setIndices and/or setSearchSurface are used
  boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
  for (size_t i = 0; i < cloud.size (); i+=3)
    test_indices->push_back (i);

  testSHOTIndicesAndSearchSurface<SHOTEstimation<PointXYZ, Normal, SHOT>, PointXYZ, Normal, SHOT> (cloud.makeShared (), normals, test_indices, shapeStep_);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TEST (PCL, SHOTShapeAndColorEstimation)
{
  double mr = 0.002;
  // Estimate normals first
  NormalEstimation<PointXYZ, Normal> n;
  PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
  // set parameters
  n.setInputCloud (cloud.makeShared ());
  boost::shared_ptr<vector<int> > indicesptr (new vector<int> (indices));
  n.setIndices (indicesptr);
  n.setSearchMethod (tree);
  n.setRadiusSearch (20 * mr);
  n.compute (*normals);

  search::KdTree<PointXYZRGBA>::Ptr rgbaTree;
  rgbaTree.reset (new search::KdTree<PointXYZRGBA> (false));

  // Object
  SHOTEstimation<PointXYZRGBA, Normal, SHOT> shot (true, true);
  shot.setInputNormals (normals);
  EXPECT_EQ (shot.getInputNormals (), normals);

  shot.setRadiusSearch ( 20 * mr);

  // Create fake point cloud with colors
  PointCloud<PointXYZRGBA> cloudWithColors;
  for (size_t i = 0; i < cloud.points.size (); ++i)
  {
    PointXYZRGBA p;
    p.x = cloud.points[i].x;
    p.y = cloud.points[i].y;
    p.z = cloud.points[i].z;

    p.rgba = ( (i%255) << 16 ) + ( ( (255 - i ) %255) << 8) + ( ( i*37 ) %255);
    cloudWithColors.push_back(p);
  }

  rgbaTree->setInputCloud (cloudWithColors.makeShared ());
  PointCloud<SHOT>::Ptr shots (new PointCloud<SHOT>);

  shot.setInputCloud (cloudWithColors.makeShared ());
  shot.setIndices (indicesptr);
  shot.setSearchMethod (rgbaTree);

  // estimate
  shot.compute (*shots);
  EXPECT_EQ (shots->points.size (), indices.size ());

  EXPECT_NEAR (shots->points[103].descriptor[10], 0.0020453099, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[11], 0.0021887729, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[21], 0.062557608, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[42], 0.011778189, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[53], 0.0065085669, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[54], 0.012025614, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[55], 0.0044803056, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[64], 0.064429596, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[65], 0.046486385, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[86], 0.011518310, 1e-5);

  EXPECT_NEAR (shots->points[103].descriptor[357], 0.0020453099, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[360], 0.0027993850, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[386], 0.045115642, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[387], 0.059068538, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[389], 0.0047547864, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[453], 0.0051176427, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[481], 0.0053625242, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[482], 0.012025614, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[511], 0.0057367259, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[512], 0.048357654, 1e-5);

  // Test results when setIndices and/or setSearchSurface are used
  boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
  for (size_t i = 0; i < cloud.size (); i+=3)
    test_indices->push_back (i);

  testSHOTIndicesAndSearchSurface<SHOTEstimation<PointXYZRGBA, Normal, SHOT>, PointXYZRGBA, Normal, SHOT> (cloudWithColors.makeShared (), normals, test_indices);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TEST (PCL, SHOTShapeEstimationOpenMP)
{
  // Estimate normals first
  double mr = 0.002;
  NormalEstimationOMP<PointXYZ, Normal> n (omp_get_max_threads ());
  PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
  // set parameters
  n.setInputCloud (cloud.makeShared ());
  boost::shared_ptr<vector<int> > indicesptr (new vector<int> (indices));
  n.setIndices (indicesptr);
  n.setSearchMethod (tree);
  n.setRadiusSearch (20 * mr);
  n.compute (*normals);

  SHOTEstimationOMP<PointXYZ, Normal, SHOT> shot;
  shot.setInputNormals (normals);
  EXPECT_EQ (shot.getInputNormals (), normals);

  shot.setRadiusSearch ( 20 * mr);

  // Object
  PointCloud<SHOT>::Ptr shots (new PointCloud<SHOT>);

  // set parameters
  shot.setInputCloud (cloud.makeShared ());
  shot.setIndices (indicesptr);
  shot.setSearchMethod (tree);

  // estimate
  shot.compute (*shots);
  EXPECT_EQ (shots->points.size (), indices.size ());

  EXPECT_NEAR (shots->points[103].descriptor[9 ], 0.0072018504, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[10], 0.0023103887, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[11], 0.0024724449, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[19], 0.0031367359, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[20], 0.17439659, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[21], 0.070665278, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[42], 0.013304681, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[53], 0.0073520984, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[54], 0.013584172, 1e-4);
  EXPECT_NEAR (shots->points[103].descriptor[55], 0.0050609680, 1e-4);

  // Test results when setIndices and/or setSearchSurface are used
  boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
  for (size_t i = 0; i < cloud.size (); i+=3)
    test_indices->push_back (i);

  testSHOTIndicesAndSearchSurface<SHOTEstimationOMP<PointXYZ, Normal, SHOT>, PointXYZ, Normal, SHOT> (cloud.makeShared (), normals, test_indices);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TEST (PCL,SHOTShapeAndColorEstimationOpenMP)
{
  double mr = 0.002;
  // Estimate normals first
  NormalEstimation<PointXYZ, Normal> n;
  PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
  // set parameters
  n.setInputCloud (cloud.makeShared ());
  boost::shared_ptr<vector<int> > indicesptr (new vector<int> (indices));
  n.setIndices (indicesptr);
  n.setSearchMethod (tree);
  n.setRadiusSearch (20 * mr);
  n.compute (*normals);

  search::KdTree<PointXYZRGBA>::Ptr rgbaTree;

  rgbaTree.reset (new search::KdTree<PointXYZRGBA> (false));

  // Object
  SHOTEstimationOMP<PointXYZRGBA, Normal, SHOT> shot (true, true, -1);
  shot.setInputNormals (normals);

  EXPECT_EQ (shot.getInputNormals (), normals);

  shot.setRadiusSearch ( 20 * mr);

  // Create fake point cloud with colors
  PointCloud<PointXYZRGBA> cloudWithColors;
  for (size_t i = 0; i < cloud.points.size (); ++i)
  {
    PointXYZRGBA p;
    p.x = cloud.points[i].x;
    p.y = cloud.points[i].y;
    p.z = cloud.points[i].z;

    p.rgba = ( (i%255) << 16 ) + ( ( (255 - i ) %255) << 8) + ( ( i*37 ) %255);
    cloudWithColors.push_back(p);
  }

  rgbaTree->setInputCloud (cloudWithColors.makeShared ());

  PointCloud<SHOT>::Ptr shots (new PointCloud<SHOT> ());

  shot.setInputCloud (cloudWithColors.makeShared ());
  shot.setIndices (indicesptr);
  shot.setSearchMethod (rgbaTree);

  // estimate
  shot.compute (*shots);
  EXPECT_EQ (shots->points.size (), indices.size ());

  EXPECT_NEAR (shots->points[103].descriptor[10], 0.0020453099, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[11], 0.0021887729, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[21], 0.062557608, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[42], 0.011778189, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[53], 0.0065085669, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[54], 0.012025614, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[55], 0.0044803056, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[64], 0.064429596, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[65], 0.046486385, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[86], 0.011518310, 1e-5);

  EXPECT_NEAR (shots->points[103].descriptor[357], 0.0020453099, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[360], 0.0027993850, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[386], 0.045115642, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[387], 0.059068538, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[389], 0.0047547864, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[453], 0.0051176427, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[481], 0.0053625242, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[482], 0.012025614, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[511], 0.0057367259, 1e-5);
  EXPECT_NEAR (shots->points[103].descriptor[512], 0.048357654, 1e-5);

  // Test results when setIndices and/or setSearchSurface are used
  boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
  for (size_t i = 0; i < cloud.size (); i+=3)
    test_indices->push_back (i);

  testSHOTIndicesAndSearchSurface<SHOTEstimationOMP<PointXYZRGBA, Normal, SHOT>, PointXYZRGBA, Normal, SHOT> (cloudWithColors.makeShared (), normals, test_indices);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TEST (PCL, 3DSCEstimation)
{
  float meshRes = 0.002;
  size_t nBinsL = 4;
  size_t nBinsK = 4;
  size_t nBinsJ = 4;
  float radius = 20.0 * meshRes;
  float rmin = radius / 10.0;
  float ptDensityRad = radius / 5.0;

  PointCloud<PointXYZ>::Ptr cloudptr = cloud.makeShared ();

  // Estimate normals first
  NormalEstimation<PointXYZ, Normal> ne;
  PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
  // set parameters
  ne.setInputCloud (cloudptr);
  ne.setSearchMethod (tree);
  ne.setRadiusSearch (radius);
  // estimate
  ne.compute (*normals);
  ShapeContext3DEstimation<PointXYZ, Normal, SHOT> sc3d;
  sc3d.setInputCloud (cloudptr);
  sc3d.setInputNormals (normals);
  sc3d.setSearchMethod (tree);
  sc3d.setRadiusSearch (radius);
  sc3d.setAzimuthBins (nBinsL);
  sc3d.setElevationBins (nBinsK);
  sc3d.setRadiusBins (nBinsJ);
  sc3d.setMinimalRadius (rmin);
  sc3d.setPointDensityRadius (ptDensityRad);
  // Compute the features
  PointCloud<SHOT>::Ptr sc3ds (new PointCloud<SHOT> ());
  sc3d.compute (*sc3ds);
  EXPECT_EQ (sc3ds->size (), cloud.size ());

  // 3DSC does not define a repeatable local RF, we set it to zero to signal it to the user
  //EXPECT_NEAR ((*sc3ds)[0].rf[0], 0.2902f, 1e-4f);
  //EXPECT_NEAR ((*sc3ds)[0].rf[1], 0.7334f, 1e-4f);
  //EXPECT_NEAR ((*sc3ds)[0].rf[2], -0.6146f, 1e-4f);
  //EXPECT_NEAR ((*sc3ds)[0].rf[3], 0.9486f, 1e-4f);
  //EXPECT_NEAR ((*sc3ds)[0].rf[4], -0.3051f, 1e-4f);
  //EXPECT_NEAR ((*sc3ds)[0].rf[5], 0.0838f, 1e-4f);
  //EXPECT_NEAR ((*sc3ds)[0].rf[6], -0.1261f, 1e-4f);
  //EXPECT_NEAR ((*sc3ds)[0].rf[7], -0.6074f, 1e-4f);
  //EXPECT_NEAR ((*sc3ds)[0].rf[8], -0.7843f, 1e-4f);

  EXPECT_NEAR ((*sc3ds)[0].rf[0], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].rf[1], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].rf[2], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].rf[3], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].rf[4], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].rf[5], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].rf[6], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].rf[7], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].rf[8], 0.0f, 1e-4f);

  EXPECT_EQ ((*sc3ds)[0].descriptor.size (), 64);
  EXPECT_NEAR ((*sc3ds)[0].descriptor[4], 52.2474f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].descriptor[6], 150.901611328125, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].descriptor[7], 169.09703063964844, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].descriptor[8], 0, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[0].descriptor[21], 39.1745f, 1e-4f);

  EXPECT_NEAR ((*sc3ds)[2].descriptor[4], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[6], 73.7986f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[7], 209.97763061523438, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[9], 68.5553f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[16], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[17], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[18], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[20], 0.0f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[21], 39.1745f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[22], 154.2060f, 1e-4f);
  EXPECT_NEAR ((*sc3ds)[2].descriptor[23], 275.63433837890625, 1e-4f);

  // Test results when setIndices and/or setSearchSurface are used
  boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
  for (size_t i = 0; i < cloud.size (); i++)
    test_indices->push_back (i);

  testSHOTIndicesAndSearchSurface<ShapeContext3DEstimation<PointXYZ, Normal, SHOT>, PointXYZ, Normal, SHOT> (cloudptr, normals, test_indices);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TEST (PCL, USCEstimation)
{
  float meshRes = 0.002;
  size_t nBinsL = 4;
  size_t nBinsK = 4;
  size_t nBinsJ = 4;
  float radius = 20.0 * meshRes;
  float rmin = radius / 10.0;
  float ptDensityRad = radius / 5.0;

  // estimate
  UniqueShapeContext<PointXYZ, SHOT> uscd;
  uscd.setInputCloud (cloud.makeShared ());
  uscd.setSearchMethod (tree);
  uscd.setRadiusSearch (radius);
  uscd.setAzimuthBins (nBinsL);
  uscd.setElevationBins (nBinsK);
  uscd.setRadiusBins (nBinsJ);
  uscd.setMinimalRadius (rmin);
  uscd.setPointDensityRadius (ptDensityRad);
  uscd.setLocalRadius (radius);
  // Compute the features
  PointCloud<SHOT>::Ptr uscds (new PointCloud<SHOT>);
  uscd.compute (*uscds);
  EXPECT_EQ (uscds->size (), cloud.size ());

  EXPECT_NEAR ((*uscds)[0].rf[0], 0.9876f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].rf[1], -0.1408f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].rf[2], -0.06949f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].rf[3], -0.06984f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].rf[4], -0.7904f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].rf[5], 0.6086f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].rf[6], -0.1406f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].rf[7], -0.5962f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].rf[8], -0.7904f, 1e-4f);

  EXPECT_EQ ((*uscds)[0].descriptor.size (), 64);
  EXPECT_NEAR ((*uscds)[0].descriptor[4], 52.2474f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].descriptor[5], 39.1745f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].descriptor[6], 176.2354f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].descriptor[7], 199.4478f, 1e-4f);
  EXPECT_NEAR ((*uscds)[0].descriptor[8], 0.0f, 1e-4f);

  EXPECT_NEAR ((*uscds)[2].descriptor[6], 110.1472f, 1e-4f);
  EXPECT_NEAR ((*uscds)[2].descriptor[7], 145.5597f, 1e-4f);
  EXPECT_NEAR ((*uscds)[2].descriptor[8], 69.6632f, 1e-4f);
  EXPECT_NEAR ((*uscds)[2].descriptor[22], 57.2765f, 1e-4f);
  EXPECT_NEAR ((*uscds)[2].descriptor[23], 172.8134f, 1e-4f);
  EXPECT_NEAR ((*uscds)[2].descriptor[25], 68.5554f, 1e-4f);
  EXPECT_NEAR ((*uscds)[2].descriptor[26], 0.0f, 1e-4f);
  EXPECT_NEAR ((*uscds)[2].descriptor[27], 0.0f, 1e-4f);
  EXPECT_NEAR ((*uscds)[2].descriptor[37], 39.1745f, 1e-4f);
  EXPECT_NEAR ((*uscds)[2].descriptor[38], 71.5957f, 1e-4f);

  // Test results when setIndices and/or setSearchSurface are used
  boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
  for (size_t i = 0; i < cloud.size (); i+=3)
    test_indices->push_back (i);

  PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
  testSHOTIndicesAndSearchSurface<UniqueShapeContext<PointXYZ, SHOT>, PointXYZ, Normal, SHOT> (cloud.makeShared (), normals, test_indices);
}

#ifndef PCL_ONLY_CORE_POINT_TYPES
  ///////////////////////////////////////////////////////////////////////////////////
  template <> UniqueShapeContext<PointXYZ, Eigen::MatrixXf>
  createSHOTDesc<UniqueShapeContext<PointXYZ, Eigen::MatrixXf>, PointXYZ, Normal, Eigen::MatrixXf> (
      const PointCloud<Normal>::Ptr & normals,
      const int nr_shape_bins,
      const int nr_color_bins,
      const bool describe_shape,
      const bool describe_color)
  {
    UniqueShapeContext<PointXYZ, Eigen::MatrixXf> usc;
    usc.setAzimuthBins (4);
    usc.setElevationBins (4);
    usc.setRadiusBins (4);
    usc.setMinimalRadius (0.004);
    usc.setPointDensityRadius (0.008);
    usc.setLocalRadius (0.04);
    return (usc);
  }

  ///////////////////////////////////////////////////////////////////////////////////
  template <> ShapeContext3DEstimation<PointXYZ, Normal, Eigen::MatrixXf>
  createSHOTDesc<ShapeContext3DEstimation<PointXYZ, Normal, Eigen::MatrixXf>, PointXYZ, Normal, Eigen::MatrixXf> (
      const PointCloud<Normal>::Ptr & normals,
      const int nr_shape_bins,
      const int nr_color_bins,
      const bool describe_shape,
      const bool describe_color)
  {
    ShapeContext3DEstimation<PointXYZ, Normal, Eigen::MatrixXf> sc3d;
    sc3d.setAzimuthBins (4);
    sc3d.setElevationBins (4);
    sc3d.setRadiusBins (4);
    sc3d.setMinimalRadius (0.004);
    sc3d.setPointDensityRadius (0.008);
    sc3d.setInputNormals (normals);
    return (sc3d);
  }

  ///////////////////////////////////////////////////////////////////////////////////
  template <typename FeatureEstimation, typename PointT, typename NormalT> void
  testSHOTIndicesAndSearchSurfaceEigen (const typename PointCloud<PointT>::Ptr & points,
                                        const typename PointCloud<NormalT>::Ptr & normals,
                                        const boost::shared_ptr<vector<int> > & indices,
                                        const int nr_shape_bins = 10,
                                        const int nr_color_bins = 30,
                                        const bool describe_shape = true,
                                        const bool describe_color = false)
  {
    double radius = 0.04;
    //
    // Test setIndices and setSearchSurface
    //
    PointCloud<Eigen::MatrixXf> full_output, output0, output1, output2;

    // Compute for all points and then subsample the results
    FeatureEstimation est0 = createSHOTDesc<FeatureEstimation, PointT, NormalT, Eigen::MatrixXf>(normals, nr_shape_bins,nr_color_bins,describe_shape,describe_color);
    est0.setSearchMethod (typename search::KdTree<PointT>::Ptr (new search::KdTree<PointT>));
    est0.setRadiusSearch (radius);
    est0.setInputCloud (points);
    est0.computeEigen (full_output);

    output0 = PointCloud<Eigen::MatrixXf> (full_output, *indices);

    // Compute with all points as "search surface" and the specified sub-cloud as "input"
    typename PointCloud<PointT>::Ptr subpoints (new PointCloud<PointT>);
    copyPointCloud (*points, *indices, *subpoints);
    FeatureEstimation est1 = createSHOTDesc<FeatureEstimation, PointT, NormalT, Eigen::MatrixXf>(normals, nr_shape_bins,nr_color_bins,describe_shape,describe_color);
    est1.setSearchMethod (typename search::KdTree<PointT>::Ptr (new search::KdTree<PointT>));
    est1.setRadiusSearch (radius);
    est1.setInputCloud (subpoints);
    est1.setSearchSurface (points);
    est1.computeEigen (output1);

    //// Compute with all points as "input" and the specified indices
    FeatureEstimation est2 = createSHOTDesc<FeatureEstimation, PointT, NormalT, Eigen::MatrixXf>(normals, nr_shape_bins,nr_color_bins,describe_shape,describe_color);
    est2.setSearchMethod (typename search::KdTree<PointT>::Ptr (new search::KdTree<PointT>));
    est2.setRadiusSearch (radius);
    est2.setInputCloud (points);
    est2.setIndices (indices);
    est2.computeEigen (output2);

    // All three of the above cases should produce equivalent results
    ASSERT_EQ (output0.points.rows (), output1.points.rows ());
    ASSERT_EQ (output1.points.rows (), output2.points.rows ());
    for (int i = 0; i < output1.points.rows (); ++i)
    {
      for (int j = 0; j < output0.points.cols (); ++j)
      {
        ASSERT_EQ (output0.points (i, j), output1.points (i, j));
        ASSERT_EQ (output1.points (i, j), output2.points (i, j));
      }
    }

    //
    // Test the combination of setIndices and setSearchSurface
    //
    PointCloud<Eigen::MatrixXf> output3, output4;

    boost::shared_ptr<vector<int> > indices2 (new vector<int> (0));
    for (size_t i = 0; i < (indices->size ()/2); ++i)
      indices2->push_back (i);

    // Compute with all points as search surface + the specified sub-cloud as "input" but for only a subset of indices
    FeatureEstimation est3 = createSHOTDesc<FeatureEstimation, PointT, NormalT, Eigen::MatrixXf>(normals, nr_shape_bins,nr_color_bins,describe_shape,describe_color);
    est3.setSearchMethod (typename search::KdTree<PointT>::Ptr (new search::KdTree<PointT>));
    est3.setRadiusSearch (0.04);
    est3.setSearchSurface (points);
    est3.setInputCloud (subpoints);
    est3.setIndices (indices2);
    est3.computeEigen (output3);

    // Start with features for each point in "subpoints" and then subsample the results
    output4 = PointCloud<Eigen::MatrixXf> (output0, *indices2);

    // The two cases above should produce equivalent results
    ASSERT_EQ (output3.points.rows (), output4.points.rows ());
    for (int i = 0; i < output3.points.rows (); ++i)
      for (int j = 0; j < output3.points.cols (); ++j)
        ASSERT_EQ (output3.points (i, j), output4.points (i, j));
  }

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  TEST (PCL, SHOTShapeEstimationEigen)
  {
    // Estimate normals first
    double mr = 0.002;
    NormalEstimation<PointXYZ, Normal> n;
    PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
    // set parameters
    n.setInputCloud (cloud.makeShared ());
    boost::shared_ptr<vector<int> > indicesptr (new vector<int> (indices));
    n.setIndices (indicesptr);
    n.setSearchMethod (tree);
    n.setRadiusSearch (20 * mr);
    n.compute (*normals);

    EXPECT_NEAR (normals->points[103].normal_x, 0.36683175, 1e-4);
    EXPECT_NEAR (normals->points[103].normal_y, -0.44696972, 1e-4);
    EXPECT_NEAR (normals->points[103].normal_z, -0.81587529, 1e-4);
    EXPECT_NEAR (normals->points[200].normal_x, -0.71414840, 1e-4);
    EXPECT_NEAR (normals->points[200].normal_y, -0.06002361, 1e-4);
    EXPECT_NEAR (normals->points[200].normal_z, -0.69741613, 1e-4);

    EXPECT_NEAR (normals->points[140].normal_x, -0.45109111, 1e-4);
    EXPECT_NEAR (normals->points[140].normal_y, -0.19499126, 1e-4);
    EXPECT_NEAR (normals->points[140].normal_z, -0.87091631, 1e-4);

    SHOTEstimation<PointXYZ, Normal, Eigen::MatrixXf> shot;
    shot.setInputNormals (normals);
    EXPECT_EQ (shot.getInputNormals (), normals);
    shot.setRadiusSearch (20 * mr);

    // Object
    PointCloud<Eigen::MatrixXf>::Ptr shots (new PointCloud<Eigen::MatrixXf>);

    // set parameters
    shot.setInputCloud (cloud.makeShared ());
    shot.setIndices (indicesptr);
    shot.setSearchMethod (tree);

    // estimate
    shot.computeEigen (*shots);
    EXPECT_EQ (shots->points.rows (), indices.size ());

    EXPECT_NEAR (shots->points (103, 9 ), 0.0072018504, 1e-4);
    EXPECT_NEAR (shots->points (103, 10), 0.0023103887, 1e-4);
    EXPECT_NEAR (shots->points (103, 11), 0.0024724449, 1e-4);
    EXPECT_NEAR (shots->points (103, 19), 0.0031367359, 1e-4);
    EXPECT_NEAR (shots->points (103, 20), 0.17439659, 1e-4);
    EXPECT_NEAR (shots->points (103, 21), 0.070665278, 1e-4);
    EXPECT_NEAR (shots->points (103, 42), 0.013304681, 1e-4);
    EXPECT_NEAR (shots->points (103, 53), 0.0073520984, 1e-4);
    EXPECT_NEAR (shots->points (103, 54), 0.013584172, 1e-4);
    EXPECT_NEAR (shots->points (103, 55), 0.0050609680, 1e-4);

    // Test results when setIndices and/or setSearchSurface are used
    boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
    for (size_t i = 0; i < cloud.points.size (); i+=3)
      test_indices->push_back (i);

    testSHOTIndicesAndSearchSurfaceEigen<SHOTEstimation<PointXYZ, Normal, Eigen::MatrixXf>, PointXYZ, Normal> (cloud.makeShared (), normals, test_indices);

  }

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  TEST (PCL, GenericSHOTShapeEstimationEigen)
  {
    // SHOT length
    const int shapeStep_ = 20;
    //const int dim = 32*(shapeStep_+1);

    // Estimate normals first
    double mr = 0.002;
    NormalEstimation<PointXYZ, Normal> n;
    PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
    // set parameters
    n.setInputCloud (cloud.makeShared ());
    boost::shared_ptr<vector<int> > indicesptr (new vector<int> (indices));
    n.setIndices (indicesptr);
    n.setSearchMethod (tree);
    n.setRadiusSearch (20 * mr);
    n.compute (*normals);

    SHOTEstimation<PointXYZ, Normal, Eigen::MatrixXf> shot (shapeStep_);
    shot.setInputNormals (normals);
    EXPECT_EQ (shot.getInputNormals (), normals);

    shot.setRadiusSearch (20 * mr);

    PointCloud<Eigen::MatrixXf>::Ptr shots (new PointCloud<Eigen::MatrixXf>);

    // set parameters
    shot.setInputCloud (cloud.makeShared ());
    shot.setIndices (indicesptr);
    shot.setSearchMethod (tree);

    // estimate
    shot.computeEigen (*shots);
    EXPECT_EQ (shots->points.rows (), indices.size ());

    EXPECT_NEAR (shots->points (103, 18), 0.0077019366, 1e-5);
    EXPECT_NEAR (shots->points (103, 19), 0.0024708188, 1e-5);
    EXPECT_NEAR (shots->points (103, 21), 0.0079652183, 1e-5);
    EXPECT_NEAR (shots->points (103, 38), 0.0067090928, 1e-5);
    EXPECT_NEAR (shots->points (103, 39), 0.17498907, 1e-5);
    EXPECT_NEAR (shots->points (103, 40), 0.078413926, 1e-5);
    EXPECT_NEAR (shots->points (103, 81), 0.014228539, 1e-5);
    EXPECT_NEAR (shots->points (103, 103), 0.022390056, 1e-5);
    EXPECT_NEAR (shots->points (103, 105), 0.0058866320, 1e-5);
    EXPECT_NEAR (shots->points (103, 123), 0.019105887, 1e-5);

    // Test results when setIndices and/or setSearchSurface are used
    boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
    for (size_t i = 0; i < cloud.size (); i+=3)
      test_indices->push_back (i);

    testSHOTIndicesAndSearchSurfaceEigen<SHOTEstimation<PointXYZ, Normal, Eigen::MatrixXf>, PointXYZ, Normal> (cloud.makeShared (), normals, test_indices, shapeStep_);
  }

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  TEST (PCL, SHOTShapeAndColorEstimationEigen)
  {
    double mr = 0.002;
    // Estimate normals first
    NormalEstimation<PointXYZ, Normal> n;
    PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
    // set parameters
    n.setInputCloud (cloud.makeShared ());
    boost::shared_ptr<vector<int> > indicesptr (new vector<int> (indices));
    n.setIndices (indicesptr);
    n.setSearchMethod (tree);
    n.setRadiusSearch (20 * mr);
    n.compute (*normals);

    search::KdTree<PointXYZRGBA>::Ptr rgbaTree;
    rgbaTree.reset (new search::KdTree<PointXYZRGBA> (false));

    // Object
    SHOTEstimation<PointXYZRGBA, Normal, Eigen::MatrixXf> shot (true, true);
    shot.setInputNormals (normals);
    EXPECT_EQ (shot.getInputNormals (), normals);

    shot.setRadiusSearch (20 * mr);

    // Create fake point cloud with colors
    PointCloud<PointXYZRGBA> cloudWithColors;
    for (size_t i = 0; i < cloud.points.size (); ++i)
    {
      PointXYZRGBA p;
      p.x = cloud.points[i].x;
      p.y = cloud.points[i].y;
      p.z = cloud.points[i].z;

      p.rgba = ( (i%255) << 16 ) + ( ( (255 - i ) %255) << 8) + ( ( i*37 ) %255);
      cloudWithColors.push_back (p);
    }

    rgbaTree->setInputCloud (cloudWithColors.makeShared ());
    PointCloud<Eigen::MatrixXf>::Ptr shots (new PointCloud<Eigen::MatrixXf>);

    shot.setInputCloud (cloudWithColors.makeShared ());
    shot.setIndices (indicesptr);
    shot.setSearchMethod (rgbaTree);

    // estimate
    shot.computeEigen (*shots);
    EXPECT_EQ (shots->points.rows (), indices.size ());

    EXPECT_NEAR (shots->points (103, 10), 0.0020453099, 1e-5);
    EXPECT_NEAR (shots->points (103, 11), 0.0021887729, 1e-5);
    EXPECT_NEAR (shots->points (103, 21), 0.062557608, 1e-5);
    EXPECT_NEAR (shots->points (103, 42), 0.011778189, 1e-5);
    EXPECT_NEAR (shots->points (103, 53), 0.0065085669, 1e-5);
    EXPECT_NEAR (shots->points (103, 54), 0.012025614, 1e-5);
    EXPECT_NEAR (shots->points (103, 55), 0.0044803056, 1e-5);
    EXPECT_NEAR (shots->points (103, 64), 0.064429596, 1e-5);
    EXPECT_NEAR (shots->points (103, 65), 0.046486385, 1e-5);
    EXPECT_NEAR (shots->points (103, 86), 0.011518310, 1e-5);

    EXPECT_NEAR (shots->points (103, 357), 0.0020453099, 1e-5);
    EXPECT_NEAR (shots->points (103, 360), 0.0027993850, 1e-5);
    EXPECT_NEAR (shots->points (103, 386), 0.045115642, 1e-5);
    EXPECT_NEAR (shots->points (103, 387), 0.059068538, 1e-5);
    EXPECT_NEAR (shots->points (103, 389), 0.0047547864, 1e-5);
    EXPECT_NEAR (shots->points (103, 453), 0.0051176427, 1e-5);
    EXPECT_NEAR (shots->points (103, 481), 0.0053625242, 1e-5);
    EXPECT_NEAR (shots->points (103, 482), 0.012025614, 1e-5);
    EXPECT_NEAR (shots->points (103, 511), 0.0057367259, 1e-5);
    EXPECT_NEAR (shots->points (103, 512), 0.048357654, 1e-5);

    // Test results when setIndices and/or setSearchSurface are used
    boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
    for (size_t i = 0; i < cloud.size (); i+=3)
      test_indices->push_back (i);

    testSHOTIndicesAndSearchSurfaceEigen<SHOTEstimation<PointXYZRGBA, Normal, Eigen::MatrixXf>, PointXYZRGBA, Normal> (cloudWithColors.makeShared (), normals, test_indices);
  }

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  TEST (PCL, 3DSCEstimationEigen)
  {
    float meshRes = 0.002;
    size_t nBinsL = 4;
    size_t nBinsK = 4;
    size_t nBinsJ = 4;
    float radius = 20.0 * meshRes;
    float rmin = radius / 10.0;
    float ptDensityRad = radius / 5.0;

    PointCloud<PointXYZ>::Ptr cloudptr = cloud.makeShared ();

    // Estimate normals first
    NormalEstimation<PointXYZ, Normal> ne;
    PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
    // set parameters
    ne.setInputCloud (cloudptr);
    ne.setSearchMethod (tree);
    ne.setRadiusSearch (radius);
    // estimate
    ne.compute (*normals);
    ShapeContext3DEstimation<PointXYZ, Normal, Eigen::MatrixXf> sc3d;
    sc3d.setInputCloud (cloudptr);
    sc3d.setInputNormals (normals);
    sc3d.setSearchMethod (tree);
    sc3d.setRadiusSearch (radius);
    sc3d.setAzimuthBins (nBinsL);
    sc3d.setElevationBins (nBinsK);
    sc3d.setRadiusBins (nBinsJ);
    sc3d.setMinimalRadius (rmin);
    sc3d.setPointDensityRadius (ptDensityRad);
    // Compute the features
    PointCloud<Eigen::MatrixXf>::Ptr sc3ds (new PointCloud<Eigen::MatrixXf>);
    sc3d.computeEigen (*sc3ds);
    EXPECT_EQ (sc3ds->points.rows (), cloud.size ());

    // 3DSC does not define a repeatable local RF, we set it to zero to signal it to the user
    //EXPECT_NEAR ((*sc3ds)[0].rf[0], 0.2902f, 1e-4f);
    //EXPECT_NEAR ((*sc3ds)[0].rf[1], 0.7334f, 1e-4f);
    //EXPECT_NEAR ((*sc3ds)[0].rf[2], -0.6146f, 1e-4f);
    //EXPECT_NEAR ((*sc3ds)[0].rf[3], 0.9486f, 1e-4f);
    //EXPECT_NEAR ((*sc3ds)[0].rf[4], -0.3051f, 1e-4f);
    //EXPECT_NEAR ((*sc3ds)[0].rf[5], 0.0838f, 1e-4f);
    //EXPECT_NEAR ((*sc3ds)[0].rf[6], -0.1261f, 1e-4f);
    //EXPECT_NEAR ((*sc3ds)[0].rf[7], -0.6074f, 1e-4f);
    //EXPECT_NEAR ((*sc3ds)[0].rf[8], -0.7843f, 1e-4f);

    EXPECT_NEAR (sc3ds->points (0, 0), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 1), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 2), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 3), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 4), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 5), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 6), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 7), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 8), 0.0f, 1e-4f);

    EXPECT_EQ   (sc3ds->points.row (0).size (), 64 + 9);
    EXPECT_NEAR (sc3ds->points (0, 9 + 4), 52.2474f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 9 + 6), 150.901611328125, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 9 + 7), 169.09703063964844, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 9 + 8), 0, 1e-4f);
    EXPECT_NEAR (sc3ds->points (0, 9 + 21), 39.1745f, 1e-4f);

    EXPECT_NEAR (sc3ds->points (2, 9 + 4), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (2, 9 + 6), 73.7986f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (2, 9 + 7), 209.97763061523438, 1e-4f);

    EXPECT_NEAR (sc3ds->points (2, 9 + 9), 68.5553f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (2, 9 + 16), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (2, 9 + 17), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (2, 9 + 18), 0.0f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (2, 9 + 20), 0.0f, 1e-4f);

    EXPECT_NEAR (sc3ds->points (2, 9 + 21), 39.1745f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (2, 9 + 22), 154.2060f, 1e-4f);
    EXPECT_NEAR (sc3ds->points (2, 9 + 23), 275.63433837890625, 1e-4f);

    // Test results when setIndices and/or setSearchSurface are used
    boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
    for (size_t i = 0; i < cloud.size (); i++)
      test_indices->push_back (i);

    testSHOTIndicesAndSearchSurfaceEigen<ShapeContext3DEstimation<PointXYZ, Normal, Eigen::MatrixXf>, PointXYZ, Normal> (cloudptr, normals, test_indices);
  }

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  TEST (PCL, USCEstimationEigen)
  {
    float meshRes = 0.002;
    size_t nBinsL = 4;
    size_t nBinsK = 4;
    size_t nBinsJ = 4;
    float radius = 20.0 * meshRes;
    float rmin = radius / 10.0;
    float ptDensityRad = radius / 5.0;

    // estimate
    UniqueShapeContext<PointXYZ, Eigen::MatrixXf> uscd;
    uscd.setInputCloud (cloud.makeShared ());
    uscd.setSearchMethod (tree);
    uscd.setRadiusSearch (radius);
    uscd.setAzimuthBins (nBinsL);
    uscd.setElevationBins (nBinsK);
    uscd.setRadiusBins (nBinsJ);
    uscd.setMinimalRadius (rmin);
    uscd.setPointDensityRadius (ptDensityRad);
    uscd.setLocalRadius (radius);
    // Compute the features
    PointCloud<Eigen::MatrixXf>::Ptr uscds (new PointCloud<Eigen::MatrixXf>);
    uscd.computeEigen (*uscds);
    EXPECT_EQ (uscds->points.rows (), cloud.size ());

    EXPECT_NEAR (uscds->points (0, 0), 0.9876f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 1), -0.1408f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 2), -0.06949f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 3), -0.06984f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 4), -0.7904f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 5), 0.6086f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 6), -0.1406f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 7), -0.5962f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 8), -0.7904f, 1e-4f);

    EXPECT_EQ   (uscds->points.row (0).size (), 9+64);
    EXPECT_NEAR (uscds->points (0, 9 + 4), 52.2474f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 9 + 5), 39.1745f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 9 + 6), 176.2354f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 9 + 7), 199.4478f, 1e-4f);
    EXPECT_NEAR (uscds->points (0, 9 + 8), 0.0f, 1e-4f);

    EXPECT_NEAR (uscds->points (2, 9 + 6), 110.1472f, 1e-4f);
    EXPECT_NEAR (uscds->points (2, 9 + 7), 145.5597f, 1e-4f);
    EXPECT_NEAR (uscds->points (2, 9 + 8), 69.6632f, 1e-4f);
    EXPECT_NEAR (uscds->points (2, 9 + 22), 57.2765f, 1e-4f);
    EXPECT_NEAR (uscds->points (2, 9 + 23), 172.8134f, 1e-4f);
    EXPECT_NEAR (uscds->points (2, 9 + 25), 68.5554f, 1e-4f);
    EXPECT_NEAR (uscds->points (2, 9 + 26), 0.0f, 1e-4f);
    EXPECT_NEAR (uscds->points (2, 9 + 27), 0.0f, 1e-4f);
    EXPECT_NEAR (uscds->points (2, 9 + 37), 39.1745f, 1e-4f);
    EXPECT_NEAR (uscds->points (2, 9 + 38), 71.5957f, 1e-4f);

    // Test results when setIndices and/or setSearchSurface are used
    boost::shared_ptr<vector<int> > test_indices (new vector<int> (0));
    for (size_t i = 0; i < cloud.size (); i+=3)
      test_indices->push_back (i);

    PointCloud<Normal>::Ptr normals (new PointCloud<Normal> ());
    testSHOTIndicesAndSearchSurfaceEigen<UniqueShapeContext<PointXYZ, Eigen::MatrixXf>, PointXYZ, Normal> (cloud.makeShared (), normals, test_indices);
  }
#endif

/* ---[ */
int
main (int argc, char** argv)
{
  if (argc < 2)
  {
    std::cerr << "No test file given. Please download `bun0.pcd` and pass its path to the test." << std::endl;
    return (-1);
  }

  if (loadPCDFile<PointXYZ> (argv[1], cloud) < 0)
  {
    std::cerr << "Failed to read test file. Please download `bun0.pcd` and pass its path to the test." << std::endl;
    return (-1);
  }

  indices.resize (cloud.points.size ());
  for (size_t i = 0; i < indices.size (); ++i)
  {
    indices[i] = i;
  }

  tree.reset (new search::KdTree<PointXYZ> (false));
  tree->setInputCloud (cloud.makeShared ());

  testing::InitGoogleTest (&argc, argv);
  return (RUN_ALL_TESTS ());
}
/* ]--- */
