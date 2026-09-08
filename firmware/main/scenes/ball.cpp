#include "scenes/ball.hpp"

#include <algorithm>
#include <cmath>

namespace p64 {
namespace {

constexpr float kRadius = 4.5f;         // 9 px diameter
constexpr float kGravity = 150.0f;      // px/s^2, downwards (+y)
constexpr float kRestitution = 0.86f;   // energy kept per floor bounce
constexpr float kMinApex = 14.0f;       // px above the floor; below this the ball is re-launched
constexpr float kLaunchApex = 50.0f;    // px above the floor after a re-launch
constexpr float kMaxStep = 0.05f;       // s, guards the integration against long stalls

constexpr int kFloorRow = kHeight - 1;                         // the floor line lives on this row
constexpr float kFloorY = static_cast<float>(kFloorRow);       // top edge of the floor line

constexpr Rgb kBall{255, 110, 0};
constexpr Rgb kFloor{0, 70, 200};
constexpr Rgb kOrigin{255, 255, 255};
constexpr Rgb kAxisX{255, 0, 0};
constexpr Rgb kAxisY{0, 255, 0};

float launch_speed(float apex) { return std::sqrt(2.0f * kGravity * apex); }

}  // namespace

void BallScene::enter(Display &display, Frame &frame) {
  x_ = 16.0f;
  y_ = kRadius + 1.0f;  // start near the top, so the first thing you see is the drop
  vx_ = 11.0f;
  vy_ = 0.0f;
  display.set_brightness(max_brightness());
  draw(frame);
  display.present(frame);
}

bool BallScene::render(Display &, Frame &frame, uint32_t, float dt_s) {
  step(std::min(dt_s, kMaxStep));
  draw(frame);
  return true;
}

void BallScene::step(float dt) {
  vy_ += kGravity * dt;
  x_ += vx_ * dt;
  y_ += vy_ * dt;

  // Floor: bounce, lose some energy, and re-launch when the bounces get too small.
  if (y_ + kRadius > kFloorY) {
    y_ = kFloorY - kRadius;
    vy_ = -vy_ * kRestitution;
    const float apex = (vy_ * vy_) / (2.0f * kGravity);
    if (apex < kMinApex) vy_ = -launch_speed(kLaunchApex);
  }
  // Ceiling (only reachable after a stall): plain reflection.
  if (y_ - kRadius < 0.0f) {
    y_ = kRadius;
    vy_ = -vy_ * kRestitution;
  }
  // Side walls: keep drifting so the ball does not sit on one column.
  if (x_ - kRadius < 0.0f) {
    x_ = kRadius;
    vx_ = std::fabs(vx_);
  } else if (x_ + kRadius > static_cast<float>(kWidth)) {
    x_ = static_cast<float>(kWidth) - kRadius;
    vx_ = -std::fabs(vx_);
  }
}

void BallScene::draw(Frame &frame) const {
  frame.clear();
  frame.fill_rect(0, kFloorRow, kWidth, 1, kFloor);
  frame.fill_disc(x_, y_, kRadius, kBall);
  // Origin marker, drawn last so it stays visible if the ball passes over it.
  for (int i = 1; i <= 4; ++i) {
    frame.set(i, 0, kAxisX);
    frame.set(0, i, kAxisY);
  }
  frame.set(0, 0, kOrigin);
}

}  // namespace p64
