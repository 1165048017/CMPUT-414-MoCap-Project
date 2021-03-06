#include "Library/LerpBlender.hpp"

#include <Vector/Vector.hpp>
#include <Vector/Quat.hpp>

#include <cmath>
#include <cassert>
#include <iostream>

#define INTERP_DIVISOR 4

using namespace Character;
using std::pair;
using std::vector;

namespace Library
{

LerpBlender::LerpBlender(const Motion *f, const Motion *t)
: from(f),
  to(t),
  distance_map(f, t),
  last_frame(0),
  cur_frame(0)
{ 
  n_from_frames = from->frames();
  n_to_frames = to->frames();

  /* For now we're naively interpolating the last quarter of the first animation
   * with the first quarter of the last animation.
   * Since the animations aren't the same length, in practice we'll interpolate
   * n frames where n is one quarter of the number of frames in the shorter
   * animation.  This is probably a bad way to go about things, for a number of
   * reasons which I'm not going to enumerate at the moment.
   * Also: ternary ifs are bad. Do as I say, not as I do. */
  n_interp_frames = n_from_frames < n_to_frames ? 
                    (n_from_frames / INTERP_DIVISOR) : (n_to_frames / INTERP_DIVISOR);

  // TODO: cout message should go somewhere else
  distance_map.calcShortestPath(n_interp_frames);

  global_state.clear();
  velocity_control.clear();

}

LerpBlender::LerpBlender(const LerpBlender &other)
: from(other.from),
  to(other.to),
  global_state(other.global_state),
  velocity_control(other.velocity_control),
  distance_map(other.distance_map),
  last_frame(other.last_frame),
  cur_frame(other.cur_frame),
  n_from_frames(other.n_from_frames),
  n_to_frames(other.n_to_frames),
  n_interp_frames(other.n_interp_frames)
{
}

LerpBlender& LerpBlender::operator= (const LerpBlender &other)
{
  if(this == &other) return *this;

  from = other.from;
  to = other.to;
  distance_map = other.distance_map;
  last_frame = other.last_frame;
  cur_frame = other.cur_frame;
  n_from_frames = other.n_from_frames;
  n_to_frames = other.n_to_frames;
  n_interp_frames = other.n_interp_frames;
  global_state = other.global_state;
  velocity_control = other.velocity_control;

  return *this;
}

LerpBlender LerpBlender::blendFromBlend(const LerpBlender &old, const Motion *m)
{

  // Create a new LerpBlender from the old "to" motion, and the new motion, m
  LerpBlender blender(old.to, m);

  /* Here's the complicated part... frames are specified by locations in the
   * distance map.  To determine which frame we should be on, we need to
   * find the place in our new distance map where the first element of
   * the frame pair (from frame number) matches the second element in
   * the current frame pair of the old distance map (its to frame number).
   * This is pretty inefficient since we have to iterate through the path
   * until we find the right frame.  There's probably a better way to do this,
   * but it might involve redesigning the LerpBlender to be more
   * forward-thinking with regard to blending multiple animations. */

   /****************************************************************************
    * WARNING!
    ****************************************************************************
    * The following code is potentially confusing because "frame" is used in
    * two different ways to refer to different things.  The cur_frame member
    * variable is not actually a frame number but rather an index into the
    * distance map, which gives a pair of frame numbers for the to and from
    * animations.  target_frame, however, is an actual frame number.
    * TODO: Fix this. */

  blender.cur_frame = 0;
  const vector<pair<unsigned int, unsigned int> > &old_path = 
    old.distance_map.getShortestPath();
  unsigned int target_frame = old_path[old.cur_frame].second;
  
  for(vector<pair<unsigned int, unsigned int> >::const_iterator it = 
        old_path.begin();
      it < old_path.end();
      ++it, ++blender.cur_frame)
  {
    if(it->first == target_frame) break;
  }
 
  // Figure out what our last frame would have been
  blender.last_frame = blender.cur_frame - (old.cur_frame - old.last_frame);

  // Copy global state from the old blender so the motion stays in the right
  // position
  blender.global_state = old.global_state;
  
  return blender;
}

void LerpBlender::changeFrame(int delta)
{
  last_frame = cur_frame;
  if(cur_frame + delta >= distance_map.getShortestPath().size())
  {
    if(delta > 0)
    {
      last_frame = 0;
      cur_frame = 0;
      global_state.clear();
    }
    else if(delta < 0)
    {
      cur_frame = distance_map.getShortestPath().size() - 1;
      last_frame = cur_frame + delta;
      global_state.clear();
    }
  }
  else
  {
    cur_frame += delta;
  }
}

void LerpBlender::getPose(Pose &output)
{

  /* Create and initialize (not sure why these are separate steps, very
   * non-idiomatic; herpderp I'm pedantic) poses for the from and to
   * to motions */
  Pose from_pose;
  Pose to_pose;
  from_pose.clear();
  to_pose.clear();

  const pair<unsigned int, unsigned int> frame_pair = 
    distance_map.getShortestPath()[cur_frame];
  from->get_pose(frame_pair.first, from_pose);
  to->get_pose(frame_pair.second, to_pose);

  float interp_value = expf((float) frame_pair.second / n_interp_frames) - 1;
  if(interp_value > 1.0f)
  {
    interp_value = 1.0f;
  }

  float interp_conjugate = 1.0f - interp_value;

  /* Loop through all bones and interpolate their orientation quaternions
   * Note that we asserted above that the two animations have the same number
   * of bones */
  for(unsigned int i = 0; i < from_pose.bone_orientations.size(); ++i)
  {
    from_pose.bone_orientations[i] = slerp(from_pose.bone_orientations[i],
                                           to_pose.bone_orientations[i],
                                           interp_value);
  }

  // Interpolate the root position and orientation
  from_pose.root_orientation = slerp(from_pose.root_orientation,
                                     to_pose.root_orientation,
                                     interp_value);

  // Assign the output frame to be equal to the interpolated from pose.
  // We're not done yet, though; using root positions is unreliable,
  // so instead we'll want to use velocities, which we'll calculate below.
  // For now, set the output x and z positions to 0.
  output = from_pose;
  output.root_position.x = output.root_position.z = 0;

  // Get the next frame pair.  If we're at the end of both animations,
  // we just use the current frame for simplicity, since there obviously
  // can't be any velocity change after the end of the animation anyway.
  const pair<unsigned int, unsigned int> last_pair = 
    distance_map.getShortestPath()[last_frame];

  // Determine velocity in each of the animations
  Pose from_last;
  from_last.clear();
  from->get_pose(last_pair.first, from_last);

  Pose to_last;
  to_last.clear();
  to->get_pose(last_pair.second, to_last);

  // We interpolate the velocity according to the interpolation value
  // calculated above
  velocity_control.desired_velocity = 
    (to_pose.root_position - to_last.root_position) * interp_value +
    (from_pose.root_position - from_last.root_position) * interp_conjugate;

  velocity_control.apply_to(global_state, 1);
  global_state.apply_to(output);

  /*if(frame_pair.second > 0 && frame_pair.first < from->frames())
    isInterpolating = true;
  else
    isInterpolating = false;*/
}  

}
