#include "Main.h"
#include "Physics.h"
#include "Structures.h"
#define M_PI 3.14159265358979323846 /* pi */

// kernelGradientsPrecomputation
// Precomputes discrete values of the kernel gradient:
// -> discrete values : {0, kh/(resolution-1), 2*kh/(resolution-1), ..., kh}
void kernelGradPre(Kernel myKernel, int resolution, double kh,
                   std::vector<double> &kernelGradientsSamples)
{
    // Consistency verification
    assert(resolution > 1);
    // Loop on the values
    double r = 0.0;
    double increment = kh / ((double)resolution - 1.0);
    for (int i = 0; i < resolution; i++)
    {
        kernelGradientsSamples.push_back(gradWab(r, kh, myKernel));
        r += increment;
    }
}

// Gives the index of the closest sample value of the kernel gradient for a given resolution
// The index goes from 0 (r=0) to resolution-1 (r=kh)
int indexSamples(int resolution, double r, double kh)
{
    return round(r * (resolution - 1) / kh); // ATTENTION
}

// Smoothing function
// (For the gaussian kernel, kh is the size of the boxes)
double Wab(double r, double kh, Kernel myKernel)
{
    // Normalisation constant
    double alphaD;

    double h;

    switch (myKernel)
    {
    case Gaussian: // Gausian Kernel
        h = kh;
        alphaD = 1.0 / (pow(M_PI, 1.5) * h * h * h);
        return alphaD * exp(-(r / h) * (r / h));
        break;

    case Bell_shaped: // Bell-shaped Kernel
        h = kh;
        alphaD = 6.5625 / (M_PI * h * h * h);
        if (r < h)
            return alphaD * ((1.0 + 3.0 * (r / h)) * ((1.0 - (r / h)) * (1.0 - (r / h)) * (1.0 - (r / h))));
        else
            return 0.0;
        break;

    case Cubic_spline: // Cubic spline Kernel
        h = kh / 2.0;
        alphaD = 1.5 / (M_PI * h * h * h);
        if (r < h) // never negative...
            return alphaD * (1.5 - r * r / (h * h) + 0.5 * r * r * r / (h * h * h));
        else if (r < 2 * h)
            return alphaD * ((1.0 / 6.0) * (1.0 - (r / h)) * (1.0 - (r / h)) * (1.0 - (r / h)));
        else
            return 0.0;
        break;

    case Quadratic: // Quadratic Kernel
        h = kh / 2.0;
        alphaD = 1.25 / (M_PI * h * h * h);
        if (r < 2 * h)
            return alphaD * (0.0625 * r * r / (h * h) - 0.75 * (r / h) + 0.75);
        else
            return 0.0;
        break;

    case Quintic: // Quintic Kernel
        h = kh / 2.0;
        alphaD = 1.3125 / (M_PI * h * h * h);
        if (r < 2 * h)
            return alphaD * ((1 - 0.5 * (r / h)) * (1 - 0.5 * (r / h)) * (1 - 0.5 * (r / h)) * (1 - 0.5 * (r / h)) * (2 * r / h + 1));
        else
            return 0.0;
        break;

    case Quintic_spline: // Quintic spline Kernel
        h = kh / 3.0;
        alphaD = 3.0 / (359.0 * M_PI * h * h * h);
        if (r < h)
            return alphaD * (pow((3 - (r / h)), 5) - 6 * pow((2 - (r / h)), 5) + 15 * pow((1 - (r / h)), 5));
        else if (r < 2 * h)
            return alphaD * (pow((3 - (r / h)), 5) - 6 * pow((2 - (r / h)), 5));
        else if (r < 3 * h)
            return alphaD * (pow((3 - (r / h)), 5));
        else
            return 0.0;
        break;

    default:
        std::cout << "Non existing kernel.\n";
        return 0.0;
    }
}

// Derivative of the smoothing function
double gradWab(double r, double kh, Kernel myKernel)
{
    // Normalisation constant
    double alphaD;

    double h;

    switch (myKernel)
    {
    case Gaussian: // Gausian Kernel
        h = kh;
        alphaD = 1.0 / (pow(M_PI, 3.0 / 2.0) * h * h * h);
        return (alphaD / h) * (-2.0 * (r / h)) * exp(-(r / h) * (r / h));
        break;

    case Bell_shaped: // Bell-shaped Kernel
        h = kh;
        alphaD = 6.5625 / (M_PI * h * h * h);
        if (r < h)
            return (alphaD / h) * 3 * ((1 - (r / h)) * (1 - (r / h)) * (1 - (r / h)) - ((1 + 3 * (r / h)) * (1 - (r / h)) * (1 - (r / h))));
        else
            return 0.0;
        break;

    case Cubic_spline: // Cubic spline Kernel
        h = kh / 2.0;
        alphaD = 1.5 / (M_PI * h * h * h);
        if (r < h)
            return (alphaD / h) * (1.5 * (r / h) * (r / h) - 2 * r / h);
        else if (r < 2 * h)
            return (alphaD / h) * (-0.5 * (2.0 - (r / h)) * (2.0 - (r / h)));
        else
            return 0.0;
        break;

    case Quadratic: // Quadratic Kernel
        h = kh / 2.0;
        alphaD = 1.25 / (M_PI * h * h * h);
        if (r < 2 * h)
            return (alphaD / h) * (0.375 * (r / h) - 0.75);
        else
            return 0.0;
        break;

    case Quintic: // Quintic Kernel
        h = kh / 2.0;
        alphaD = 1.3125 / (M_PI * h * h * h);
        if (r < 2 * h)
            return (alphaD / h) * ((-5.0 * (r / h)) * (1 - 0.5 * (r / h)) * (1 - 0.5 * (r / h)) * (1 - 0.5 * (r / h)));
        else
            return 0.0;
        break;

    case Quintic_spline: // Quintic spline Kernel
        h = kh / 3.0;
        alphaD = 3.0 / (359.0 * M_PI * h * h * h);
        if (r < h)
            return (alphaD / h) * ((-5.0) * pow((3.0 - (r / h)), 4) + 30.0 * pow((2.0 - (r / h)), 4) - 75.0 * pow((1.0 - (r / h)), 4));
        else if (r < 2 * h)
            return (alphaD / h) * ((-5.0) * pow((3.0 - (r / h)), 4) + 30.0 * pow((2.0 - (r / h)), 4));
        else if (r < 3 * h)
            return (alphaD / h) * ((-5.0) * pow((3.0 - (r / h)), 4));
        else
            return 0.0;
        break;

    default:
        std::cout << "Non existing kernel.\n";
        return 0.0;
    }
}

double gethFromkh(Kernel kernelType, double kh)
{
    switch (kernelType)
    {
    case Gaussian: // Gausian Kernel
        return kh; // Attention, different signification for Gaussian!

    case Bell_shaped: // Bell-shaped Kernel
        return kh;

    case Cubic_spline: // Cubic spline Kernel
        return kh / 2.0;

    case Quadratic: // Quadratic Kernel
        return kh / 2.0;

    case Quintic: // Quintic Kernel
        return kh / 2.0;

    case Quintic_spline: // Quintic spline Kernel
        return kh / 3.0;

    default:
        std::cout << "Non existing kernel.\n";
        return 0.0;
    }
}
