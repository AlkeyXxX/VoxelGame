
#include "VoxelMesher.h"
#include "VoxelTypes.h"


namespace
{
    /*
     * Маленький отступ от верхней границы блока.
     *
     * Большой отступ создавал треугольные просветы у берега:
     * MC-поверхность могла оказаться чуть выше водной плоскости.
     * 2% сохраняют аккуратный край, но почти полностью убирают
     * этот зазор.
     */
    constexpr float WaterSurfaceInsetFraction = 0.02f;

    FORCEINLINE bool IsFaceVisibleInternal(uint8 Block, uint8 NeighborBlock)
    {
        const bool bWater = Block == uint8(EVoxelBlock::Water);

        if (bWater)
        {
            /*
             * Water only renders faces toward open air.
             * Faces against solid terrain/structures are internal surfaces.
             */
            return NeighborBlock == uint8(EVoxelBlock::Air);
        }

        return !IsVoxelSolid(NeighborBlock);
    }

    FORCEINLINE int32 BlockIndex(
        int32 X,
        int32 Y,
        int32 Z,
        int32 Size)
    {
        return X + Y * Size + Z * Size * Size;
    }


    FORCEINLINE int32 SideIndex(
        int32 A,
        int32 B,
        int32 Size)
    {
        return A + B * Size;
    }

    /*
     * Отдельная непрерывная Water Surface.
     *
     * Для каждого столбца находим верхний water block.
     * Если соседний столбец на этом уровне занят твёрдым
     * берегом, поверхность слегка заходит под берег.
     * Это перекрывает геометрический клин между плоской водой
     * и плавным MC-склоном, не поднимая воду над берегом.
     */
    const float SurfaceInset =
        0.02f;

    const float SurfaceZOffset =
        VoxelSize * (1.0f - SurfaceInset);

    const float ShoreExtension =
        VoxelSize * 0.18f;

    for (int32 Y = 0; Y < Size; ++Y)
    {
        for (int32 X = 0; X < Size; ++X)
        {
            int32 TopWaterZ = -1;

            for (int32 Z = Size - 1; Z >= 0; --Z)
            {
                const uint8 Block =
                    Input.Blocks[
                        BlockIndex(X, Y, Z, Size)];

                if (Block != uint8(EVoxelBlock::Water))
                {
                    continue;
                }

                const uint8 Above =
                    GetCubicNeighborBlock(
                        Input,
                        X,
                        Y,
                        Z + 1,
                        true);

                if (Above == uint8(EVoxelBlock::Air))
                {
                    TopWaterZ = Z;
                    break;
                }
            }

            if (TopWaterZ < 0)
            {
                continue;
            }

            const FLinearColor WaterColor(
                0.05f,
                0.35f,
                0.85f,
                1.0f);

            const auto IsSolidShore =
                [&](int32 NX, int32 NY)
                {
                    const uint8 Neighbor =
                        GetCubicNeighborBlock(
                            Input,
                            NX,
                            NY,
                            TopWaterZ,
                            true);

                    return IsVoxelSolid(Neighbor);
                };

            const float X0 =
                IsSolidShore(X - 1, Y)
                    ? -ShoreExtension
                    : 0.0f;

            const float X1 =
                IsSolidShore(X + 1, Y)
                    ? VoxelSize + ShoreExtension
                    : VoxelSize;

            const float Y0 =
                IsSolidShore(X, Y - 1)
                    ? -ShoreExtension
                    : 0.0f;

            const float Y1 =
                IsSolidShore(X, Y + 1)
                    ? VoxelSize + ShoreExtension
                    : VoxelSize;

            const float SurfaceZ =
                TopWaterZ * VoxelSize +
                SurfaceZOffset;

            AddWaterSurfaceQuad(
                Output,
                FVector(
                    X * VoxelSize + X0,
                    Y * VoxelSize + Y0,
                    SurfaceZ),
                FVector(
                    X * VoxelSize + X1,
                    Y * VoxelSize + Y0,
                    SurfaceZ),
                FVector(
                    X * VoxelSize + X1,
                    Y * VoxelSize + Y1,
                    SurfaceZ),
                FVector(
                    X * VoxelSize + X0,
                    Y * VoxelSize + Y1,
                    SurfaceZ),
                WaterColor,
                Input.WorldOrigin,
                VoxelSize);
        }
    }
}
