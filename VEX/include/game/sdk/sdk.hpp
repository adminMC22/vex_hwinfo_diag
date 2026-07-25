#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <math.h>
#include "../../driver/driver_context.hpp"

#define HIDWORD(x) ((uint32_t)(((uint64_t)(x) >> 32) & 0xFFFFFFFF))

template<class T>  int16_t __PAIR__(int8_t  high, T low) { return (((int16_t)high) << sizeof(high) * 8) | uint8_t(low); }
template<class T>  int32_t __PAIR__(int16_t high, T low) { return (((int32_t)high) << sizeof(high) * 8) | uint16_t(low); }
template<class T>  int64_t __PAIR__(int32_t high, T low) { return (((int64_t)high) << sizeof(high) * 8) | uint32_t(low); }
template<class T> uint16_t __PAIR__(uint8_t  high, T low) { return (((uint16_t)high) << sizeof(high) * 8) | uint8_t(low); }
template<class T> uint32_t __PAIR__(uint16_t high, T low) { return (((uint32_t)high) << sizeof(high) * 8) | uint16_t(low); }
template<class T> uint64_t __PAIR__(uint32_t high, T low) { return (((uint64_t)high) << sizeof(high) * 8) | uint32_t(low); }

template<class T> T __ROL__(T value, int count)
{
    const std::uint32_t nbits = sizeof(T) * 8;

    if (count > 0)
    {
        count %= nbits;
        T high = value >> (nbits - count);
        if (T(-1) < 0)
            high &= ~((T(-1) << count));
        value <<= count;
        value |= high;
    }
    else
    {
        count = -count % nbits;
        T low = value << (nbits - count);
        value >>= count;
        value |= low;
    }
    return value;
}

inline std::uint8_t  __ROL1__(std::uint8_t  value, int count) { return __ROL__((std::uint8_t)value, count); }
inline std::uint16_t __ROL2__(std::uint16_t value, int count) { return __ROL__((std::uint16_t)value, count); }
inline std::uint32_t __ROL4__(std::uint32_t value, int count) { return __ROL__((std::uint32_t)value, count); }
inline std::uint64_t __ROL8__(std::uint64_t value, int count) { return __ROL__((std::uint64_t)value, count); }
inline std::uint8_t  __ROR1__(std::uint8_t  value, int count) { return __ROL__((std::uint8_t)value, -count); }
inline std::uint16_t __ROR2__(std::uint16_t value, int count) { return __ROL__((std::uint16_t)value, -count); }
inline std::uint32_t __ROR4__(std::uint32_t value, int count) { return __ROL__((std::uint32_t)value, -count); }
inline std::uint64_t __ROR8__(std::uint64_t value, int count) { return __ROL__((std::uint64_t)value, -count); }

namespace sky::game::sdk {

    // Forward declarations
    class UObject;
    class UClass;
    class UWorld;
    class ULevel;
    class AActor;
    class APlayerController;
    class APawn;
    class ACharacter;

    struct FUObjectItem
    {
        // Pointer to the allocated object
        uintptr_t Object;
        // Internal flags
        int32_t Flags;
        // UObject Owner Cluster Index
        int32_t ClusterRootIndex;
        // Weak Object Pointer Serial number associated with the object
        int32_t SerialNumber;
        uintptr_t StatID;
    };

    struct FChunkedFixedUObjectArray
    {
        /** Master table to chunks of pointers **/
        FUObjectItem** Objects;
        /** If requested, a contiguous memory where all objects are allocated **/
        FUObjectItem* PreAllocatedObjects;
        /** Maximum number of elements **/
        int32_t MaxElements;
        /** Number of elements we currently have **/
        int32_t NumElements;
        /** Maximum number of chunks **/
        int32_t MaxChunks;
        /** Number of chunks we currently have **/
        int32_t NumChunks;
    };

    typedef FChunkedFixedUObjectArray TypeUObjectArray;

    /**
     * @brief Base structure for Unreal Engine objects
     */
    struct FPointer {
        uintptr_t Dummy;
    };

    /**
     * @brief Structure for 3D vectors (UE5 uses double)
     */
    struct FVector {
        double X, Y, Z;

        FVector() : X(0.0), Y(0.0), Z(0.0) {}

        FVector(double X, double Y, double Z) : X(X), Y(Y), Z(Z) {}

        FVector(double InF) : X(InF), Y(InF), Z(InF) {}

        FVector operator+(const FVector& other) const { return FVector(X + other.X, Y + other.Y, Z + other.Z); }

        FVector operator-(const FVector& other) const { return FVector(X - other.X, Y - other.Y, Z - other.Z); }

        FVector operator*(const FVector& V) const { return FVector(X * V.X, Y * V.Y, Z * V.Z); }

        FVector operator/(const FVector& V) const { return FVector(X / V.X, Y / V.Y, Z / V.Z); }

        bool operator==(const FVector& V) const { return X == V.X && Y == V.Y && Z == V.Z; }

        bool operator!=(const FVector& V) const { return X != V.X || Y != V.Y || Z != V.Z; }

        FVector operator-() const { return FVector(-X, -Y, -Z); }

        FVector operator+(double Bias) const { return FVector(X + Bias, Y + Bias, Z + Bias); }

        FVector operator-(double Bias) const { return FVector(X - Bias, Y - Bias, Z - Bias); }

        FVector operator*(double Scale) const { return FVector(X * Scale, Y * Scale, Z * Scale); }

        FVector operator/(double Scale) const { const double RScale = 1.0 / Scale; return FVector(X * RScale, Y * RScale, Z * RScale); }

        FVector operator=(const FVector& V) { X = V.X; Y = V.Y; Z = V.Z; return *this; }

        FVector operator+=(const FVector& V) { X += V.X; Y += V.Y; Z += V.Z; return *this; }

        FVector operator-=(const FVector& V) { X -= V.X; Y -= V.Y; Z -= V.Z; return *this; }

        FVector operator*=(const FVector& V) { X *= V.X; Y *= V.Y; Z *= V.Z; return *this; }

        FVector operator/=(const FVector& V) { X /= V.X; Y /= V.Y; Z /= V.Z; return *this; }

        FVector operator*=(double Scale) { X *= Scale; Y *= Scale; Z *= Scale; return *this; }

        FVector operator/=(double V) { const double RV = 1.0 / V; X *= RV; Y *= RV; Z *= RV; return *this; }

        double operator|(const FVector& V) const { return X * V.X + Y * V.Y + Z * V.Z; }

        FVector operator^(const FVector& V) const { return FVector(Y * V.Z - Z * V.Y, Z * V.X - X * V.Z, X * V.Y - Y * V.X); }

        __forceinline double SiZeSquared() const {
            return X * X + Y * Y + Z * Z;
        }

        __forceinline double SizeSquared2D() const {
            return X * X + Y * Y;
        }

        __forceinline double Magnitude() const
        {
            return (double)sqrtf(X * X + Y * Y + Z * Z);
        }

        __forceinline double Dot(const FVector& vOther) const {
            const FVector& a = *this;

            return (a.X * vOther.X + a.Y * vOther.Y + a.Z * vOther.Z);
        }

        double Distance(FVector v)
        {
            double dx = (double)(this->X - v.X);
            double dy = (double)(this->Y - v.Y);
            double dz = (double)(this->Z - v.Z);
            double distance_units = sqrt((dx * dx) + (dy * dy) + (dz * dz));
            return distance_units;
        }

        FVector Normalize()
        {
            FVector vector;
            double length = this->Magnitude();

            if (length != 0)
            {
                vector.X = X / length;
                vector.Y = Y / length;
                vector.Z = Z / length;
            }
            else
            {
                vector.X = vector.Y = 0.0;
                vector.Z = 1.0;
            }
            return vector;
        }

        inline double Dot(FVector v)
        {
            return X * v.X + Y * v.Y + Z * v.Z;
        }
    };

    /**
     * @brief Structure for 2D vectors (UE5 uses double)
	 */
    struct FVector2D {
        double                                             X;                                                        // 0x0000(0x0008) (Edit, BlueprintVisible, ZeroConstructor, SaveGame, IsPlainOldData)
        double                                             Y;                                                        // 0x0008(0x0008) (Edit, BlueprintVisible, ZeroConstructor, SaveGame, IsPlainOldData)

        inline FVector2D()
            : X(0), Y(0)
        {
        }

        inline FVector2D(double x, double y)
            : X(x),
            Y(y)
        {
        }

        __forceinline FVector2D operator-(const FVector2D& V) {
            return FVector2D(X - V.X, Y - V.Y);
        }

        __forceinline FVector2D operator+(const FVector2D& V) {
            return FVector2D(X + V.X, Y + V.Y);
        }

        __forceinline FVector2D operator*(double Scale) const {
            return FVector2D(X * Scale, Y * Scale);
        }

        __forceinline FVector2D operator/(double Scale) const {
            const double RScale = 1.0 / Scale;
            return FVector2D(X * RScale, Y * RScale);
        }

        __forceinline FVector2D operator+(double A) const {
            return FVector2D(X + A, Y + A);
        }

        __forceinline FVector2D operator-(double A) const {
            return FVector2D(X - A, Y - A);
        }

        __forceinline FVector2D operator*(const FVector2D& V) const {
            return FVector2D(X * V.X, Y * V.Y);
        }

        __forceinline FVector2D operator/(const FVector2D& V) const {
            return FVector2D(X / V.X, Y / V.Y);
        }

        __forceinline double operator|(const FVector2D& V) const {
            return X * V.X + Y * V.Y;
        }

        __forceinline double operator^(const FVector2D& V) const {
            return X * V.Y - Y * V.X;
        }

        __forceinline FVector2D& operator+=(const FVector2D& v) {
            X += v.X;
            Y += v.Y;
            return *this;
        }

        __forceinline FVector2D& operator-=(const FVector2D& v) {
            X -= v.X;
            Y -= v.Y;
            return *this;
        }

        __forceinline FVector2D& operator*=(const FVector2D& v) {
            X *= v.X;
            Y *= v.Y;
            return *this;
        }

        __forceinline FVector2D& operator/=(const FVector2D& v) {
            X /= v.X;
            Y /= v.Y;
            return *this;
        }

        friend FVector2D operator*(double scalar, const FVector2D& V) {
            return FVector2D(scalar * V.X, scalar * V.Y);
        }

        __forceinline bool operator==(const FVector2D& src) const {
            return (src.X == X) && (src.Y == Y);
        }

        __forceinline bool operator!=(const FVector2D& src) const {
            return (src.X != X) || (src.Y != Y);
        }

        __forceinline double SizeSquared() const {
            return X * X + Y * Y;
        }

        __forceinline double Dot(const FVector2D& vOther) const {
            const FVector2D& a = *this;

            return (a.X * vOther.X + a.Y * vOther.Y);
        }

        bool isValid() const {
            return !isnan(X) && !isnan(Y) && !isinf(X) && !isinf(Y) && (X > 0 || Y > 0);
        }
	};

    /**
     * @brief Structure for rotation (UE5 uses double)
     */
    struct FRotator {
        double Pitch;
        double Yaw;
        double Roll;

        inline FRotator() : Pitch(0), Yaw(0), Roll(0) {}

        inline FRotator(double x, double y, double z) : Pitch(x), Yaw(y), Roll(z) {}

        FRotator operator+(const FRotator& V) {
            return FRotator(Pitch + V.Pitch, Yaw + V.Yaw, Roll + V.Roll);
        }

        FRotator operator-(const FRotator& V) {
            return FRotator(Pitch - V.Pitch, Yaw - V.Yaw, Roll - V.Roll);
        }

        FRotator operator*(double Scale) const {
            return FRotator(Pitch * Scale, Yaw * Scale, Roll * Scale);
        }

        FRotator operator/(double Scale) const {
            const double RScale = 1.0 / Scale;
            return FRotator(Pitch * RScale, Yaw * RScale, Roll * RScale);
        }

        FRotator operator+(double A) const {
            return FRotator(Pitch + A, Yaw + A, Roll + A);
        }

        FRotator operator+(FRotator A) const {
            return FRotator(Pitch + A.Pitch, Yaw + A.Yaw, Roll + A.Roll);
        }

        FRotator operator-(double A) const {
            return FRotator(Pitch - A, Yaw - A, Roll - A);
        }

        FRotator operator-(FRotator A) const {
            return FRotator(Pitch - A.Pitch, Yaw - A.Yaw, Roll - A.Roll);
        }

        FRotator operator*(const FRotator& V) const {
            return FRotator(Pitch * V.Pitch, Yaw * V.Yaw, Roll * V.Roll);
        }

        FRotator operator/(const FRotator& V) const {
            return FRotator(Pitch / V.Pitch, Yaw / V.Yaw, Roll / V.Roll);
        }

        double operator|(const FRotator& V) const {
            return Pitch * V.Pitch + Yaw * V.Yaw + Roll * V.Roll;
        }

        FRotator& operator+=(const FRotator& v) {
            Pitch += v.Pitch;
            Yaw += v.Yaw;
            Roll += v.Roll;
            return *this;
        }

        FRotator& operator-=(const FRotator& v) {
            Pitch -= v.Pitch;
            Yaw -= v.Yaw;
            Roll -= v.Roll;
            return *this;
        }

        FRotator& operator*=(const FRotator& v) {
            Pitch *= v.Pitch;
            Yaw *= v.Yaw;
            Roll *= v.Roll;
            return *this;
        }

        FRotator& operator/=(const FRotator& v) {
            Pitch /= v.Pitch;
            Yaw /= v.Yaw;
            Roll /= v.Roll;
            return *this;
        }

        double operator^(const FRotator& V) const {
            return Pitch * V.Yaw - Yaw * V.Pitch - Roll * V.Roll;
        }

        bool operator==(const FRotator& src) const {
            return (src.Pitch == Pitch) && (src.Yaw == Yaw) && (src.Roll == Roll);
        }

        bool operator!=(const FRotator& src) const {
            return (src.Pitch != Pitch) || (src.Yaw != Yaw) || (src.Roll != Roll);
        }

        double Size() const {
            return sqrtf(Pitch * Pitch + Yaw * Yaw + Roll * Roll);
        }

        double SizeSquared() const {
            return Pitch * Pitch + Yaw * Yaw + Roll * Roll;
        }

        double Dot(const FRotator& vOther) const {
            const FRotator& a = *this;
            return (a.Pitch * vOther.Pitch + a.Yaw * vOther.Yaw + a.Roll * vOther.Roll);
        }

        double ClampAxis(double Angle) {
            // Angles 0..360
            Angle = fmod(Angle, 360.0);

            if (Angle < 0.0) {
                Angle += 360.0;
            }

            return Angle;
        }

        double NormalizeAxis(double Angle) {
            // returns Angle in the range (-180,180]
            Angle = ClampAxis(Angle);

            if (Angle > 180.0) {
                Angle -= 360.0;
            }

            return Angle;
        }

        void NormalizeAngles()
        {
            if (Pitch > 89.0) Pitch -= 360.0;
            else if (Pitch < -89.0) Pitch += 360.0;

            while (Yaw > 180.0)Yaw -= 360.0;
            while (Yaw < -180.0)Yaw += 360.0;
            Roll = 0;
        }

        void Normalize() {
            Pitch = NormalizeAxis(Pitch);
            Yaw = NormalizeAxis(Yaw);
            Roll = NormalizeAxis(Roll);
        }

        FRotator GetNormalized() const {
            FRotator Rot = *this;
            Rot.Normalize();
            return Rot;
        }

        FVector ToVector() const {
            return FVector(Pitch, Yaw, Roll);
        }
    };

    /**
     * @brief Structure for plane (UE5 uses double)
     */
    struct alignas(16) FPlane : public FVector {
        double W;
    };

    /**
     * @brief Structure for quaternion (UE5 uses double)
     */
    struct FQuat {
        double X;
        double Y;
        double Z;
        double W;

        FQuat() : X(0.0), Y(0.0), Z(0.0), W(1.0) {}
        FQuat(double x, double y, double z, double w) : X(x), Y(y), Z(z), W(w) {}
    };

    struct FMatrix
    {
        FPlane                                      XPlane;                                                   // 0x0000(0x0010) (Edit, BlueprintVisible, SaveGame, IsPlainOldData)
        FPlane                                      YPlane;                                                   // 0x0010(0x0010) (Edit, BlueprintVisible, SaveGame, IsPlainOldData)
        FPlane                                      ZPlane;                                                   // 0x0020(0x0010) (Edit, BlueprintVisible, SaveGame, IsPlainOldData)
        FPlane                                      WPlane;                                                   // 0x0030(0x0010) (Edit, BlueprintVisible, SaveGame, IsPlainOldData)

        FMatrix operator*(const FMatrix& pM2)
        {
            FMatrix pOut;
            pOut.XPlane.X = XPlane.X * pM2.XPlane.X + XPlane.Y * pM2.YPlane.X + XPlane.Z * pM2.ZPlane.X + XPlane.W * pM2.WPlane.X;
            pOut.XPlane.Y = XPlane.X * pM2.XPlane.Y + XPlane.Y * pM2.YPlane.Y + XPlane.Z * pM2.ZPlane.Y + XPlane.W * pM2.WPlane.Y;
            pOut.XPlane.Z = XPlane.X * pM2.XPlane.Z + XPlane.Y * pM2.ZPlane.Z + XPlane.Z * pM2.ZPlane.Z + XPlane.W * pM2.WPlane.Z;
            pOut.XPlane.W = XPlane.X * pM2.XPlane.W + XPlane.Y * pM2.YPlane.W + XPlane.Z * pM2.ZPlane.W + XPlane.W * pM2.WPlane.W;
            pOut.YPlane.X = YPlane.X * pM2.XPlane.X + YPlane.Y * pM2.YPlane.X + YPlane.Z * pM2.ZPlane.X + YPlane.W * pM2.WPlane.X;
            pOut.YPlane.Y = YPlane.X * pM2.XPlane.Y + YPlane.Y * pM2.YPlane.Y + YPlane.Z * pM2.ZPlane.Y + YPlane.W * pM2.WPlane.Y;
            pOut.YPlane.Z = YPlane.X * pM2.XPlane.Z + YPlane.Y * pM2.ZPlane.Z + YPlane.Z * pM2.ZPlane.Z + YPlane.W * pM2.WPlane.Z;
            pOut.YPlane.W = YPlane.X * pM2.XPlane.W + YPlane.Y * pM2.YPlane.W + YPlane.Z * pM2.ZPlane.W + YPlane.W * pM2.WPlane.W;
            pOut.ZPlane.X = ZPlane.X * pM2.XPlane.X + ZPlane.Y * pM2.YPlane.X + ZPlane.Z * pM2.ZPlane.X + ZPlane.W * pM2.WPlane.X;
            pOut.ZPlane.Y = ZPlane.X * pM2.XPlane.Y + ZPlane.Y * pM2.YPlane.Y + ZPlane.Z * pM2.ZPlane.Y + ZPlane.W * pM2.WPlane.Y;
            pOut.ZPlane.Z = ZPlane.X * pM2.XPlane.Z + ZPlane.Y * pM2.ZPlane.Z + ZPlane.Z * pM2.ZPlane.Z + ZPlane.W * pM2.WPlane.Z;
            pOut.ZPlane.W = ZPlane.X * pM2.XPlane.W + ZPlane.Y * pM2.YPlane.W + ZPlane.Z * pM2.ZPlane.W + ZPlane.W * pM2.WPlane.W;
            pOut.WPlane.X = WPlane.X * pM2.XPlane.X + WPlane.Y * pM2.YPlane.X + WPlane.Z * pM2.ZPlane.X + WPlane.W * pM2.WPlane.X;
            pOut.WPlane.Y = WPlane.X * pM2.XPlane.Y + WPlane.Y * pM2.YPlane.Y + WPlane.Z * pM2.ZPlane.Y + WPlane.W * pM2.WPlane.Y;
            pOut.WPlane.Z = WPlane.X * pM2.XPlane.Z + WPlane.Y * pM2.ZPlane.Z + WPlane.Z * pM2.ZPlane.Z + WPlane.W * pM2.WPlane.Z;
            pOut.WPlane.W = WPlane.X * pM2.XPlane.W + WPlane.Y * pM2.YPlane.W + WPlane.Z * pM2.ZPlane.W + WPlane.W * pM2.WPlane.W;
            return pOut;
        }
    };

    /**
     * @brief Structure for transformation (UE5 uses double)
     */
    struct FTransform {
        struct FQuat                                       Rotation;                                                 // 00000(0x0010) (Edit, BlueprintVisible, SaveGame, IsPlainOldData)
        struct FVector                                     Translation;                                              // 0x0010(0x000C) (Edit, BlueprintVisible, SaveGame, IsPlainOldData)
        unsigned char                                      UnknownData00[0x4];                                       // 0x001C(0x0004) MISSED OFFSET
        struct FVector                                     Scale3D;                                                  // 0x0020(0x000C) (Edit, BlueprintVisible, SaveGame, IsPlainOldData)
        unsigned char                                      UnknownData01[0x4];                                       // 0x002C(0x0004) MISSED OFFSET

        FMatrix ToMatrixWithScale()
        {
            FMatrix m;
            m.WPlane.X = Translation.X;
            m.WPlane.Y = Translation.Y;
            m.WPlane.Z = Translation.Z;

            double x2 = Rotation.X + Rotation.X;
            double y2 = Rotation.Y + Rotation.Y;
            double z2 = Rotation.Z + Rotation.Z;

            double xx2 = Rotation.X * x2;
            double yy2 = Rotation.Y * y2;
            double zz2 = Rotation.Z * z2;
            m.XPlane.X = (1.0 - (yy2 + zz2)) * Scale3D.X;
            m.YPlane.Y = (1.0 - (xx2 + zz2)) * Scale3D.X;
            m.ZPlane.Z = (1.0 - (xx2 + yy2)) * Scale3D.X;

            double yz2 = Rotation.Y * z2;
            double wx2 = Rotation.W * x2;
            m.ZPlane.Y = (yz2 - wx2) * Scale3D.Z;
            m.YPlane.Z = (yz2 + wx2) * Scale3D.Y;

            double xy2 = Rotation.X * y2;
            double wz2 = Rotation.W * z2;
            m.YPlane.X = (xy2 - wz2) * Scale3D.Y;
            m.XPlane.Y = (xy2 + wz2) * Scale3D.X;

            double xz2 = Rotation.X * z2;
            double wy2 = Rotation.W * y2;
            m.ZPlane.X = (xz2 + wy2) * Scale3D.Z;
            m.XPlane.Z = (xz2 - wy2) * Scale3D.X;

            m.XPlane.W = 0.0;
            m.YPlane.W = 0.0;
            m.ZPlane.W = 0.0;
            m.WPlane.W = 1.0;

            return m;
        }
    };

    /**
     * @brief Structure for 4x4 matrix (UE5 uses double)
     */
    struct D3DMatrix {
        union {
            struct {
                double        _11, _12, _13, _14;
                double        _21, _22, _23, _24;
                double        _31, _32, _33, _34;
                double        _41, _42, _43, _44;

            };
            double m[4][4];
        };

        D3DMatrix() {
            memset(m, 0, sizeof(m));
		}
    };

    struct FMinimalViewInfo
    {
        FVector                                            Location;                                                   // 0x0000   (0x0018)
        FRotator                                           Rotation;                                                   // 0x0018   (0x0018)
        float                                              FOV;                                                        // 0x0030   (0x0004)
        float                                              DesiredFOV;                                                 // 0x0034   (0x0004)
        float                                              OrthoWidth;                                                 // 0x0038   (0x0004)
        float                                              OrthoNearClipPlane;                                         // 0x003C   (0x0004)
        float                                              OrthoFarClipPlane;                                          // 0x0040   (0x0004)
        float                                              PerspectiveNearClipPlane;                                   // 0x0044   (0x0004)
        float                                              AspectRatio;                                                // 0x0048   (0x0004)
    };

    struct FCameraCacheEntry
    {
        float                                              Timestamp;                                                  // 0x0000   (0x0004)
        unsigned char                                      UnknownData00_6[0xC];                                       // 0x0004   (0x000C)  MISSED
        FMinimalViewInfo                                   POV;                                                        // 0x0010   (0x08E0)
    };

    /**
     * @brief Structure for name
     */
    struct FName {
        int32_t ComparisonIndex;
        int32_t Number;

        FName() : ComparisonIndex(0), Number(0) {}
        FName(int32_t index, int32_t number = 0) : ComparisonIndex(index), Number(number) {}

        inline bool operator==(const FName& other) const
        {
            return ComparisonIndex == other.ComparisonIndex;
        };
    };

    struct FNameEntryHandle
    {
        uint16_t bIsWide : 1;
        uint16_t Len : 15;
    };

    struct FNameEntry
    {
        int32_t ComparisonId;
        FNameEntryHandle Header;
        union
        {
            char        AnsiName[1024];
            wchar_t     WideName[1024];
        };
        wchar_t const* GetWideName() const { return WideName; }
        char const* GetAnsiName() const { return AnsiName; }
        bool IsWide() const { return Header.bIsWide; }

    };

    /**
     * @brief Structure for string
     */
    struct FString {
        wchar_t* Data;
        int32_t ArrayNum;
        int32_t ArrayMax;

        FString() : Data(nullptr), ArrayNum(0), ArrayMax(0) {}
    };

    /**
     * @brief Structure for array
     */
    template<typename T>
    struct TArray {
        T* Data;
        int32_t Count;
        int32_t Max;

        TArray() : Data(nullptr), Count(0), Max(0) {}

        template<typename U = T, typename std::enable_if<std::is_constructible<U, uintptr_t>::value, int>::type = 0>
        U operator[](int32_t index) const {
            if (!Data || index < 0 || index >= Count) return U{};
            auto element_ptr = sky::driver::g_driver->read<uintptr_t>(reinterpret_cast<uintptr_t>(Data) + static_cast<uintptr_t>(index) * sizeof(uintptr_t));
            return U(element_ptr);
        }

        template<typename U = T, typename std::enable_if<!std::is_constructible<U, uintptr_t>::value, int>::type = 0>
        U operator[](int32_t index) const {
            if (!Data || index < 0 || index >= Count) return U{};
            return sky::driver::g_driver->read<U>(reinterpret_cast<uintptr_t>(Data) + static_cast<uintptr_t>(index) * sizeof(U));
        }

        // Method for batch reading - much more efficient for multiple elements
        template<typename U = T, typename std::enable_if<!std::is_constructible<U, uintptr_t>::value, int>::type = 0>
        std::vector<U> ReadAllBatch() const {
            if (!Data || Count <= 0) return {};
            return sky::driver::g_driver->read_array_batch<U>(reinterpret_cast<uintptr_t>(Data), Count);
        }

        // Method for batch reading a specific range
        template<typename U = T, typename std::enable_if<!std::is_constructible<U, uintptr_t>::value, int>::type = 0>
        std::vector<U> ReadRangeBatch(int32_t start_index, int32_t count) const {
            if (!Data || start_index < 0 || start_index >= Count || count <= 0) return {};

            int32_t actual_count = std::min(count, Count - start_index);
            uintptr_t start_ptr = reinterpret_cast<uintptr_t>(Data) + static_cast<uintptr_t>(start_index) * sizeof(U);

            return sky::driver::g_driver->read_array_batch<U>(start_ptr, actual_count);
        }

        int32_t Num() const { return Count; }
        bool IsValidIndex(int32_t index) const { return index >= 0 && index < Count; }
    };

    /**
     * @brief Structure for map
     */
    template<typename KeyType, typename ValueType>
    struct TMap {
        struct TPair {
            KeyType Key;
            ValueType Value;
        };

        TPair* Data;
        int32_t Count;
        int32_t Max;

        TMap() : Data(nullptr), Count(0), Max(0) {}
    };

    /**
     * @brief Structure for set
     */
    template<typename T>
    struct TSet {
        T* Data;
        int32_t Count;
        int32_t Max;

        TSet() : Data(nullptr), Count(0), Max(0) {}
    };

    /**
     * @brief Structure for weak pointer
     */
    template<typename T>
    struct TWeakObjectPtr {
        int32_t ObjectIndex;
        int32_t ObjectSerialNumber;

        TWeakObjectPtr() : ObjectIndex(0), ObjectSerialNumber(0) {}
    };

    /**
     * @brief Structure for shared pointer
     */
    template<typename T>
    struct TSharedPtr {
        T* Object;
        void* SharedReferenceCount;

        TSharedPtr() : Object(nullptr), SharedReferenceCount(nullptr) {}
    };

    /**
     * @brief Structure for unique pointer
     */
    template<typename T>
    struct TUniquePtr {
        T* Object;

        TUniquePtr() : Object(nullptr) {}
    };

    /**
     * @brief Structure for delegate
     */
    template<typename... Args>
    struct FDelegate {
        void* Object;
        void* Function;

        FDelegate() : Object(nullptr), Function(nullptr) {}
    };

    /**
     * @brief Structure for multicast delegate
     */
    template<typename... Args>
    struct FMulticastDelegate {
        TArray<FDelegate<Args...>> InvocationList;

        FMulticastDelegate() : InvocationList() {}
    };

    // Common constants (UE5 uses double)
    namespace Constants {
        constexpr double PI = 3.14159265358979323846264338327950288419716939937510;
        constexpr double HALF_PI = PI * 0.5;
        constexpr double TWO_PI = PI * 2.0;
        constexpr double DEG_TO_RAD = PI / 180.0;
        constexpr double RAD_TO_DEG = 180.0 / PI;
        constexpr double HALF_DEG_TO_RAD = PI / 360.0;

        constexpr double SMALL_NUMBER = 1.e-8;
        constexpr double KINDA_SMALL_NUMBER = 1.e-4;
        constexpr double BIG_NUMBER = 1.7976931348623157e+308;

        constexpr int32_t INDEX_NONE = -1;
        constexpr uint32_t MAX_uint32 = 0xFFFFFFFF;
        constexpr int32_t MAX_int32 = 0x7FFFFFFF;
        constexpr int32_t MIN_int32 = 0x80000000;
    }

    // Utility functions (UE5 uses double)
    namespace Math {
        template<typename T>
        T Clamp(T value, T min, T max) {
            return (value < min) ? min : (value > max) ? max : value;
        }

        template<typename T>
        T Lerp(T a, T b, double alpha) {
            return a + (b - a) * alpha;
        }

        inline D3DMatrix toMatrix(FRotator Rotation, FVector origin)
        {
            double Pitch = ((double)Rotation.Pitch * double(Constants::DEG_TO_RAD));
            double Yaw = ((double)Rotation.Yaw * double(Constants::DEG_TO_RAD));
            double Roll = ((double)Rotation.Roll * double(Constants::DEG_TO_RAD));

            double SP = sinf(Pitch);
            double CP = cosf(Pitch);
            double SY = sinf(Yaw);
            double CY = cosf(Yaw);
            double SR = sinf(Roll);
            double CR = cosf(Roll);

            D3DMatrix Matrix;
            Matrix._11 = CP * CY;
            Matrix._12 = CP * SY;
            Matrix._13 = SP;
            Matrix._14 = 0.;

            Matrix._21 = SR * SP * CY - CR * SY;
            Matrix._22 = SR * SP * SY + CR * CY;
            Matrix._23 = -SR * CP;
            Matrix._24 = 0.;

            Matrix._31 = -(CR * SP * CY + SR * SY);
            Matrix._32 = CY * SR - CR * SP * SY;
            Matrix._33 = CR * CP;
            Matrix._34 = 0.;

            Matrix._41 = origin.X;
            Matrix._42 = origin.Y;
            Matrix._43 = origin.Z;
            Matrix._44 = 1.;

            return Matrix;
        }

        double FInterpTo(double current, double target, double deltaTime, double interpSpeed);
        FVector VInterpTo(const FVector& current, const FVector& target, double deltaTime, double interpSpeed);
        FRotator RInterpTo(const FRotator& current, const FRotator& target, double deltaTime, double interpSpeed);

        bool IsNearlyEqual(double a, double b, double tolerance = Constants::KINDA_SMALL_NUMBER);
        bool IsNearlyZero(double value, double tolerance = Constants::KINDA_SMALL_NUMBER);

        double FMod(double x, double y);
        double FTrunc(double value);
        double FFloor(double value);
        double FCeil(double value);
        double FRound(double value);

        FVector GetForwardVector(const FRotator& rotation);
        FVector GetRightVector(const FRotator& rotation);
        FVector GetUpVector(const FRotator& rotation);

        FRotator MakeRotFromX(const FVector& forward);
        FRotator MakeRotFromY(const FVector& right);
        FRotator MakeRotFromZ(const FVector& up);

        double Dist(const FVector& a, const FVector& b);
        double DistSquared(const FVector& a, const FVector& b);
        double Dist2D(const FVector& a, const FVector& b);
        double DistSquared2D(const FVector& a, const FVector& b);
    }

} // namespace sky::game::sdk