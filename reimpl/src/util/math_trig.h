#pragma once

// Trig helpers from gilde.exe. These are declared in util/math.h too; this
// sibling header lets math.cpp use them without pulling in the whole module.
// See math.h for the canonical doc comments.
namespace guild::util {

double Atan2(double y, double x);
double Atan2Unary(double y);
double AcosGuarded(double x);

} // namespace guild::util
