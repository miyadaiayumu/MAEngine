#pragma once

struct Matrix4x4;
struct Vector3 {
	float x, y, z;

	Vector3(float x = 0, float y = 0, float z = 0);

	Vector3 Add(const Vector3& v) const;
	Vector3 Subtract(const Vector3& v) const;
	Vector3 Multiply(float s) const;
	float Dot(const Vector3& v) const;
	float Length() const;
	Vector3 Normalize() const;

	static Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix);

	static Vector3 Cross(const Vector3& v1, const Vector3& v2);
};