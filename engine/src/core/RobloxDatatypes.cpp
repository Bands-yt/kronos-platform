#include "core/RobloxDatatypes.hpp"

#include <cstdio>
#include <string>

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

namespace engine::core {
namespace {

// Written in Luau so the behaviour reads like the Roblox docs. Split into
// pieces because MSVC limits a single string literal to 16 KB.
const char* const kSourceParts[] = {
R"LUAU(
local freeze, setmt, rawtypeof = table.freeze, setmetatable, typeof
local sqrt, abs, floor, ceil = math.sqrt, math.abs, math.floor, math.ceil
local sin, cos, atan2, asin, acos = math.sin, math.cos, math.atan2, math.asin, math.acos
local clamp, min, max = math.clamp, math.min, math.max
local G = {}

G.typeof = function(v)
	local t = rawtypeof(v)
	if t == "table" then
		local mt = getmetatable(v)
		if rawtypeof(mt) == "table" then
			local name = rawget(mt, "__type")
			if name ~= nil then return name end
		end
	end
	return t
end

local function newType(name, getters, methods)
	local mt = { __type = name }
	mt.__index = function(_, key)
		local m = methods[key]
		if m ~= nil then return m end
		local g = getters[key]
		if g ~= nil then return g(_) end
		error(tostring(key) .. " is not a valid member of " .. name, 2)
	end
	mt.__newindex = function(_, key)
		error(tostring(key) .. " cannot be assigned to", 2)
	end
	return mt
end

local function num(v, default)
	if v == nil then return default end
	if rawtypeof(v) ~= "number" then error("number expected, got " .. G.typeof(v), 3) end
	return v
end

-- Vector3
local Vector3, v3get, v3m = {}, {}, {}
local V3mt = newType("Vector3", v3get, v3m)
local function v3(x, y, z) return freeze(setmt({ X = x, Y = y, Z = z }, V3mt)) end
local function isV3(v) return getmetatable(v) == V3mt end

v3get.Magnitude = function(a) return sqrt(a.X * a.X + a.Y * a.Y + a.Z * a.Z) end
v3get.Unit = function(a)
	local m = sqrt(a.X * a.X + a.Y * a.Y + a.Z * a.Z)
	return v3(a.X / m, a.Y / m, a.Z / m)
end
v3m.Dot = function(a, b) return a.X * b.X + a.Y * b.Y + a.Z * b.Z end
v3m.Cross = function(a, b)
	return v3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X)
end
v3m.Lerp = function(a, b, t) return v3(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t) end
v3m.FuzzyEq = function(a, b, eps)
	eps = eps or 1e-5
	return abs(a.X - b.X) <= eps and abs(a.Y - b.Y) <= eps and abs(a.Z - b.Z) <= eps
end
v3m.Max = function(a, b) return v3(max(a.X, b.X), max(a.Y, b.Y), max(a.Z, b.Z)) end
v3m.Min = function(a, b) return v3(min(a.X, b.X), min(a.Y, b.Y), min(a.Z, b.Z)) end
v3m.Abs = function(a) return v3(abs(a.X), abs(a.Y), abs(a.Z)) end
v3m.Floor = function(a) return v3(floor(a.X), floor(a.Y), floor(a.Z)) end
v3m.Ceil = function(a) return v3(ceil(a.X), ceil(a.Y), ceil(a.Z)) end
v3m.Sign = function(a) return v3(math.sign(a.X), math.sign(a.Y), math.sign(a.Z)) end
v3m.Angle = function(a, b)
	local d = (a.X * b.X + a.Y * b.Y + a.Z * b.Z) / (sqrt(a.X * a.X + a.Y * a.Y + a.Z * a.Z) * sqrt(b.X * b.X + b.Y * b.Y + b.Z * b.Z))
	return acos(clamp(d, -1, 1))
end

V3mt.__add = function(a, b)
	if isV3(a) and isV3(b) then return v3(a.X + b.X, a.Y + b.Y, a.Z + b.Z) end
	error("attempt to perform arithmetic (add) on " .. G.typeof(a) .. " and " .. G.typeof(b), 2)
end
V3mt.__sub = function(a, b)
	if isV3(a) and isV3(b) then return v3(a.X - b.X, a.Y - b.Y, a.Z - b.Z) end
	error("attempt to perform arithmetic (sub) on " .. G.typeof(a) .. " and " .. G.typeof(b), 2)
end
V3mt.__mul = function(a, b)
	if rawtypeof(a) == "number" then return v3(a * b.X, a * b.Y, a * b.Z) end
	if rawtypeof(b) == "number" then return v3(a.X * b, a.Y * b, a.Z * b) end
	if isV3(a) and isV3(b) then return v3(a.X * b.X, a.Y * b.Y, a.Z * b.Z) end
	error("attempt to perform arithmetic (mul) on " .. G.typeof(a) .. " and " .. G.typeof(b), 2)
end
V3mt.__div = function(a, b)
	if rawtypeof(b) == "number" then return v3(a.X / b, a.Y / b, a.Z / b) end
	if rawtypeof(a) == "number" then return v3(a / b.X, a / b.Y, a / b.Z) end
	if isV3(a) and isV3(b) then return v3(a.X / b.X, a.Y / b.Y, a.Z / b.Z) end
	error("attempt to perform arithmetic (div) on " .. G.typeof(a) .. " and " .. G.typeof(b), 2)
end
V3mt.__unm = function(a) return v3(-a.X, -a.Y, -a.Z) end
V3mt.__eq = function(a, b) return a.X == b.X and a.Y == b.Y and a.Z == b.Z end
V3mt.__tostring = function(a) return tostring(a.X) .. ", " .. tostring(a.Y) .. ", " .. tostring(a.Z) end
freeze(V3mt)

Vector3.new = function(x, y, z) return v3(num(x, 0), num(y, 0), num(z, 0)) end
Vector3.zero, Vector3.one = v3(0, 0, 0), v3(1, 1, 1)
Vector3.xAxis, Vector3.yAxis, Vector3.zAxis = v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1)
G.Vector3 = freeze(Vector3)

-- Vector2
local Vector2, v2get, v2m = {}, {}, {}
local V2mt = newType("Vector2", v2get, v2m)
local function v2(x, y) return freeze(setmt({ X = x, Y = y }, V2mt)) end
local function isV2(v) return getmetatable(v) == V2mt end
v2get.Magnitude = function(a) return sqrt(a.X * a.X + a.Y * a.Y) end
v2get.Unit = function(a)
	local m = sqrt(a.X * a.X + a.Y * a.Y)
	return v2(a.X / m, a.Y / m)
end
v2m.Dot = function(a, b) return a.X * b.X + a.Y * b.Y end
v2m.Cross = function(a, b) return a.X * b.Y - a.Y * b.X end
v2m.Lerp = function(a, b, t) return v2(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t) end
v2m.Max = function(a, b) return v2(max(a.X, b.X), max(a.Y, b.Y)) end
v2m.Min = function(a, b) return v2(min(a.X, b.X), min(a.Y, b.Y)) end
v2m.Abs = function(a) return v2(abs(a.X), abs(a.Y)) end
v2m.Floor = function(a) return v2(floor(a.X), floor(a.Y)) end
v2m.Ceil = function(a) return v2(ceil(a.X), ceil(a.Y)) end
V2mt.__add = function(a, b) return v2(a.X + b.X, a.Y + b.Y) end
V2mt.__sub = function(a, b) return v2(a.X - b.X, a.Y - b.Y) end
V2mt.__mul = function(a, b)
	if rawtypeof(a) == "number" then return v2(a * b.X, a * b.Y) end
	if rawtypeof(b) == "number" then return v2(a.X * b, a.Y * b) end
	return v2(a.X * b.X, a.Y * b.Y)
end
V2mt.__div = function(a, b)
	if rawtypeof(b) == "number" then return v2(a.X / b, a.Y / b) end
	if rawtypeof(a) == "number" then return v2(a / b.X, a / b.Y) end
	return v2(a.X / b.X, a.Y / b.Y)
end
V2mt.__unm = function(a) return v2(-a.X, -a.Y) end
V2mt.__eq = function(a, b) return a.X == b.X and a.Y == b.Y end
V2mt.__tostring = function(a) return tostring(a.X) .. ", " .. tostring(a.Y) end
freeze(V2mt)
Vector2.new = function(x, y) return v2(num(x, 0), num(y, 0)) end
Vector2.zero, Vector2.one = v2(0, 0), v2(1, 1)
Vector2.xAxis, Vector2.yAxis = v2(1, 0), v2(0, 1)
G.Vector2 = freeze(Vector2)
)LUAU",
R"LUAU(
-- CFrame: position X, Y, Z plus a row-major rotation matrix in [1]..[9].
local CFrame, cfget, cfm = {}, {}, {}
local CFmt = newType("CFrame", cfget, cfm)
local function cf(x, y, z, r00, r01, r02, r10, r11, r12, r20, r21, r22)
	return freeze(setmt({ X = x, Y = y, Z = z, r00, r01, r02, r10, r11, r12, r20, r21, r22 }, CFmt))
end
local function isCF(v) return getmetatable(v) == CFmt end
local IDENTITY = cf(0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1)

local function mul(a, b)
	local a1, a2, a3, a4, a5, a6, a7, a8, a9 = a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]
	local b1, b2, b3, b4, b5, b6, b7, b8, b9 = b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9]
	return cf(
		a.X + a1 * b.X + a2 * b.Y + a3 * b.Z,
		a.Y + a4 * b.X + a5 * b.Y + a6 * b.Z,
		a.Z + a7 * b.X + a8 * b.Y + a9 * b.Z,
		a1 * b1 + a2 * b4 + a3 * b7, a1 * b2 + a2 * b5 + a3 * b8, a1 * b3 + a2 * b6 + a3 * b9,
		a4 * b1 + a5 * b4 + a6 * b7, a4 * b2 + a5 * b5 + a6 * b8, a4 * b3 + a5 * b6 + a6 * b9,
		a7 * b1 + a8 * b4 + a9 * b7, a7 * b2 + a8 * b5 + a9 * b8, a7 * b3 + a8 * b6 + a9 * b9)
end
local function point(a, v)
	return v3(a.X + a[1] * v.X + a[2] * v.Y + a[3] * v.Z,
		a.Y + a[4] * v.X + a[5] * v.Y + a[6] * v.Z,
		a.Z + a[7] * v.X + a[8] * v.Y + a[9] * v.Z)
end
local function rotate(a, v)
	return v3(a[1] * v.X + a[2] * v.Y + a[3] * v.Z, a[4] * v.X + a[5] * v.Y + a[6] * v.Z, a[7] * v.X + a[8] * v.Y + a[9] * v.Z)
end
local function inverse(a)
	local r1, r2, r3, r4, r5, r6, r7, r8, r9 = a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]
	local x, y, z = a.X, a.Y, a.Z
	return cf(-(r1 * x + r4 * y + r7 * z), -(r2 * x + r5 * y + r8 * z), -(r3 * x + r6 * y + r9 * z),
		r1, r4, r7, r2, r5, r8, r3, r6, r9)
end

local function toQuat(a)
	local m1, m2, m3, m4, m5, m6, m7, m8, m9 = a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]
	local trace = m1 + m5 + m9
	if trace > 0 then
		local s = sqrt(trace + 1) * 2
		return (m8 - m6) / s, (m3 - m7) / s, (m4 - m2) / s, 0.25 * s
	elseif m1 > m5 and m1 > m9 then
		local s = sqrt(1 + m1 - m5 - m9) * 2
		return 0.25 * s, (m2 + m4) / s, (m3 + m7) / s, (m8 - m6) / s
	elseif m5 > m9 then
		local s = sqrt(1 + m5 - m1 - m9) * 2
		return (m2 + m4) / s, 0.25 * s, (m6 + m8) / s, (m3 - m7) / s
	end
	local s = sqrt(1 + m9 - m1 - m5) * 2
	return (m3 + m7) / s, (m6 + m8) / s, 0.25 * s, (m4 - m2) / s
end
local function fromQuat(x, y, z, w)
	local n = sqrt(x * x + y * y + z * z + w * w)
	x, y, z, w = x / n, y / n, z / n, w / n
	return 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
		2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
		2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)
end

local function lookAt(at, target, up)
	up = up or Vector3.yAxis
	local fx, fy, fz = target.X - at.X, target.Y - at.Y, target.Z - at.Z
	local fl = sqrt(fx * fx + fy * fy + fz * fz)
	if fl == 0 then return cf(at.X, at.Y, at.Z, 1, 0, 0, 0, 1, 0, 0, 0, 1) end
	fx, fy, fz = fx / fl, fy / fl, fz / fl
	local ux, uy, uz = up.X, up.Y, up.Z
	local rx, ry, rz = fy * uz - fz * uy, fz * ux - fx * uz, fx * uy - fy * ux
	local rl = sqrt(rx * rx + ry * ry + rz * rz)
	if rl < 1e-9 then
		ux, uy, uz = 0, 0, 1
		rx, ry, rz = fy * uz - fz * uy, fz * ux - fx * uz, fx * uy - fy * ux
		rl = sqrt(rx * rx + ry * ry + rz * rz)
	end
	rx, ry, rz = rx / rl, ry / rl, rz / rl
	ux, uy, uz = ry * fz - rz * fy, rz * fx - rx * fz, rx * fy - ry * fx
	return cf(at.X, at.Y, at.Z, rx, ux, -fx, ry, uy, -fy, rz, uz, -fz)
end

local function anglesXYZ(rx, ry, rz)
	local cx, sx, cy, sy, cz, sz = cos(rx), sin(rx), cos(ry), sin(ry), cos(rz), sin(rz)
	return cf(0, 0, 0,
		cy * cz, -cy * sz, sy,
		cx * sz + sx * sy * cz, cx * cz - sx * sy * sz, -sx * cy,
		sx * sz - cx * sy * cz, sx * cz + cx * sy * sz, cx * cy)
end
local function anglesYXZ(rx, ry, rz)
	local cx, sx, cy, sy, cz, sz = cos(rx), sin(rx), cos(ry), sin(ry), cos(rz), sin(rz)
	return cf(0, 0, 0,
		cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx,
		cx * sz, cx * cz, -sx,
		-sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx)
end

cfget.Position = function(a) return v3(a.X, a.Y, a.Z) end
cfget.p = cfget.Position
cfget.Rotation = function(a) return cf(0, 0, 0, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]) end
cfget.LookVector = function(a) return v3(-a[3], -a[6], -a[9]) end
cfget.RightVector = function(a) return v3(a[1], a[4], a[7]) end
cfget.UpVector = function(a) return v3(a[2], a[5], a[8]) end
cfget.XVector = cfget.RightVector
cfget.YVector = cfget.UpVector
cfget.ZVector = function(a) return v3(a[3], a[6], a[9]) end

cfm.Inverse = inverse
cfm.ToWorldSpace = function(a, b) return mul(a, b or IDENTITY) end
cfm.ToObjectSpace = function(a, b) return mul(inverse(a), b or IDENTITY) end
cfm.PointToWorldSpace = function(a, v) return point(a, v) end
cfm.PointToObjectSpace = function(a, v) return point(inverse(a), v) end
cfm.VectorToWorldSpace = function(a, v) return rotate(a, v) end
cfm.VectorToObjectSpace = function(a, v) return rotate(inverse(a), v) end
cfm.GetComponents = function(a) return a.X, a.Y, a.Z, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9] end
cfm.components = cfm.GetComponents
cfm.ToEulerAnglesXYZ = function(a)
	return atan2(-a[6], a[9]), asin(clamp(a[3], -1, 1)), atan2(-a[2], a[1])
end
cfm.ToEulerAnglesYXZ = function(a)
	return asin(clamp(-a[6], -1, 1)), atan2(a[3], a[9]), atan2(a[4], a[5])
end
cfm.ToOrientation = cfm.ToEulerAnglesYXZ
cfm.ToAxisAngle = function(a)
	local x, y, z, w = toQuat(a)
	local angle = 2 * acos(clamp(w, -1, 1))
	local s = sqrt(1 - w * w)
	if s < 1e-9 then return v3(1, 0, 0), 0 end
	return v3(x / s, y / s, z / s), angle
end
cfm.Lerp = function(a, b, t)
	local ax, ay, az, aw = toQuat(a)
	local bx, by, bz, bw = toQuat(b)
	local d = ax * bx + ay * by + az * bz + aw * bw
	if d < 0 then bx, by, bz, bw, d = -bx, -by, -bz, -bw, -d end
	local k0, k1
	if d > 0.9995 then
		k0, k1 = 1 - t, t
	else
		local theta = acos(d)
		local s = sin(theta)
		k0, k1 = sin((1 - t) * theta) / s, sin(t * theta) / s
	end
	return cf(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t,
		fromQuat(ax * k0 + bx * k1, ay * k0 + by * k1, az * k0 + bz * k1, aw * k0 + bw * k1))
end
cfm.FuzzyEq = function(a, b, eps)
	eps = eps or 1e-5
	if abs(a.X - b.X) > eps or abs(a.Y - b.Y) > eps or abs(a.Z - b.Z) > eps then return false end
	for i = 1, 9 do
		if abs(a[i] - b[i]) > eps then return false end
	end
	return true
end

CFmt.__mul = function(a, b)
	if isCF(a) and isCF(b) then return mul(a, b) end
	if isCF(a) and isV3(b) then return point(a, b) end
	error("attempt to perform arithmetic (mul) on " .. G.typeof(a) .. " and " .. G.typeof(b), 2)
end
CFmt.__add = function(a, b)
	if isCF(a) and isV3(b) then return cf(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]) end
	error("attempt to perform arithmetic (add) on " .. G.typeof(a) .. " and " .. G.typeof(b), 2)
end
CFmt.__sub = function(a, b)
	if isCF(a) and isV3(b) then return cf(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]) end
	error("attempt to perform arithmetic (sub) on " .. G.typeof(a) .. " and " .. G.typeof(b), 2)
end
CFmt.__eq = function(a, b)
	if a.X ~= b.X or a.Y ~= b.Y or a.Z ~= b.Z then return false end
	for i = 1, 9 do
		if a[i] ~= b[i] then return false end
	end
	return true
end
CFmt.__tostring = function(a)
	local parts = { a.X, a.Y, a.Z, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9] }
	for i, v in parts do parts[i] = tostring(v) end
	return table.concat(parts, ", ")
end
freeze(CFmt)

CFrame.new = function(...)
	local n = select("#", ...)
	if n == 0 then return IDENTITY end
	local a, b, c = ...
	if n == 1 then return cf(a.X, a.Y, a.Z, 1, 0, 0, 0, 1, 0, 0, 0, 1) end
	if n == 2 then return lookAt(a, b) end
	if n == 3 then return cf(num(a), num(b), num(c), 1, 0, 0, 0, 1, 0, 0, 0, 1) end
	if n == 7 then
		local x, y, z, qx, qy, qz, qw = ...
		return cf(x, y, z, fromQuat(qx, qy, qz, qw))
	end
	if n == 12 then return cf(...) end
	error("Invalid number of arguments: " .. n, 2)
end
CFrame.identity = IDENTITY
CFrame.lookAt = lookAt
CFrame.Angles = function(rx, ry, rz) return anglesXYZ(num(rx, 0), num(ry, 0), num(rz, 0)) end
CFrame.fromEulerAnglesXYZ = CFrame.Angles
CFrame.fromEulerAnglesYXZ = function(rx, ry, rz) return anglesYXZ(num(rx, 0), num(ry, 0), num(rz, 0)) end
CFrame.fromOrientation = CFrame.fromEulerAnglesYXZ
CFrame.fromAxisAngle = function(axis, angle)
	local u = axis.Unit
	local x, y, z = u.X, u.Y, u.Z
	local c, s = cos(angle), sin(angle)
	local t = 1 - c
	return cf(0, 0, 0,
		t * x * x + c, t * x * y - s * z, t * x * z + s * y,
		t * x * y + s * z, t * y * y + c, t * y * z - s * x,
		t * x * z - s * y, t * y * z + s * x, t * z * z + c)
end
CFrame.fromMatrix = function(pos, vx, vy, vz)
	vz = vz or vx:Cross(vy).Unit
	return cf(pos.X, pos.Y, pos.Z, vx.X, vy.X, vz.X, vx.Y, vy.Y, vz.Y, vx.Z, vy.Z, vz.Z)
end
G.CFrame = freeze(CFrame)
)LUAU",
R"LUAU(
-- Color3
local Color3, c3get, c3m = {}, {}, {}
local C3mt = newType("Color3", c3get, c3m)
local function c3(r, g, b) return freeze(setmt({ R = r, G = g, B = b }, C3mt)) end
local function hsv(h, s, v)
	local i = floor(h * 6)
	local f = h * 6 - i
	local p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
	i = i % 6
	if i == 0 then return c3(v, t, p) elseif i == 1 then return c3(q, v, p) elseif i == 2 then return c3(p, v, t)
	elseif i == 3 then return c3(p, q, v) elseif i == 4 then return c3(t, p, v) end
	return c3(v, p, q)
end
c3m.Lerp = function(a, b, t) return c3(a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t) end
c3m.ToHSV = function(c)
	local hi, lo = max(c.R, c.G, c.B), min(c.R, c.G, c.B)
	local d = hi - lo
	local h = 0
	if d > 0 then
		if hi == c.R then h = ((c.G - c.B) / d) % 6 elseif hi == c.G then h = (c.B - c.R) / d + 2 else h = (c.R - c.G) / d + 4 end
		h = h / 6
	end
	return h, hi == 0 and 0 or d / hi, hi
end
c3m.ToHex = function(c)
	local function byte(v) return clamp(math.round(v * 255), 0, 255) end
	return string.format("%02X%02X%02X", byte(c.R), byte(c.G), byte(c.B))
end
C3mt.__eq = function(a, b) return a.R == b.R and a.G == b.G and a.B == b.B end
C3mt.__tostring = function(c) return tostring(c.R) .. ", " .. tostring(c.G) .. ", " .. tostring(c.B) end
freeze(C3mt)
Color3.new = function(r, g, b) return c3(num(r, 0), num(g, 0), num(b, 0)) end
Color3.fromRGB = function(r, g, b) return c3(num(r, 0) / 255, num(g, 0) / 255, num(b, 0) / 255) end
Color3.fromHSV = function(h, s, v) return hsv(num(h), num(s), num(v)) end
Color3.toHSV = function(c) return c:ToHSV() end
Color3.fromHex = function(hex)
	hex = string.gsub(hex, "^#", "")
	if #hex == 3 then hex = string.gsub(hex, "(%x)", "%1%1") end
	if #hex ~= 6 or not string.match(hex, "^%x+$") then error("Unable to convert characters to hex value", 2) end
	return c3(tonumber(string.sub(hex, 1, 2), 16) / 255, tonumber(string.sub(hex, 3, 4), 16) / 255, tonumber(string.sub(hex, 5, 6), 16) / 255)
end
G.Color3 = freeze(Color3)

-- BrickColor: the commonly used part of Roblox's palette.
local PALETTE = {
	{ 1, "White", 242, 243, 243 }, { 5, "Brick yellow", 215, 197, 154 }, { 9, "Light reddish violet", 232, 186, 200 },
	{ 18, "Nougat", 204, 142, 105 }, { 21, "Bright red", 196, 40, 28 }, { 23, "Bright blue", 13, 105, 172 },
	{ 24, "Bright yellow", 245, 205, 48 }, { 26, "Black", 27, 42, 53 }, { 28, "Dark green", 40, 127, 71 },
	{ 37, "Bright green", 75, 151, 75 }, { 102, "Medium blue", 110, 153, 202 }, { 104, "Bright violet", 107, 50, 124 },
	{ 106, "Bright orange", 218, 133, 65 }, { 119, "Br. yellowish green", 164, 189, 71 }, { 141, "Earth green", 39, 70, 45 },
	{ 153, "Sand red", 149, 121, 119 }, { 192, "Reddish brown", 105, 64, 40 }, { 194, "Medium stone grey", 163, 162, 165 },
	{ 199, "Dark stone grey", 99, 95, 98 }, { 208, "Light stone grey", 229, 228, 223 }, { 1001, "Institutional white", 248, 248, 248 },
	{ 1002, "Mid gray", 205, 205, 205 }, { 1003, "Really black", 17, 17, 17 }, { 1004, "Really red", 255, 0, 0 },
	{ 1005, "Deep orange", 255, 176, 0 }, { 1006, "Alder", 180, 128, 255 }, { 1007, "Dusty Rose", 163, 75, 75 },
	{ 1009, "New Yeller", 255, 255, 0 }, { 1010, "Really blue", 0, 0, 255 }, { 1011, "Navy blue", 0, 32, 96 },
	{ 1012, "Deep blue", 33, 84, 185 }, { 1013, "Cyan", 4, 175, 236 }, { 1014, "CGA brown", 170, 85, 0 },
	{ 1015, "Magenta", 170, 0, 170 }, { 1016, "Pink", 255, 102, 204 }, { 1018, "Teal", 18, 238, 212 },
	{ 1019, "Toothpaste", 0, 255, 255 }, { 1020, "Lime green", 0, 255, 0 }, { 1021, "Camo", 58, 125, 21 },
	{ 1022, "Grime", 127, 142, 100 }, { 1027, "Pastel blue-green", 159, 243, 233 }, { 1030, "Pastel brown", 255, 204, 153 },
	{ 1032, "Hot pink", 255, 0, 191 },
}
local BrickColor, bcget = {}, {}
local BCmt = newType("BrickColor", bcget, {})
local byName, byNumber, all = {}, {}, {}
for _, e in PALETTE do
	local brick = freeze(setmt({ Name = e[2], Number = e[1], Color = Color3.fromRGB(e[3], e[4], e[5]) }, BCmt))
	byName[e[2]], byNumber[e[1]] = brick, brick
	table.insert(all, brick)
end
local DEFAULT_BRICK = byNumber[194]
bcget.r = function(b) return b.Color.R end
bcget.g = function(b) return b.Color.G end
bcget.b = function(b) return b.Color.B end
BCmt.__eq = function(a, b) return a.Number == b.Number end
BCmt.__tostring = function(b) return b.Name end
freeze(BCmt)
local function nearest(r, g, b)
	local best, bestD = DEFAULT_BRICK, math.huge
	for _, brick in all do
		local c = brick.Color
		local d = (c.R - r) ^ 2 + (c.G - g) ^ 2 + (c.B - b) ^ 2
		if d < bestD then best, bestD = brick, d end
	end
	return best
end
BrickColor.new = function(a, g, b)
	if a == nil then return DEFAULT_BRICK end
	if rawtypeof(a) == "string" then return byName[a] or DEFAULT_BRICK end
	if getmetatable(a) == C3mt then return nearest(a.R, a.G, a.B) end
	if rawtypeof(a) == "number" and g == nil then return byNumber[a] or DEFAULT_BRICK end
	return nearest(num(a), num(g), num(b))
end
BrickColor.random = function() return all[math.random(1, #all)] end
BrickColor.White = function() return byNumber[1] end
BrickColor.Gray = function() return byNumber[194] end
BrickColor.DarkGray = function() return byNumber[199] end
BrickColor.Black = function() return byNumber[26] end
BrickColor.Red = function() return byNumber[21] end
BrickColor.Yellow = function() return byNumber[24] end
BrickColor.Green = function() return byNumber[28] end
BrickColor.Blue = function() return byNumber[23] end
G.BrickColor = freeze(BrickColor)

-- UDim and UDim2
local UDim, UDim2, u2get, u2m = {}, {}, {}, {}
local UDmt = newType("UDim", {}, {})
local U2mt = newType("UDim2", u2get, u2m)
local function udim(s, o) return freeze(setmt({ Scale = s, Offset = o }, UDmt)) end
local function udim2(xs, xo, ys, yo) return freeze(setmt({ X = udim(xs, xo), Y = udim(ys, yo) }, U2mt)) end
UDmt.__add = function(a, b) return udim(a.Scale + b.Scale, a.Offset + b.Offset) end
UDmt.__sub = function(a, b) return udim(a.Scale - b.Scale, a.Offset - b.Offset) end
UDmt.__unm = function(a) return udim(-a.Scale, -a.Offset) end
UDmt.__eq = function(a, b) return a.Scale == b.Scale and a.Offset == b.Offset end
UDmt.__tostring = function(a) return tostring(a.Scale) .. ", " .. tostring(a.Offset) end
freeze(UDmt)
u2get.Width = function(u) return u.X end
u2get.Height = function(u) return u.Y end
u2m.Lerp = function(a, b, t)
	return udim2(a.X.Scale + (b.X.Scale - a.X.Scale) * t, a.X.Offset + (b.X.Offset - a.X.Offset) * t,
		a.Y.Scale + (b.Y.Scale - a.Y.Scale) * t, a.Y.Offset + (b.Y.Offset - a.Y.Offset) * t)
end
U2mt.__add = function(a, b) return udim2(a.X.Scale + b.X.Scale, a.X.Offset + b.X.Offset, a.Y.Scale + b.Y.Scale, a.Y.Offset + b.Y.Offset) end
U2mt.__sub = function(a, b) return udim2(a.X.Scale - b.X.Scale, a.X.Offset - b.X.Offset, a.Y.Scale - b.Y.Scale, a.Y.Offset - b.Y.Offset) end
U2mt.__unm = function(a) return udim2(-a.X.Scale, -a.X.Offset, -a.Y.Scale, -a.Y.Offset) end
U2mt.__eq = function(a, b) return a.X == b.X and a.Y == b.Y end
U2mt.__tostring = function(a) return "{" .. tostring(a.X) .. "}, {" .. tostring(a.Y) .. "}" end
freeze(U2mt)
UDim.new = function(s, o) return udim(num(s, 0), num(o, 0)) end
UDim2.new = function(a, b, c, d)
	if getmetatable(a) == UDmt then return udim2(a.Scale, a.Offset, b.Scale, b.Offset) end
	return udim2(num(a, 0), num(b, 0), num(c, 0), num(d, 0))
end
UDim2.fromScale = function(x, y) return udim2(num(x, 0), 0, num(y, 0), 0) end
UDim2.fromOffset = function(x, y) return udim2(0, num(x, 0), 0, num(y, 0)) end
G.UDim, G.UDim2 = freeze(UDim), freeze(UDim2)
)LUAU",
R"LUAU(
-- Enum: Enum.Material.Plastic and friends. Values match Roblox.
local ENUMS = {
	Material = "Plastic=256 SmoothPlastic=272 Neon=288 Wood=512 WoodPlanks=528 Marble=784 Slate=800 Concrete=816 Granite=832 Brick=848 Pebble=864 Cobblestone=880 CorrodedMetal=1040 DiamondPlate=1056 Foil=1072 Metal=1088 Grass=1280 Sand=1296 Fabric=1312 Ice=1536 Glass=1568 ForceField=1584 Air=1792 Water=2048",
	PartType = "Ball=0 Block=1 Cylinder=2 Wedge=3 CornerWedge=4",
	EasingStyle = "Linear=0 Sine=1 Back=2 Quad=3 Quart=4 Quint=5 Bounce=6 Elastic=7 Exponential=8 Circular=9 Cubic=10",
	EasingDirection = "In=0 Out=1 InOut=2",
	NormalId = "Right=0 Top=1 Back=2 Left=3 Bottom=4 Front=5",
	SurfaceType = "Smooth=0 Glue=1 Weld=2 Studs=3 Inlet=4 Universal=5 Hinge=6 Motor=7 SteppingMotor=8 SmoothNoOutlines=10",
	KeyCode = "Unknown=0 Backspace=8 Tab=9 Return=13 Escape=27 Space=32 Zero=48 One=49 Two=50 Three=51 Four=52 Five=53 Six=54 Seven=55 Eight=56 Nine=57 A=97 B=98 C=99 D=100 E=101 F=102 G=103 H=104 I=105 J=106 K=107 L=108 M=109 N=110 O=111 P=112 Q=113 R=114 S=115 T=116 U=117 V=118 W=119 X=120 Y=121 Z=122 Up=273 Down=274 Right=275 Left=276 F1=282 F2=283 F3=284 F4=285 F5=286 F6=287 F7=288 F8=289 F9=290 F10=291 F11=292 F12=293 RightShift=303 LeftShift=304 RightControl=305 LeftControl=306 RightAlt=307 LeftAlt=308",
	UserInputType = "MouseButton1=0 MouseButton2=1 MouseButton3=2 MouseWheel=3 MouseMovement=4 Touch=7 Keyboard=8 Focus=9 Accelerometer=10 Gyro=11 Gamepad1=12 Gamepad2=13 Gamepad3=14 Gamepad4=15 TextInput=20 None=22",
	UserInputState = "Begin=0 Change=1 End=2 Cancel=3 None=4",
	HumanoidStateType = "FallingDown=0 Ragdoll=1 GettingUp=2 Jumping=3 Swimming=4 Freefall=5 Flying=6 Landed=7 Running=8 RunningNoPhysics=10 StrafingNoPhysics=11 Climbing=12 Seated=13 PlatformStanding=14 Dead=15 Physics=16 None=18",
	HumanoidRigType = "R6=0 R15=1",
	RaycastFilterType = "Exclude=0 Include=1",
	SortOrder = "Name=0 Custom=1 LayoutOrder=2",
	FillDirection = "Horizontal=0 Vertical=1",
	HorizontalAlignment = "Center=0 Left=1 Right=2",
	VerticalAlignment = "Center=0 Top=1 Bottom=2",
	TextXAlignment = "Left=0 Right=1 Center=2",
	TextYAlignment = "Top=0 Center=1 Bottom=2",
	Font = "Legacy=0 Arial=1 ArialBold=2 SourceSans=3 SourceSansBold=4 SourceSansLight=5 SourceSansItalic=6 Bodoni=7 Garamond=8 Cartoon=9 Code=10 Highway=11 SciFi=12 Arcade=13 Fantasy=14 Antique=15 SourceSansSemibold=16 Gotham=17 GothamSemibold=18 GothamBold=19 GothamBlack=20",
	ScaleType = "Stretch=0 Slice=1 Tile=2 Fit=3 Crop=4",
	AutomaticSize = "None=0 X=1 Y=2 XY=3",
	ZIndexBehavior = "Global=0 Sibling=1",
	AnimationPriority = "Idle=0 Movement=1 Action=2 Action2=3 Action3=4 Action4=5 Core=1000",
	CameraType = "Fixed=0 Attach=1 Watch=2 Track=3 Follow=4 Custom=5 Scriptable=6 Orbital=7",
	RenderPriority = "First=0 Input=100 Camera=200 Character=300 Last=2000",
	PlaybackState = "Begin=0 Delayed=1 Playing=2 Paused=3 Completed=4 Cancelled=5",
}

local EnumItemMt = { __type = "EnumItem" }
EnumItemMt.__index = function(_, key) error(tostring(key) .. " is not a valid member of EnumItem", 2) end
EnumItemMt.__newindex = function(_, key) error(tostring(key) .. " cannot be assigned to", 2) end
EnumItemMt.__tostring = function(item) return "Enum." .. tostring(item.EnumType) .. "." .. item.Name end
freeze(EnumItemMt)

local enumNames, enumItems = {}, {}
local EnumTypeMt = { __type = "Enum" }
EnumTypeMt.__index = function(self, key)
	if key == "GetEnumItems" then
		return function(e) return table.clone(enumItems[e]) end
	elseif key == "FromName" then
		return function(e, name) return rawget(e, name) end
	elseif key == "FromValue" then
		return function(e, value)
			for _, item in enumItems[e] do
				if item.Value == value then return item end
			end
			return nil
		end
	end
	error(tostring(key) .. " is not a valid member of \"Enum." .. enumNames[self] .. "\"", 2)
end
EnumTypeMt.__newindex = function(_, key) error(tostring(key) .. " cannot be assigned to", 2) end
EnumTypeMt.__tostring = function(e) return enumNames[e] end
freeze(EnumTypeMt)

local EnumRoot, enumList = {}, {}
for enumName, spec in ENUMS do
	local enumType = setmt({}, EnumTypeMt)
	enumNames[enumType] = enumName
	local items = {}
	for itemName, value in string.gmatch(spec, "(%w+)=(%d+)") do
		local item = freeze(setmt({ Name = itemName, Value = tonumber(value), EnumType = enumType }, EnumItemMt))
		rawset(enumType, itemName, item)
		table.insert(items, item)
	end
	table.sort(items, function(a, b) return a.Value < b.Value end)
	enumItems[enumType] = items
	EnumRoot[enumName] = freeze(enumType)
	table.insert(enumList, enumType)
end
local EnumsMt = { __type = "Enums" }
EnumsMt.__index = function(_, key)
	if key == "GetEnums" then return function() return table.clone(enumList) end end
	error(tostring(key) .. " is not a valid member of \"Enums\"", 2)
end
freeze(EnumsMt)
G.Enum = freeze(setmt(EnumRoot, EnumsMt))
)LUAU",
R"LUAU(
-- TweenInfo
local TweenInfo = {}
local TImt = newType("TweenInfo", {}, {})
freeze(TImt)
TweenInfo.new = function(time, style, direction, repeatCount, reverses, delayTime)
	return freeze(setmt({
		Time = num(time, 1),
		EasingStyle = style or G.Enum.EasingStyle.Quad,
		EasingDirection = direction or G.Enum.EasingDirection.Out,
		RepeatCount = num(repeatCount, 0),
		Reverses = reverses == true,
		DelayTime = num(delayTime, 0),
	}, TImt))
end
G.TweenInfo = freeze(TweenInfo)

-- NumberRange, NumberSequence, ColorSequence
local NumberRange = {}
local NRmt = newType("NumberRange", {}, {})
NRmt.__eq = function(a, b) return a.Min == b.Min and a.Max == b.Max end
NRmt.__tostring = function(r) return tostring(r.Min) .. " " .. tostring(r.Max) end
freeze(NRmt)
NumberRange.new = function(lo, hi)
	lo = num(lo)
	hi = num(hi, lo)
	if lo > hi then error("NumberRange: invalid range", 2) end
	return freeze(setmt({ Min = lo, Max = hi }, NRmt))
end
G.NumberRange = freeze(NumberRange)

local function sequenceType(name, keypointName, valueCheck)
	local Keypoint, Sequence = {}, {}
	local KPmt = newType(keypointName, {}, {})
	KPmt.__eq = function(a, b) return a.Time == b.Time and a.Value == b.Value and a.Envelope == b.Envelope end
	freeze(KPmt)
	local SQmt = newType(name, {}, {})
	freeze(SQmt)
	local function keypoint(t, v, e) return freeze(setmt({ Time = t, Value = v, Envelope = e }, KPmt)) end
	Keypoint.new = function(t, v, e) return keypoint(num(t), valueCheck(v), num(e, 0)) end
	Sequence.new = function(a, b)
		local points
		if rawtypeof(a) == "table" and getmetatable(a) == nil then
			points = a
			if #points < 2 or points[1].Time ~= 0 or points[#points].Time ~= 1 then
				error(name .. ": requires at least 2 keypoints, starting at time 0 and ending at time 1", 2)
			end
			for i = 2, #points do
				if points[i].Time < points[i - 1].Time then error(name .. ": keypoints must be ordered by time", 2) end
			end
			points = table.clone(points)
		else
			points = { keypoint(0, valueCheck(a), 0), keypoint(1, valueCheck(b == nil and a or b), 0) }
		end
		return freeze(setmt({ Keypoints = freeze(points) }, SQmt))
	end
	return freeze(Keypoint), freeze(Sequence)
end
G.NumberSequenceKeypoint, G.NumberSequence = sequenceType("NumberSequence", "NumberSequenceKeypoint", function(v) return num(v) end)
G.ColorSequenceKeypoint, G.ColorSequence = sequenceType("ColorSequence", "ColorSequenceKeypoint", function(v)
	if getmetatable(v) ~= C3mt then error("Color3 expected", 4) end
	return v
end)

-- Ray and RaycastParams
local Ray, rayget, raym = {}, {}, {}
local RAYmt = newType("Ray", rayget, raym)
freeze(RAYmt)
local function ray(o, d) return freeze(setmt({ Origin = o, Direction = d }, RAYmt)) end
rayget.Unit = function(r) return ray(r.Origin, r.Direction.Unit) end
raym.ClosestPoint = function(r, p)
	local d = r.Direction.Unit
	local t = max(0, (p - r.Origin):Dot(d))
	return r.Origin + d * t
end
raym.Distance = function(r, p) return (p - raym.ClosestPoint(r, p)).Magnitude end
Ray.new = function(o, d) return ray(o or Vector3.zero, d or Vector3.zero) end
G.Ray = freeze(Ray)

local RaycastParams = {}
local RPmt = { __type = "RaycastParams" }
RPmt.__index = function(_, key) error(tostring(key) .. " is not a valid member of RaycastParams", 2) end
RPmt.__newindex = function(_, key) error(tostring(key) .. " is not a valid member of RaycastParams", 2) end
freeze(RPmt)
RaycastParams.new = function()
	return setmt({
		FilterDescendantsInstances = {},
		FilterType = G.Enum.RaycastFilterType.Exclude,
		IgnoreWater = false,
		CollisionGroup = "Default",
		RespectCanCollide = false,
		BruteForceAllSlow = false,
	}, RPmt)
end
G.RaycastParams = freeze(RaycastParams)

-- Random: a seeded generator (mulberry32). Same seed, same numbers, but not
-- the same numbers Roblox's generator gives.
local Random = {}
local RNGmt = newType("Random", {}, {})
local states = setmt({}, { __mode = "k" })
local bxor, rshift, bor = bit32.bxor, bit32.rshift, bit32.bor
local TWO32 = 4294967296
local function imul(a, b)
	local ah, al = floor(a / 65536), a % 65536
	local bh, bl = floor(b / 65536), b % 65536
	return (al * bl + ((ah * bl + al * bh) % 65536) * 65536) % TWO32
end
local function nextFloat(rng)
	local s = (states[rng] + 0x6D2B79F5) % TWO32
	states[rng] = s
	local t = imul(bxor(s, rshift(s, 15)), bor(s, 1))
	t = bxor(t, (t + imul(bxor(t, rshift(t, 7)), bor(t, 61))) % TWO32)
	return bxor(t, rshift(t, 14)) / TWO32
end
local function newRandom(seed)
	local rng = freeze(setmt({}, RNGmt))
	states[rng] = floor(seed) % TWO32
	return rng
end
local rngMethods = {
	NextNumber = function(rng, lo, hi)
		lo, hi = num(lo, 0), num(hi, 1)
		return lo + nextFloat(rng) * (hi - lo)
	end,
	NextInteger = function(rng, lo, hi)
		lo, hi = num(lo), num(hi)
		return lo + floor(nextFloat(rng) * (hi - lo + 1))
	end,
	NextUnitVector = function(rng)
		local z = nextFloat(rng) * 2 - 1
		local a = nextFloat(rng) * 2 * math.pi
		local r = sqrt(1 - z * z)
		return v3(r * cos(a), r * sin(a), z)
	end,
	Clone = function(rng)
		local copy = freeze(setmt({}, RNGmt))
		states[copy] = states[rng]
		return copy
	end,
}
RNGmt.__index = function(_, key)
	local m = rngMethods[key]
	if m ~= nil then return m end
	error(tostring(key) .. " is not a valid member of Random", 2)
end
freeze(RNGmt)
Random.new = function(seed)
	if seed == nil then seed = os.clock() * 1e6 + math.random(0, 1e6) end
	return newRandom(num(seed))
end
G.Random = freeze(Random)

return G
)LUAU",
};

const std::string& bytecode() {
    static const std::string compiled = [] {
        std::string source;
        for (const char* part : kSourceParts) source += part;
        return Luau::compile(source);
    }();
    return compiled;
}

} // namespace

bool registerRobloxDatatypes(lua_State* L) {
    const std::string& code = bytecode();
    if (luau_load(L, "=RobloxDatatypes", code.data(), code.size(), 0) != 0) {
        std::fprintf(stderr, "RobloxDatatypes: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 1, 0) != 0) {
        std::fprintf(stderr, "RobloxDatatypes: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        lua_setglobal(L, lua_tostring(L, -2));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

} // namespace engine::core
