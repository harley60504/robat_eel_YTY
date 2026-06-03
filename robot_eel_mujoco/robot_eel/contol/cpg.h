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
    cpg[j].theta = -j / fmaxf(lambda * L, 1e-6f);
    cpg[j].alpha = 4.0f;
    cpg[j].mu = 1.0f;
  }
}

inline float getCPGOutput(int j) {
  return Ajoint * cpg[j].r * cosf(cpg[j].theta);
}


inline float getLambdaInput() { return fmaxf(lambda * L, 1e-6f); }
inline float getTargetDelta() { return 1.0f / getLambdaInput(); }

inline void updateCPG(float t, float dt, int j, float fb_phase, float fb_amp) {
  HopfOscillator &o = cpg[j];
  float omega = 2.0f * M_PI * frequency;
  float dr = o.alpha * (o.mu - o.r * o.r) * o.r;
  float dtheta = omega;

  const float K_couple   = 0.35f;
  const float K_anchor   = 0.10f;
  const float k_fb_phase = 0.8f;
  const float k_fb_amp   = 0.25f;
  const float target_delta = getTargetDelta();

  if (j - 1 >= 0) {
    float desiredL = +target_delta;
    float errL = wrap_pi((cpg[j-1].theta - o.theta) - desiredL);
    dtheta += K_couple * sinf(errL);
  }
  if (j + 1 < bodyNum) {
    float desiredR = -target_delta;
    float errR = wrap_pi((cpg[j+1].theta - o.theta) - desiredR);
    dtheta += K_couple * sinf(errR);
  }

  float th_ref = omega * t - j / fmaxf(getLambdaInput(), 1e-6f);
  float e_ref = wrap_pi(th_ref - o.theta);
  dtheta += K_anchor * sinf(e_ref);

  dtheta += k_fb_phase * fb_phase;
  dr     += k_fb_amp   * fb_amp;

  o.r      = fmaxf(0.0f, o.r + dr * dt);
  o.theta  = wrap_pi(o.theta + dtheta * dt);
}
