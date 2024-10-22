#ifndef VMATH_H
#define VMATH_H

#include <math.h>

typedef struct _Vector2 Vector2;

typedef struct _Vector2 {
	double x;
	double y;
} Vector2;

Vector2 Vector2Init(double x, double y);
Vector2 Vector2Add(Vector2 a, Vector2 b);
double Vector2DistanceSqr(Vector2 a, Vector2 b);
double Vector2Distance(Vector2 a, Vector2 b);
Vector2 Vector2Scale(Vector2 a, double scale);

inline Vector2 Vector2Init(double x, double y) {
	return (Vector2){x, y};
}

Vector2 Vector2Add(Vector2 a, Vector2 b) {
	return Vector2Init(a.x+b.x, a.y+b.y);
}

double Vector2DistanceSqr(Vector2 a, Vector2 b) {
	return (a.x -  b.x)*(a.x -b.x) + (a.y - b.y)*(a.y - b.y);
}

double Vector2Distance(Vector2 a, Vector2 b) {
	double distanceSqr = Vector2DistanceSqr(a, b);
	double distance = sqrt(distanceSqr);
	return distance;
}

Vector2 Vector2Scale(Vector2 a, double scale) {
	return Vector2Init(a.x*scale, a.y*scale);
}

#endif
