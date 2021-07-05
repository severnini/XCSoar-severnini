/* Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2021 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
 */

#include "GrahamScan.hpp"
#include "Geo/SearchPointVector.hpp"

#include <list>

[[gnu::const]]
static int
Sign(double value, double tolerance)
{
  if (value > tolerance)
    return 1;
  if (value < -tolerance)
    return -1;

  return 0;
}

static int
Direction(const GeoPoint &p0, const GeoPoint &p1, const GeoPoint &p2,
          double tolerance)
{
  //
  // In this program we frequently want to look at three consecutive
  // points, p0, p1, and p2, and determine whether p2 has taken a turn
  // to the left or a turn to the right.
  //
  // We can do this by by translating the points so that p1 is at the origin,
  // then taking the cross product of p0 and p2. The result will be positive,
  // negative, or 0, meaning respectively that p2 has turned right, left, or
  // is on a straight line.
  //

  const auto delta_a = p0 - p1;
  const auto delta_b = p2 - p1;

  const auto a = delta_a.longitude.Native() * delta_b.latitude.Native();
  const auto b = delta_b.longitude.Native() * delta_a.latitude.Native();

  if (tolerance < 0)
    /* auto-tolerance - this has been verified by experiment */
    tolerance = std::max(fabs(a), fabs(b)) / 10;

  return Sign(a - b, tolerance);
}

GrahamScan::GrahamScan(SearchPointVector &sps, double sign_tolerance)
  :raw_vector(sps), size(sps.size()),
   tolerance(sign_tolerance)
{
}

void
GrahamScan::PartitionPoints()
{
  //
  // The initial array of points is stored in vector raw_points. I first
  // sort it, which gives me the far left and far right points of the
  // hull.  These are special values, and they are stored off separately
  // in the left and right members.
  //
  // I then go through the list of raw_points, and one by one determine
  // whether each point is above or below the line formed by the right
  // and left points.  If it is above, the point is moved into the
  // upper_partition_points sequence. If it is below, the point is moved
  // into the lower_partition_points sequence. So the output of this
  // routine is the left and right points, and the sorted points that
  // are in the upper and lower partitions.
  //

  //
  // Step one in partitioning the points is to sort the raw data
  //
  std::list<SearchPoint> raw_points{raw_vector.begin(), raw_vector.end()};
  raw_points.sort([](const SearchPoint &sp1, const SearchPoint &sp2){
    const auto &gp1 = sp1.GetLocation();
    const auto &gp2 = sp2.GetLocation();
    if (gp1.longitude < gp2.longitude)
      return true;
    else if (gp1.longitude == gp2.longitude)
      return gp1.latitude < gp2.latitude;
    else
      return false;
  });

  //
  // The the far left and far right points, remove them from the
  // sorted sequence and store them in special members
  //

  left = raw_points.front();
  right = raw_points.back();

  const auto begin = std::next(raw_points.cbegin());
  const auto end = std::prev(raw_points.cend());

  //
  // Now put the remaining points in one of the two output sequences
  //

  GeoPoint loclast = left.GetLocation();

  upper_partition_points.reserve(size);
  lower_partition_points.reserve(size);

  for (auto j = begin; j != end; ++j) {
    const auto &i = *j;

    if (loclast.longitude != i.GetLocation().longitude ||
        loclast.latitude != i.GetLocation().latitude) {
      loclast = i.GetLocation();

      int dir = Direction(left.GetLocation(), right.GetLocation(),
                          i.GetLocation(), tolerance);
      if (dir < 0)
        upper_partition_points.push_back(i);
      else
        lower_partition_points.push_back(i);
    }
  };

}

bool
GrahamScan::BuildHull()
{
  //
  // Building the hull consists of two procedures: building the lower
  // and then the upper hull. The two procedures are nearly identical -
  // the main difference between the two is the test for convexity. When
  // building the upper hull, our rull is that the middle point must
  // always be *above* the line formed by its two closest
  // neighbors. When building the lower hull, the rule is that point
  // must be *below* its two closest neighbors. We pass this information
  // to the building routine as the last parameter, which is either -1
  // or 1.
  //

  bool lower_pruned = BuildHalfHull(std::move(lower_partition_points),
                                    lower_hull, 1);
  bool upper_pruned = BuildHalfHull(std::move(upper_partition_points),
                                    upper_hull, -1);

  return lower_pruned || upper_pruned;
}

bool
GrahamScan::BuildHalfHull(std::vector<SearchPoint> &&input,
                          std::vector<SearchPoint> &output, int factor)
{
  //
  // This is the method that builds either the upper or the lower half convex
  // hull. It takes as its input a copy of the input array, which will be the
  // sorted list of points in one of the two halfs. It produces as output a list
  // of the points in the corresponding convex hull.
  //
  // The factor should be 1 for the lower hull, and -1 for the upper hull.
  //

  output.reserve(input.size() + 1);

  //
  // The hull will always start with the left point, and end with the
  // right point. According, we start by adding the left point as the
  // first point in the output sequence, and make sure the right point
  // is the last point in the input sequence.
  //
  input.push_back(right);
  output.push_back(left);

  bool pruned = false;

  //
  // The construction loop runs until the input is exhausted
  //
  for (const auto &i : input) {
    //
    // Repeatedly add the leftmost point to the hull, then test to see
    // if a convexity violation has occured. If it has, fix things up
    // by removing the next-to-last point in the output sequence until
    // convexity is restored.
    //
    output.push_back(i);

    while (output.size() >= 3) {
      const auto end = output.size() - 1;

      if (factor * Direction(output[end - 2].GetLocation(),
                             output[end].GetLocation(),
                             output[end - 1].GetLocation(),
                             tolerance) > 0)
        break;

      output.erase(output.begin() + end - 1);
      pruned = true;
    }
  }

  return pruned;
}

bool
GrahamScan::PruneInterior()
{
  if (size < 3) {
    return false;
    // nothing to do
  }

  PartitionPoints();

  if (!BuildHull())
    /* nothing was pruned */
    return false;

  SearchPointVector res;
  res.reserve(size);

  for (unsigned i = 0; i + 1 < lower_hull.size(); i++)
    res.push_back(lower_hull[i]);

  for (unsigned i = upper_hull.size() - 1; i >= 1; i--)
    res.push_back(upper_hull[i]);

  assert(res.size() <= size);
  raw_vector.swap(res);
  return true;
}
