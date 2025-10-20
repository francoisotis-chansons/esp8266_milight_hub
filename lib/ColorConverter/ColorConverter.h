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

// --- Helpers ---
  static inline uint8_t clamp8_u16(uint16_t x) { return (x > 255) ? 255 : (uint8_t)x; }

// Overload: RGB uint8_t + HSV en tableau float[3]
  static void rgbToHsv(uint8_t r, uint8_t g, uint8_t b, float hsv[3]) {
    float h, s, v;
    rgbToHsv(r, g, b, h, s, v);
    hsv[0] = h; hsv[1] = s; hsv[2] = v; 
  }

// Overload: RGB uint8_t + HSV en tableau double[3]
  static void rgbToHsv(uint8_t r, uint8_t g, uint8_t b, double hsv[3]) {
    float h, s, v;
    rgbToHsv(r, g, b, h, s, v);
    hsv[0] = (double)h; hsv[1] = (double)s; hsv[2] = (double)v;
  }

// Overload: RGB uint16_t + HSV en tableau double[3]
  static void rgbToHsv(uint16_t r, uint16_t g, uint16_t b, double hsv[3]) {
    rgbToHsv(clamp8_u16(r), clamp8_u16(g), clamp8_u16(b), hsv);
  }

// Overload: RGB uint16_t + HSV en tableau float[3]
  static void rgbToHsv(uint16_t r, uint16_t g, uint16_t b, float hsv[3]) {
    rgbToHsv(clamp8_u16(r), clamp8_u16(g), clamp8_u16(b), hsv);
  }

// (facultatif) Surcharges symétriques pour HSV->RGB si le projet les appelle un jour
  static void hsvArrayToRgb(const float hsv[3], uint8_t &r, uint8_t &g, uint8_t &b) {
    hsvToRgb(hsv[0], hsv[1], hsv[2], r, g, b);
  }
  static void hsvArrayToRgb(const double hsv[3], uint8_t &r, uint8_t &g, uint8_t &b) {
    hsvToRgb((float)hsv[0], (float)hsv[1], (float)hsv[2], r, g, b);
  }
  static void hsvArrayToRgb(const double hsv[3], uint16_t &r, uint16_t &g, uint16_t &b) {
    uint8_t rr, gg, bb; hsvToRgb((float)hsv[0], (float)hsv[1], (float)hsv[2], rr, gg, bb);
    r = rr; g = gg; b = bb;
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

