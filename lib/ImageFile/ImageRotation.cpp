#include "ImageRotation.h"
#include "ExifOrientation.h"

namespace ImageFile
{
    int orientationFitSteps(int imageWidth, int imageHeight, int screenWidth, int screenHeight, int fitSteps)
    {
        if (imageWidth <= 0 || imageHeight <= 0 || screenWidth <= 0 || screenHeight <= 0)
        {
            return 0;
        }

        // 正方形はどちらの向きに回しても収まり方が変わらない。
        // 「横長ではない＝縦長」と決めつけると、横長のパネル（このボードは 960×540）で
        // 正方形の画像が必ず 90 度回り、中身だけが横倒しになる。
        if (imageWidth == imageHeight || screenWidth == screenHeight)
        {
            return 0;
        }

        bool imageIsLandscape = imageWidth > imageHeight;
        bool screenIsLandscape = screenWidth > screenHeight;

        return (imageIsLandscape == screenIsLandscape) ? 0 : fitSteps;
    }

    int displayRotation(int orientation, int imageWidth, int imageHeight,
                        int baseRotation, int screenWidth, int screenHeight, int fitSteps)
    {
        int rotation = baseRotation + rotationStepsFor(orientation);

        if (imageWidth > 0 && imageHeight > 0)
        {
            // 画面に出たときの縦横。ここまでの回転が 90 度単位なら入れ替わる。
            bool swapped = (rotation & 1) != 0;
            int shownWidth = swapped ? imageHeight : imageWidth;
            int shownHeight = swapped ? imageWidth : imageHeight;

            // 比較は物理的な画面の向きで行う。
            // 画面を回しても本体の向きは変わらないので、画面側は入れ替えない。
            rotation += orientationFitSteps(shownWidth, shownHeight, screenWidth, screenHeight, fitSteps);
        }

        return rotation & 3;
    }
}
