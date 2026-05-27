#include "Vector3.h"
#include "Matrix4x4.h"

Vector3::Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

Vector3 Vector3::Add(const Vector3& v) const { return {x + v.x, y + v.y, z + v.z}; }

Vector3 Vector3::Subtract(const Vector3& v) const { return {x - v.x, y - v.y, z - v.z}; }

Vector3 Vector3::Multiply(float s) const { return {x * s, y * s, z * s}; }

float Vector3::Dot(const Vector3& v) const { return x * v.x + y * v.y + z * v.z; }

static float MySqrt(float x) {
	if (x <= 0.0f)
		return 0.0f;

	float result = x;
	for (int i = 0; i < 10; i++) {
		result = 0.5f * (result + x / result);
	}
	return result;
}

float Vector3::Length() const { return MySqrt(x * x + y * y + z * z); }
Vector3 Vector3::Normalize() const {
	float len = Length();
	if (len < 0.00001f)
		return {0, 0, 0};
	return {x / len, y / len, z / len};
}

Vector3 Vector3::Transform(const Vector3& v, const Matrix4x4& m) {

	float w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];

	Vector3 result;

	result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0];

	result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1];

	result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2];

	if (w != 0.0f) {
		result.x /= w;
		result.y /= w;
		result.z /= w;
	}

	return result;
}

Vector3 Vector3::Cross(const Vector3& v1, const Vector3& v2) { 
	return {
		v1.y * v2.z - v1.z * v2.y,
		v1.z * v2.x - v1.x * v2.z,
		v1.x * v2.y - v1.y * v2.x}; 
}

