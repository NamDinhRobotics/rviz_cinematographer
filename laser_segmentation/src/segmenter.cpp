/** @file
 *
 * This class segments objects of a specified width in laser point clouds
 */

#include <laser_segmentation/segmenter.h>

namespace laser_segmentation
{
Segmenter::Segmenter(ros::NodeHandle node, ros::NodeHandle private_nh)
: PUCK_NUM_RINGS(16)
 , HOKUYO_NUM_RINGS(1)  
 , m_input_is_velodyne(true)       
 , m_circular_buffer_capacity_launch(6000)
 , m_angle_between_scanpoints_launch(0.2f) // 0.1 for 5Hz 0.2 for 10Hz 0.4 for 20Hz
 , m_certainty_threshold_launch(0.0)
 , m_dist_weight_launch(0.75)
 , m_intensity_weight_launch(0.25)
 , m_object_size_launch(1.2f)
 , m_distance_to_comparison_points_launch(2.f)
 , m_kernel_size_diff_factor_launch(5.0)
 , m_median_min_dist_launch(2.5)
 , m_median_thresh1_dist_launch(5.0)
 , m_median_thresh2_dist_launch(200.0)
 , m_median_max_dist_launch(200.0)
 , m_max_dist_for_median_computation_launch(0.0)
 , m_max_kernel_size(100)
 , m_max_prob_by_distance(1.f)
 , m_max_intensity_range(100.f)
 , m_certainty_threshold("laser_segmentation/certainty_threshold", 0.0, 0.01, 1.0, m_certainty_threshold_launch)
 , m_dist_weight("laser_segmentation/dist_weight", 0.0, 0.1, 10.0, m_dist_weight_launch)
 , m_intensity_weight("laser_segmentation/intensity_weight", 0.0, 0.01, 10.0, m_intensity_weight_launch)
 , m_weight_for_small_intensities("laser_segmentation/weight_for_small_intensities", 1.f, 1.f, 30.f, 10.f)
 , m_object_size("laser_segmentation/object_size_in_m", 0.005, 0.005, 5.0, m_object_size_launch)
 , m_distance_to_comparison_points("laser_segmentation/distance_to_comparison_points", 0.0, 0.01, 10.0, 0.38f)
 , m_kernel_size_diff_factor("laser_segmentation/kernel_size_diff_factor", 1.0, 0.1, 5.0, m_kernel_size_diff_factor_launch)
 , m_median_min_dist("laser_segmentation/median_min_dist", 0.0, 0.01, 5.0, m_median_min_dist_launch)
 , m_median_thresh1_dist("laser_segmentation/median_thresh1_dist", 0.0001, 0.05, 12.5, m_median_thresh1_dist_launch)
 , m_median_thresh2_dist("laser_segmentation/median_thresh2_dist", 0.0, 0.1, 200.0, m_median_thresh2_dist_launch)
 , m_median_max_dist("laser_segmentation/median_max_dist", 0.0, 0.5, 200.0, m_median_max_dist_launch)
 , m_max_dist_for_median_computation("laser_segmentation/max_dist_for_median_computation", 0.0, 0.25, 10.0, 6.0)
 , m_input_topic("/velodyne_points")
 , m_publish_debug_clouds(false)
 , m_buffer_initialized(false)
{
   ROS_INFO("laser_segmentation::Segmenter: Init...");

   private_nh.getParam("input_topic", m_input_topic);
   private_nh.getParam("input_is_velodyne", m_input_is_velodyne);
   if(m_input_is_velodyne)
     m_velodyne_sub = node.subscribe(m_input_topic, 1, &Segmenter::velodyneCallback, this);
   else
     m_hokuyo_sub = node.subscribe(m_input_topic, 1, &Segmenter::hokuyoCallback, this);
  
   private_nh.getParam("publish_debug_cloud", m_publish_debug_clouds);

   if(m_publish_debug_clouds)
   {
      m_plotter = new pcl::visualization::PCLPlotter ("Segmentation Plotter");
      m_plotter->setShowLegend (true);
      m_plotter->setXTitle("distance difference in meters");
      m_plotter->setYTitle("object certainty");
      plot();

      m_pub_debug_obstacle_cloud = node.advertise<DebugOutputPointCloud >("/laser_segmenter_debug_objects", 1);
      m_pub_filtered_cloud = node.advertise<DebugOutputPointCloud >("/laser_segmenter_filtered", 1);
   }

   m_pub_obstacle_cloud = node.advertise<OutputPointCloud >("/laser_segmenter_objects", 1);

   if(private_nh.getParam("certainty_threshold", m_certainty_threshold_launch))
      m_certainty_threshold.set(m_certainty_threshold_launch);

   if(private_nh.getParam("dist_weight", m_dist_weight_launch))
      m_dist_weight.set(m_dist_weight_launch);

   if(private_nh.getParam("intensity_weight", m_intensity_weight_launch))
      m_intensity_weight.set(m_intensity_weight_launch);

   if(private_nh.getParam("object_size_in_m", m_object_size_launch))
      m_object_size.set(m_object_size_launch);

   if(private_nh.getParam("distance_to_comparison_points", m_distance_to_comparison_points_launch))
      m_distance_to_comparison_points.set(m_distance_to_comparison_points_launch);

   if(private_nh.getParam("kernel_size_diff_factor", m_kernel_size_diff_factor_launch))
      m_kernel_size_diff_factor.set(m_kernel_size_diff_factor_launch);

   if(private_nh.getParam("median_min_dist", m_median_min_dist_launch))
      m_median_min_dist.set(m_median_min_dist_launch);

   if(private_nh.getParam("median_thresh1_dist", m_median_thresh1_dist_launch))
      m_median_thresh1_dist.set(m_median_thresh1_dist_launch);

   if(private_nh.getParam("median_thresh2_dist", m_median_thresh2_dist_launch))
      m_median_thresh2_dist.set(m_median_thresh2_dist_launch);

   if(private_nh.getParam("median_max_dist", m_median_max_dist_launch))
      m_median_max_dist.set(m_median_max_dist_launch);

   if(private_nh.getParam("max_dist_for_median_computation", m_max_dist_for_median_computation_launch))
      m_max_dist_for_median_computation.set(m_max_dist_for_median_computation_launch);

   private_nh.getParam("circular_buffer_capacity", m_circular_buffer_capacity_launch);
   private_nh.getParam("max_kernel_size", m_max_kernel_size);
   private_nh.getParam("angle_between_scanpoints", m_angle_between_scanpoints_launch);

   m_certainty_threshold.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));
   m_dist_weight.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));
   m_intensity_weight.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));
   m_weight_for_small_intensities.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));

   m_object_size.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));
   m_distance_to_comparison_points.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));

   m_median_min_dist.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));
   m_median_thresh1_dist.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));
   m_median_thresh2_dist.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));
   m_median_max_dist.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));

   m_max_dist_for_median_computation.setCallback(boost::bind(&Segmenter::changeParameterSavely, this));
}

void Segmenter::changeParameterSavely()
{
   boost::mutex::scoped_lock lock(m_parameter_change_lock);
   ROS_DEBUG("Segmenter: New parameter");
   if(m_publish_debug_clouds)
      plot();
}

void Segmenter::initBuffer(int number_of_rings)
{
  for(int i = 0; i < number_of_rings; i++)
  {
    BufferMediansPtr median_filtered_circ_buffer(new BufferMedians(m_circular_buffer_capacity_launch));
    m_median_filtered_circ_buffer_vector.push_back(median_filtered_circ_buffer);
  }

  m_median_iters_by_ring.resize(number_of_rings);
  m_segmentation_iters_by_ring.resize(number_of_rings);

  m_buffer_initialized = true;
}

void Segmenter::resetBuffer()
{
  for(int ring = 0; ring < (int)m_median_filtered_circ_buffer_vector.size(); ring++)
  {
    m_median_filtered_circ_buffer_vector.at(ring)->clear();
    m_median_iters_by_ring.at(ring).reset();
    m_segmentation_iters_by_ring.at(ring).reset();
  }
}

void Segmenter::hokuyoCallback(const sensor_msgs::LaserScanConstPtr& input_scan)
{
  if(m_pub_obstacle_cloud.getNumSubscribers() == 0 &&
     m_pub_debug_obstacle_cloud.getNumSubscribers() == 0 &&
     m_pub_filtered_cloud.getNumSubscribers() == 0)
  {
    ROS_DEBUG_STREAM("Segmenter::hokuyoCallback: No subscriber to laser_segmentation. Resetting buffer.");
    resetBuffer();
    return;
  }

  if(!m_buffer_initialized)
    initBuffer(HOKUYO_NUM_RINGS);
  
  boost::mutex::scoped_lock lock(m_parameter_change_lock);

  sensor_msgs::PointCloud2 cloud;
  InputPointCloud::Ptr cloud_transformed(new InputPointCloud());

  std::string frame_id = "base_link";

  if (!m_tf_listener.waitForTransform(frame_id, input_scan->header.frame_id, input_scan->header.stamp + ros::Duration().fromSec((input_scan->ranges.size()) * input_scan->time_increment), ros::Duration(0.1)))
  {
    ROS_ERROR_THROTTLE(10.0, "Segmenter::hokuyoCallback: Could not wait for transform.");
    return;
  }

  // transform 2D scan line to 3D point cloud
  try
  {
    m_scan_projector.transformLaserScanToPointCloud(frame_id, *input_scan, cloud, m_tf_listener, 35.f,
                                                    (laser_geometry::channel_option::Intensity | laser_geometry::channel_option::Distance));

    // fix fields.count member
    for (unsigned int i = 0; i < cloud.fields.size(); i++)
      cloud.fields[i].count = 1;

    sensor_msgs::PointCloud2Iterator<float> iter_x(cloud, "x");
    sensor_msgs::PointCloud2Iterator<float> iter_y(cloud, "y");
    sensor_msgs::PointCloud2Iterator<float> iter_z(cloud, "z");
    sensor_msgs::PointCloud2Iterator<float> iter_intensity(cloud, "intensity");
    sensor_msgs::PointCloud2Iterator<float> iter_distance(cloud, "distances");

    cloud_transformed->points.reserve(cloud.height * cloud.width);
    for(; iter_x != iter_x.end(); ++iter_x, ++iter_y, ++iter_z, ++iter_intensity, ++iter_distance)
    {
      if(std::isnan(*iter_x) || std::isnan(*iter_y) || std::isnan(*iter_z))
        continue;

      InputPoint point;
      point.x = *iter_x;
      point.y = *iter_y;
      point.z = *iter_z;
      point.intensity = *iter_intensity;
      point.distance = *iter_distance;
      point.ring = 0;

      m_median_filtered_circ_buffer_vector[point.ring]->push_back(point);
    }
  }
  catch (tf::TransformException& exc)
  {
    ROS_ERROR_THROTTLE(10.0, "Segmenter::hokuyoCallback: No transform found.");
    ROS_ERROR_THROTTLE(10.0, "message: '%s'", exc.what());
  }

  processScan(pcl_conversions::toPCL(cloud.header));
}

void Segmenter::velodyneCallback(const InputPointCloud::ConstPtr &input_cloud)
{
  if(m_pub_obstacle_cloud.getNumSubscribers() == 0 &&
     m_pub_debug_obstacle_cloud.getNumSubscribers() == 0 &&
     m_pub_filtered_cloud.getNumSubscribers() == 0)
  {
    ROS_DEBUG_STREAM("Segmenter::velodyneCallback: No subscriber to laser_segmentation. Resetting buffer");
    resetBuffer();
    return;
  }

  if(!m_buffer_initialized)
    initBuffer(PUCK_NUM_RINGS);
  
  boost::mutex::scoped_lock lock(m_parameter_change_lock);

  for(const auto& point : input_cloud->points)
  {
    m_median_filtered_circ_buffer_vector[point.ring]->push_back(point);
  }
  
  processScan(input_cloud->header);
}

void Segmenter::processScan(pcl::PCLHeader header)
{
   pcl::StopWatch timer;

   OutputPointCloud::Ptr obstacle_cloud (new OutputPointCloud);
   obstacle_cloud->header = header;

   DebugOutputPointCloud::Ptr debug_obstacle_cloud (new DebugOutputPointCloud);
   debug_obstacle_cloud->header = header;

   DebugOutputPointCloud::Ptr filtered_cloud (new DebugOutputPointCloud);
   filtered_cloud->header = header;
  
   for(auto ring = 0; ring < (int)m_median_filtered_circ_buffer_vector.size(); ++ring)
   {
     // initialize member iterators
     if(!m_median_filtered_circ_buffer_vector.at(ring)->empty() && !m_median_iters_by_ring[ring])
     {
       m_median_iters_by_ring[ring] = m_median_filtered_circ_buffer_vector.at(ring)->begin();
     }

     if(!m_median_filtered_circ_buffer_vector.at(ring)->empty() && !m_segmentation_iters_by_ring[ring])
     {
       m_segmentation_iters_by_ring[ring] = m_median_filtered_circ_buffer_vector.at(ring)->begin();
     }
     
     if(m_median_iters_by_ring[ring])
     {
       filterRing(m_median_filtered_circ_buffer_vector.at(ring), *m_median_iters_by_ring.at(ring));
     }

     if(m_segmentation_iters_by_ring[ring])
     {
	      segmentRing(m_median_filtered_circ_buffer_vector.at(ring),
                        *m_segmentation_iters_by_ring.at(ring),
                        *m_median_iters_by_ring.at(ring),
                        obstacle_cloud,
                        debug_obstacle_cloud);
     }
//      ROS_INFO_STREAM("ring : " << ring << " " << m_median_filtered_circ_buffer_vector.at(ring)->size() << " points: " << obstacle_cloud->points.size());
   }

   ROS_DEBUG_STREAM("Segmenter::processScan: Time needed to segment one cloud in ms : " << timer.getTime());

   if(m_publish_debug_clouds)
   {
      fillFilteredCloud(debug_obstacle_cloud, filtered_cloud);
      m_pub_filtered_cloud.publish(filtered_cloud);
      m_pub_debug_obstacle_cloud.publish(debug_obstacle_cloud);
   }

   m_pub_obstacle_cloud.publish(obstacle_cloud);
}

void Segmenter::calcMedianFromBuffer(const int noise_filter_kernel_size,
                                    const int object_filter_kernel_size,
                                    const BufferMediansPtr& buffer,
                                    const median_const_iterator& current_element,
                                    std::function<float(Segmenter::InputPoint)> f,
                                    float max_dist_for_median_computation,
                                    float& noise_filter_result, 
                                    float& object_filter_result) const
{
  assert(std::distance(buffer->begin(), buffer->end()) > object_filter_kernel_size);

  const int noise_filter_kernel_size_half = noise_filter_kernel_size / 2;
  const int object_filter_kernel_size_half = object_filter_kernel_size / 2;

  median_const_iterator noise_kernel_start = current_element - noise_filter_kernel_size_half;
  median_const_iterator noise_kernel_end = current_element + noise_filter_kernel_size_half;
  median_const_iterator object_kernel_start = current_element - object_filter_kernel_size_half;
  median_const_iterator object_kernel_end = current_element + object_filter_kernel_size_half;
  long int noise_kernel_start_offset = -1;
  long int noise_kernel_end_offset = -1;

  // get distances of neighbors
  std::vector<float> neighborhood_values;
  neighborhood_values.reserve(object_filter_kernel_size);

  // if max_dist_for_median_computation threshold is zero, take all neighbors within kernel bounds for filtering
  // else use only those neighbors whose distance difference to the current point is below the threshold
  if(max_dist_for_median_computation == 0.f)
  {
    median_const_iterator it_tmp = object_kernel_start;
    noise_kernel_start_offset = std::distance(object_kernel_start, noise_kernel_start);
    noise_kernel_end_offset = std::distance(object_kernel_start, noise_kernel_end);
    // use advance to cast const
    while(it_tmp <= object_kernel_end)
      neighborhood_values.push_back(f((*it_tmp++).point));
  }
  else
  {
    // save distance of midpoint in the buffer aka the current point we are looking at
    const float distance_of_current_point = f((*current_element).point);

    median_const_iterator it_tmp = object_kernel_start;
    int counter = 0;
    while(it_tmp <= object_kernel_end)
    {
      const float val_tmp = f((*it_tmp).point);
      const float abs_distance_difference_to_current_point = fabsf(distance_of_current_point - val_tmp);

      // check for each point in the buffer if it exceeds the distance threshold to the current point
      if(abs_distance_difference_to_current_point < max_dist_for_median_computation)
      {
        neighborhood_values.push_back(val_tmp);
        if(it_tmp >= noise_kernel_start && it_tmp <= noise_kernel_end)
        {
          if(noise_kernel_start_offset < 0)
            noise_kernel_start_offset = counter;

          noise_kernel_end_offset = counter;
        }
      }
      counter++;
      ++it_tmp;
    }
  }

  // get median of neighborhood distances within range of noise kernel
  long int noise_kernel_middle_offset = (noise_kernel_end_offset + noise_kernel_start_offset) / 2;
  std::nth_element(neighborhood_values.begin() + noise_kernel_start_offset,
                   neighborhood_values.begin() + noise_kernel_middle_offset,
                   neighborhood_values.begin() + noise_kernel_end_offset + 1);

  noise_filter_result = neighborhood_values[noise_kernel_middle_offset];

  // get median of neighborhood distances within range of object kernel
  std::nth_element(neighborhood_values.begin(), neighborhood_values.begin() + neighborhood_values.size() / 2, neighborhood_values.end());

  object_filter_result = neighborhood_values[neighborhood_values.size() / 2];
}

void Segmenter::filterRing(std::shared_ptr<boost::circular_buffer<MedianFiltered> > buffer_median_filtered,
                          median_iterator& iter)
{
   while(!buffer_median_filtered->empty() && iter != buffer_median_filtered->end())
   {
      // compute the kernel size in number of points corresponding to the desired object size
      // in other words, compute how many points are approximately on the object itself
      float alpha = static_cast<float>(std::atan((m_object_size()/2.f)/(*iter).point.distance) * (180.0 / M_PI));
      int object_size_in_points = (int)std::floor(alpha * 2.f / m_angle_between_scanpoints_launch);
      // the kernel size at which the target object gets filtered out is when there are more 
      // non-object points than object points in the median filter kernel. To stay slightly below
      // this border we floor in the computation above and double the result. This way only noise 
      // and objects smaller than the target objects get filtered out.
      int noise_filter_kernel_size = object_size_in_points * 2;

      if(noise_filter_kernel_size < 0)
         ROS_ERROR("Segmenter::filterRing: Kernel size negative.");

      noise_filter_kernel_size = std::max(noise_filter_kernel_size, 1);
      noise_filter_kernel_size = std::min(noise_filter_kernel_size, m_max_kernel_size);

      int object_filter_kernel_size = (int)std::ceil(noise_filter_kernel_size * m_kernel_size_diff_factor());
      object_filter_kernel_size = std::max(object_filter_kernel_size, 2);

      const int object_filter_kernel_size_half = object_filter_kernel_size / 2;

      if(std::distance(buffer_median_filtered->begin(), iter) >= object_filter_kernel_size_half && std::distance(iter, buffer_median_filtered->end()) > object_filter_kernel_size_half)
      {
         if(m_dist_weight() != 0.f)
            calcMedianFromBuffer(noise_filter_kernel_size,
                                 object_filter_kernel_size,
                                 buffer_median_filtered,
                                 median_const_iterator(iter),
                                 [&](const InputPoint &fn) -> float { return fn.distance; },
                                 m_max_dist_for_median_computation(),
                                 (*iter).dist_noise_kernel,
                                 (*iter).dist_object_kernel);

         if(m_intensity_weight() != 0.f)
            calcMedianFromBuffer(noise_filter_kernel_size,
                                 object_filter_kernel_size,
                                 buffer_median_filtered,
                                 median_const_iterator(iter),
                                 [&](const InputPoint &fn) -> float { return fn.intensity; },
                                 0.f,
                                 (*iter).intens_noise_kernel,
                                 (*iter).intens_object_kernel);
      }

      // if there are not enough neighboring points left in the circular to filter -> break  
      if(std::distance(iter, buffer_median_filtered->end()) <= object_filter_kernel_size_half)
         break;

      ++iter;
   }
}

float Segmenter::computeSegmentationProbability(float distance_delta, float intensity_delta)
{
   float certainty_value = 0.f;
   // cap absolute difference to 0 - m_max_intensity_range
   // and do some kind of weighting, bigger weight -> bigger weight for smaller intensity differences
   intensity_delta = std::max(0.f, intensity_delta);
   intensity_delta = std::min(intensity_delta, m_max_intensity_range/m_weight_for_small_intensities());
   intensity_delta *= m_weight_for_small_intensities();

//   ROS_INFO_STREAM("intensity_delta inner " << (intensity_delta * m_intensity_weight()));


   if(distance_delta < m_median_min_dist() || distance_delta > m_median_max_dist())
   {
      return 0.f;
   }
   else
   {
      // TODO: adapt the term in the first if statement to the one that was intended in the first place
      if(distance_delta >= m_median_min_dist() && distance_delta < m_median_thresh1_dist())
      {
         certainty_value = distance_delta * m_dist_weight() * (m_max_prob_by_distance/m_median_thresh1_dist()) + intensity_delta * (m_intensity_weight()/m_max_intensity_range);
      }
      if(distance_delta >= m_median_thresh1_dist() && distance_delta < m_median_thresh2_dist())
      {
         certainty_value = m_dist_weight() * m_max_prob_by_distance + intensity_delta * (m_intensity_weight()/m_max_intensity_range);
      }
      if(distance_delta >= m_median_thresh2_dist() && distance_delta < m_median_max_dist())
      {
         certainty_value = (m_max_prob_by_distance / (m_median_max_dist() - m_median_thresh2_dist())) * ((m_median_max_dist() - distance_delta) * m_dist_weight()) + intensity_delta * (m_intensity_weight()/m_max_intensity_range);
      }
   }
   certainty_value = std::min(certainty_value, 1.0f);
   certainty_value = std::max(certainty_value, 0.0f);

   return certainty_value;
}

void Segmenter::segmentRing(std::shared_ptr<boost::circular_buffer<MedianFiltered> > buffer_median_filtered,
                               median_iterator& median_it,
                               median_iterator& end,
                               OutputPointCloud::Ptr obstacle_cloud,
                               DebugOutputPointCloud::Ptr debug_obstacle_cloud)

{
   // iterator to the first element in the buffer where a computation of the median value was not possible
   // so to say the .end() of median values in the buffer
   median_iterator end_of_values = end + 1;

   float alpha = static_cast<float>(std::atan(m_distance_to_comparison_points()/(*median_it).dist_noise_kernel) * 180.f / M_PI);
   int dist_to_comparsion_point = (int)std::round(alpha / m_angle_between_scanpoints_launch);

   dist_to_comparsion_point = std::max(dist_to_comparsion_point, 0);
   dist_to_comparsion_point = std::min(dist_to_comparsion_point, m_max_kernel_size);

   median_iterator::difference_type dist_to_comparsion_point_bounded = dist_to_comparsion_point;

   if(std::distance( buffer_median_filtered->begin(), end_of_values) <= 2*dist_to_comparsion_point_bounded+1 ||
       std::distance( median_it, end_of_values) <= dist_to_comparsion_point_bounded + 1)
   {
      ROS_WARN("Segmenter::segmentRing: Not enough medians in buffer.");
      return;
   }
   
   for(; median_it != end_of_values; ++median_it)
   {
      // compute index of neighbors to compare to
      alpha = static_cast<float>(std::atan(m_distance_to_comparison_points()/(*median_it).dist_noise_kernel) * 180.f / M_PI);
      dist_to_comparsion_point = (int)std::round(alpha / m_angle_between_scanpoints_launch);

      dist_to_comparsion_point = std::max(dist_to_comparsion_point, 0);
      dist_to_comparsion_point = std::min(dist_to_comparsion_point, m_max_kernel_size / 2);

      dist_to_comparsion_point_bounded = dist_to_comparsion_point;

      if(std::distance(median_it, end_of_values) <= dist_to_comparsion_point_bounded + 1)
         break;

      auto window_start = median_it;
      // handle special case for first points in buffer
      if(std::distance(buffer_median_filtered->begin(), median_it) > dist_to_comparsion_point_bounded)
         window_start -= dist_to_comparsion_point_bounded;
      else
         window_start = buffer_median_filtered->begin();

      auto window_end = median_it + dist_to_comparsion_point_bounded;

      // TODO: check sum and max terms if necessary
      // compute differences and resulting certainty value
      float distance_delta = 0.f;
      if(m_dist_weight() != 0.f)
      {
         float difference_distance_start = (*median_it).dist_noise_kernel - (*window_start).dist_object_kernel;
         float difference_distance_end = (*median_it).dist_noise_kernel - (*window_end).dist_object_kernel;

         float difference_distance_sum = difference_distance_start + difference_distance_end;
         float difference_distance_max = std::max(difference_distance_start, difference_distance_end);
         distance_delta = std::max(difference_distance_sum, difference_distance_max);
      }


      float intensity_delta = 0.f;
      if(m_intensity_weight() != 0.f)
      {
         float difference_intensities_start = (*median_it).intens_noise_kernel - (*window_start).intens_object_kernel;
         float difference_intensities_end = (*median_it).intens_noise_kernel - (*window_end).intens_object_kernel;

         float difference_intensities_sum = difference_intensities_start + difference_intensities_end;
         float difference_intensities_min = std::min(difference_intensities_start, difference_intensities_end);
         intensity_delta = std::min(difference_intensities_sum, difference_intensities_min);
      }

      // TODO: change certainty computation to the terms from the expose
      float certainty_value = computeSegmentationProbability(-distance_delta, intensity_delta);

      const auto& current_point = (*median_it).point;

      OutputPoint output_point;
      output_point.x = current_point.x;
      output_point.y = current_point.y;
      output_point.z = current_point.z;
      output_point.segment = (certainty_value < m_certainty_threshold()) ? 0 : 1;
      obstacle_cloud->push_back(output_point);

      if(m_publish_debug_clouds)
      {
        DebugOutputPoint debug_output_point;

        debug_output_point.x = current_point.x;
        debug_output_point.y = current_point.y;
        debug_output_point.z = current_point.z;
        debug_output_point.intensity = current_point.intensity;
        debug_output_point.ring = current_point.ring;

        debug_output_point.segmentation_distance = distance_delta;
        debug_output_point.segmentation_intensity = intensity_delta;
        debug_output_point.segmentation = certainty_value;

        debug_obstacle_cloud->push_back(debug_output_point);

        // save factors for median filtered cloud
        float factor = 1.f;
        if(!std::isnan((*median_it).dist_noise_kernel) && !std::isnan(current_point.distance))
           factor = (*median_it).dist_noise_kernel / current_point.distance;

        m_filtering_factors.push_back(factor);
      }

   }
}

void Segmenter::fillFilteredCloud(const DebugOutputPointCloud::ConstPtr &cloud,
                                 DebugOutputPointCloud::Ptr filtered_cloud)
{
   if(cloud->size() != m_filtering_factors.size())
   {
      m_filtering_factors.clear();
      ROS_ERROR("Segmenter::fillFilteredCloud: Cloud and factors have different sizes.");
      return;
   }

   std::string laser_frame_id = "/velodyne";
   if(!m_input_is_velodyne)
     laser_frame_id = "/laser_scanner_center";

   tf::StampedTransform laser_link_transform;
   bool transform_found = true;
   try
   {
      m_tf_listener.lookupTransform(laser_frame_id, cloud->header.frame_id, pcl_conversions::fromPCL(cloud->header.stamp), laser_link_transform);
   }
   catch(tf::TransformException& ex)
   {
      ROS_ERROR("Segmenter::fillFilteredCloud: Transform unavailable %s", ex.what());
      transform_found = false;
   }

   if(transform_found)
   {
      Eigen::Affine3d laser_link_transform_eigen;
      tf::transformTFToEigen(laser_link_transform, laser_link_transform_eigen);

      DebugOutputPointCloud::Ptr cloud_transformed(new DebugOutputPointCloud);
      pcl::transformPointCloud(*cloud, *cloud_transformed, laser_link_transform_eigen);

      filtered_cloud->header.frame_id = laser_frame_id;

      // move the cloud points to the place they would have been if the median filter would have been applied to them
      for(int point_index = 0; point_index < (int)cloud_transformed->size(); point_index++)
      {
         DebugOutputPoint output_point;
         output_point.x = cloud_transformed->points[point_index].x * m_filtering_factors[point_index];
         output_point.y = cloud_transformed->points[point_index].y * m_filtering_factors[point_index];
         output_point.z = cloud_transformed->points[point_index].z * m_filtering_factors[point_index];
         output_point.segmentation = cloud_transformed->points[point_index].segmentation;
         output_point.ring = cloud_transformed->points[point_index].ring;

         filtered_cloud->push_back(output_point);
      }
   }
   m_filtering_factors.clear();
}

void Segmenter::plot()
{
   // set up x-axis
   double epsilon = 0.00000001;
   const int range = 8;
   std::vector<double> xAxis(range, 0.0);
   xAxis[0] = 0.0;
   xAxis[1] = m_median_min_dist();
   xAxis[2] = m_median_min_dist() + epsilon;
   xAxis[3] = m_median_thresh1_dist();
   xAxis[4] = m_median_thresh2_dist();
   xAxis[5] = m_median_max_dist();
   xAxis[6] = m_median_max_dist() + epsilon;
   xAxis[7] = m_median_max_dist() + 0.5;

   std::vector<double> constant_one(range, 0.0);
   for(int i = 0; i < range; i++) constant_one[i] = 1.0 + epsilon;

   std::vector<double> distance_proportion(range, 0.0);
   distance_proportion[0] = 0.0;
   distance_proportion[1] = 0.0;
   distance_proportion[2] = 0.0;
   distance_proportion[3] = m_max_prob_by_distance * m_dist_weight();
   distance_proportion[4] = m_max_prob_by_distance * m_dist_weight();
   distance_proportion[5] = 0.0;
   distance_proportion[6] = 0.0;
   distance_proportion[7] = 0.0;

   std::vector<double> intensity_proportion(range, 0.0);
   intensity_proportion[0] = 0.0;
   intensity_proportion[1] = 0.0;
   intensity_proportion[2] = m_max_intensity_range * (m_intensity_weight()/100.f);
   intensity_proportion[3] = m_max_prob_by_distance * m_dist_weight() + m_max_intensity_range * (m_intensity_weight()/100.f);
   intensity_proportion[4] = m_max_prob_by_distance * m_dist_weight() + m_max_intensity_range * (m_intensity_weight()/100.f);
   intensity_proportion[5] = m_max_intensity_range * (m_intensity_weight()/100.f);
   intensity_proportion[6] = 0.0;
   intensity_proportion[7] = 0.0;

   // add histograms to plotter
   std::vector<char> black{0, 0, 0, (char) 255};
   std::vector<char> red{(char) 255, 0, 0, (char) 255};
   std::vector<char> green{0, (char) 255, 0, (char) 255};

   m_plotter->clearPlots();

   m_plotter->setYRange(0.0, std::max(1.1, intensity_proportion[3]));
   m_plotter->addPlotData(xAxis, distance_proportion, "Distance proportion", vtkChart::LINE, red);
   m_plotter->addPlotData(xAxis, intensity_proportion, "Intensity proportion", vtkChart::LINE, green);
   m_plotter->addPlotData(xAxis, constant_one, "One", vtkChart::LINE, black);

   m_plotter->spinOnce(0);
}

} // namespace laser_segmentation
