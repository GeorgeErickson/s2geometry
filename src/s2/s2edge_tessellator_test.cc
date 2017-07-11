// Copyright 2017 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// Author: ericv@google.com (Eric Veach)

#include "s2/s2edge_tessellator.h"

#include <gtest/gtest.h>
#include "s2/s2edgeutil.h"
#include "s2/s2projections.h"
#include "s2/s2testing.h"

using std::fabs;
using std::vector;

namespace {

class DistStats {
 public:
  DistStats() : min_dist_(S1Angle::Infinity()),
                max_dist_(S1Angle::Zero()),
                sum_dist_(S1Angle::Zero()),
                count_(0) {
  }
  void Tally(S1Angle dist) {
    min_dist_ = std::min(dist, min_dist_);
    max_dist_ = std::max(dist, max_dist_);
    sum_dist_ += dist;
    count_ += 1;
  }
  S1Angle min_dist() const { return min_dist_; }
  S1Angle max_dist() const { return max_dist_; }
  S1Angle avg_dist() const { return sum_dist_ / count_; }

 private:
  S1Angle min_dist_, max_dist_, sum_dist_;
  int count_;
};

S1Angle GetMaxDistance(S2::Projection const& proj,
                       R2Point const& px, S2Point const& x,
                       R2Point const& py, S2Point const& y) {
  // Step along the projected edge at a fine resolution and keep track of the
  // maximum distance of any point to the current geodesic edge.
  int const kNumSteps = 100;
  S1ChordAngle max_dist = S1ChordAngle::Zero();
  for (double f = 0.5 / kNumSteps; f < 1.0; f += 1.0 / kNumSteps) {
    S1ChordAngle dist = S1ChordAngle::Infinity();
    S2Point p = proj.Unproject(proj.Interpolate(f, px, py));
    S2::UpdateMinDistance(p, x, y, &dist);
    if (dist > max_dist) max_dist = dist;
  }
  return max_dist.ToAngle();
}

DistStats TestUnprojected(S2::Projection const& proj, S1Angle tolerance,
                          R2Point const& pa, R2Point const& pb) {
  S2EdgeTessellator tess(&proj, tolerance);
  vector<S2Point> vertices;
  tess.AppendUnprojected(pa, pb, &vertices);
  EXPECT_TRUE(S2::ApproxEquals(proj.Unproject(pa), vertices.front()));
  EXPECT_TRUE(S2::ApproxEquals(proj.Unproject(pb), vertices.back()));
  DistStats stats;
  if (pa == pb) {
    EXPECT_EQ(1, vertices.size());
    return stats;
  }
  // Precompute the normal to the projected edge.
  Vector2_d norm = (pb - pa).Ortho().Normalize();
  S2Point x = vertices[0];
  R2Point px = proj.Project(x);
  for (int i = 1; i < vertices.size(); ++i) {
    S2Point y = vertices[i];
    R2Point py = proj.Project(y);
    // Check that every vertex is on the projected edge.
    EXPECT_LT((py - pa).DotProd(norm), 1e-14 * py.Norm());
    stats.Tally(GetMaxDistance(proj, px, x, py, y));
    x = y;
    px = py;
  }
  LOG(INFO) << vertices.size() << " vertices, min/avg/max tolerance ratio = "
            << (stats.min_dist() / tolerance) << " / "
            << (stats.avg_dist() / tolerance) << " / "
            << (stats.max_dist() / tolerance);
  return stats;
}

DistStats TestProjected(S2::Projection const& proj, S1Angle tolerance,
                        S2Point const& a, S2Point const& b) {
  S2EdgeTessellator tess(&proj, tolerance);
  vector<R2Point> vertices;
  tess.AppendProjected(a, b, &vertices);
  EXPECT_TRUE(S2::ApproxEquals(a, proj.Unproject(vertices.front())));
  EXPECT_TRUE(S2::ApproxEquals(b, proj.Unproject(vertices.back())));
  DistStats stats;
  if (a == b) {
    EXPECT_EQ(1, vertices.size());
    return stats;
  }
  R2Point px = vertices[0];
  S2Point x = proj.Unproject(px);
  for (int i = 1; i < vertices.size(); ++i) {
    R2Point py = vertices[i];
    S2Point y = proj.Unproject(py);
    // Check that every vertex is on the geodesic edge.
    static S1ChordAngle kMaxInterpolationError(S1Angle::Radians(1e-14));
    EXPECT_TRUE(S2::IsDistanceLess(y, a, b, kMaxInterpolationError));
    stats.Tally(GetMaxDistance(proj, px, x, py, y));
    x = y;
    px = py;
  }
  LOG(INFO) << vertices.size() << " vertices, min/avg/max tolerance ratio = "
            << (stats.min_dist() / tolerance) << " / "
            << (stats.avg_dist() / tolerance) << " / "
            << (stats.max_dist() / tolerance);
  return stats;
}

TEST(S2EdgeTessellator, ProjectedNoTessellation) {
  S2::PlateCarreeProjection proj(180);
  S2EdgeTessellator tess(&proj, S1Angle::Degrees(0.01));
  vector<R2Point> vertices;
  tess.AppendProjected(S2Point(1, 0, 0), S2Point(0, 1, 0), &vertices);
  EXPECT_EQ(2, vertices.size());
}

TEST(S2EdgeTessellator, UnprojectedNoTessellation) {
  S2::PlateCarreeProjection proj(180);
  S2EdgeTessellator tess(&proj, S1Angle::Degrees(0.01));
  vector<S2Point> vertices;
  tess.AppendUnprojected(R2Point(0, 30), R2Point(0, 50), &vertices);
  EXPECT_EQ(2, vertices.size());
}

TEST(S2EdgeTessellator, WrapUnprojected) {
  // This tests that a projected edge that crosses the 180 degree meridian
  // goes the "short way" around the sphere.

  S2::PlateCarreeProjection proj(180);
  S2EdgeTessellator tess(&proj, S1Angle::Degrees(0.01));
  vector<S2Point> vertices;
  tess.AppendUnprojected(R2Point(-170, 0), R2Point(170, 80), &vertices);
  for (auto const& v : vertices) {
    EXPECT_GE(fabs(S2LatLng::Longitude(v).degrees()), 170);
  }
}

TEST(S2EdgeTessellator, ProjectedWrapping) {
  // This tests projecting a geodesic edge that crosses the 180 degree
  // meridian.  This results in a set of vertices that may be non-canonical
  // (i.e., absolute longitudes greater than 180 degrees) but that don't have
  // any sudden jumps in value, which is convenient for interpolating them.
  S2::PlateCarreeProjection proj(180);
  S2EdgeTessellator tess(&proj, S1Angle::Degrees(0.01));
  vector<R2Point> vertices;
  tess.AppendProjected(S2LatLng::FromDegrees(0, -170).ToPoint(),
                       S2LatLng::FromDegrees(0, 170).ToPoint(), &vertices);
  for (auto const& v : vertices) {
    EXPECT_LE(v.x(), -170);
  }
}

TEST(S2EdgeTessellator, AppendUnprojected) {
  S2::MercatorProjection proj(180);
  S1Angle tolerance(S1Angle::Degrees(1e-5));
  R2Point pa(0, 0), pb(89.999999, 179);
  DistStats stats = TestUnprojected(proj, tolerance, pa, pb);
  EXPECT_LT(stats.max_dist(), tolerance);
}

TEST(S2EdgeTessellator, AppendProjected) {
  S2::PlateCarreeProjection proj(180);
  S1Angle tolerance(S1Angle::E7(1));
  S2Point a = S2LatLng::FromDegrees(-89.999, -170).ToPoint();
  S2Point b = S2LatLng::FromDegrees(50, 100).ToPoint();
  DistStats stats = TestProjected(proj, tolerance, a, b);
  EXPECT_LT(stats.max_dist(), tolerance);
}

TEST(S2EdgeTessellator, SeattleToNewYork) {
  S2::PlateCarreeProjection proj(180);
  S1Angle tolerance = S2Testing::MetersToAngle(1.23);
  S2Point seattle(S2LatLng::FromDegrees(47.6062, -122.3321).ToPoint());
  S2Point newyork(S2LatLng::FromDegrees(40.7128, -74.0059).ToPoint());
  DistStats stats = TestProjected(proj, tolerance, seattle, newyork);
  EXPECT_LT(stats.max_dist(), tolerance);
}

}  // namespace
