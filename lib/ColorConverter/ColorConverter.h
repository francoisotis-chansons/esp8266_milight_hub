#pragma once
#include <stdint.h>

// ColorConverter minimal compatible shim for MiLight Hub.
// Provides HSV <-> RGB and Kelvin -> RGB conversions used by ParsedColor.cpp.

class ColorConverter {
public:
  // r,g,b in [0..255]  -> h in [0..360), s,v in [0..1]
  static void rgbToHsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float maxc = rf; if (gf > maxc) maxc = gf; if (bf > maxc) maxc = bf;
    float minc = rf; if (gf < minc) minc = gf; if (bf < minc) minc = bf;
    v = maxc;
    float d = maxc - minc;
    s = (maxc == 0.0f) ? 0.0f : (d / maxc);
    if (d == 0.0f) { h = 0.0f; return; }
    if (maxc == rf)      h = 60.0f * fmodf(((gf - bf) / d), 6.0f);
    else if (maxc == gf) h = 60.0f * (((bf - rf) / d) + 2.0f);
    else                 h = 60.0f * (((rf - gf) / d) + 4.0f);
    if (h < 0.0f) h += 360.0f;
  }

  // h in [0..360), s,v in [0..1] -> r,g,b in [0..255]
  static void hsvToRgb(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (s <= 0.0f) { r = g = b = (uint8_t)(v * 255.0f + 0.5f); return; }
    h = fmodf(h, 360.0f); if (h < 0.0f) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf=0, gf=0, bf=0;
    if      (h <  60.0f) { rf = c; gf = x; bf = 0; }
    else if (h < 120.0f) { rf = x; gf = c; bf = 0; }
    else if (h < 180.0f) { rf = 0; gf = c; bf = x; }
    else if (h < 240.0f) { rf = 0; gf = x; bf = c; }
    else if (h < 300.0f) { rf = x; gf = 0; bf = c; }
    else                 { rf = c; gf = 0; bf = x; }
    r = (uint8_t)((rf + m) * 255.0f + 0.5f);
    g = (uint8_t)((gf + m) * 255.0f + 0.5f);
    b = (uint8_t)((bf + m) * 255.0f + 0.5f);
  }

  // Simple approximation to convert color temperature (Kelvin) to RGB.
  // kelvin typical range: 1000..40000
  static void colorTemperatureToRgb(uint16_t kelvin, uint8_t &r, uint8_t &g, uint8_t &b) {
    float temp = kelvin / 100.0f;
    float rf, gf, bf;
    // Red
    if (temp <= 66.0f) rf = 255.0f;
    else {
      rf = 329.698727446f * powf(temp - 60.0f, -0.1332047592f);
      if (rf < 0) rf = 0; if (rf > 255) rf = 255;
    }
    // Green
    if (temp <= 66.0f) {
      gf = 99.4708025861f * logf(temp) - 161.1195681661f;
    } else {
      gf = 288.1221695283f * powf(temp - 60.0f, -0.0755148492f);
    }
    if (gf < 0) gf = 0; if (gf > 255) gf = 255;
    // Blue
    if (temp >= 66.0f) bf = 255.0f;
    else if (temp <= 19.0f) bf = 0.0f;
    else bf = 138.5177312231f * logf(temp - 10.0f) - 305.0447927307f;
    if (bf < 0) bf = 0; if (bf > 255) bf = 255;

    r = (uint8_t)(rf + 0.5f);
    g = (uint8_t)(gf + 0.5f);
    b = (uint8_t)(bf + 0.5f);
  }
};

