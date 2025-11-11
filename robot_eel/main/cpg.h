#pragma once
#include <math.h>
#include "config.h"
#include "utils.h"

inline float wrap_pi(float x) {
  while (x >  M_PI) x -= 2*M_PI;
  while (x < -M_PI) x += 2*M_PI;
  return x;
}

inline void initCPG() {
  for (int j = 0; j < bodyNum; j++) {
    cpg[j].r = 0.25f;
    cpg[j].theta = j / (lambda * L);
    cpg[j].alpha = 12.0f;
    cpg[j].mu = 1.0f;
  }
}

inline float getCPGOutput(int j) {
  return Ajoint * cpg[j].r * cosf(cpg[j].theta);
}

inline float getSensorAngle(int j) {
  float v = adsVoltage1[j % 4];
  if (v < adsMinValidVoltage) v = 0.0f;
  const float in_min = 3.16f;
  const float in_max = 2.26f;
  float angle = clampf(linmap(v, in_min, in_max, 0.0f, 90.0f), 0.0f, 180.0f);
  return angle;
}

inline float getLambdaInput() { return lambda * L; }
inline float getTargetDelta() { return 1.0f / getLambdaInput(); }

inline void updateCPG(float t, float dt, int j, float fb_phase, float fb_amp) {
  HopfOscillator &o = cpg[j];
  float omega = 2.0f * M_PI * frequency;
  float dr = o.alpha * (o.mu - o.r * o.r) * o.r;
  float dtheta = omega;

  const float K_couple   = 1.0f;
  const float K_anchor   = 0.3f;
  const float k_fb_phase = 0.8f;
  const float k_fb_amp   = 0.25f;
  const float target_delta = getTargetDelta();

  if (j - 1 >= 0) {
    float errL = wrap_pi((cpg[j-1].theta - o.theta) - (-target_delta));
    dtheta += K_couple * sinf(errL);
  }
  if (j + 1 < bodyNum) {
    float errR = wrap_pi((cpg[j+1].theta - o.theta) - (+target_delta));
    dtheta += K_couple * sinf(errR);
  }

  float th_ref = omega * t + j / getLambdaInput();
  float e_ref = wrap_pi(th_ref - o.theta);
  dtheta += K_anchor * sinf(e_ref);

  dtheta += k_fb_phase * fb_phase;
  dr     += k_fb_amp   * fb_amp;

  o.r     += dr * dt;
  o.theta  = wrap_pi(o.theta + dtheta * dt);
}
