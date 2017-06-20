///**************************************************************************
/// HEADER: Contains All Structures
///**************************************************************************

#ifndef STRUCTURES_H
#define STRUCTURES_H
#include "Main.h"

// Error types
enum Error
{
    noError,
    argumentError,
    parameterError,
    geometryError,
    consistencyError,
    NB_ERROR_VALUE
};

// Kernel Types
enum Kernel
{
    Gaussian,
    Bell_shaped,
    Cubic_spline,
    Quadratic,
    Quintic,
    Quintic_spline,
    NB_KERNEL_VALUE
};

// model of viscosity formulation
enum ViscosityModel
{
    violeauArtificial,
    NB_VISCOSITY_VALUE
};

// IntegrationMethod = euler ou RK2
enum IntegrationMethod
{
    euler,
    RK2,
    NB_INTEGRATION_VALUE
};

// AdaptativeTimeStep
enum AdaptativeTimeStep
{
    no,
    yes,
    NB_ADAPTATIVE_VALUE
};

// DensityInitMethod = hydrosatic, etc.
enum DensityInitMethod
{
    hydrostatic,
    homogeneous,
    NB_DENSITYINIT_VALUE
};

// StateEquationMethod = quasiIncompressible, perfectGas, etc.
enum StateEquationMethod
{
    quasiIncompressible,
    perfectGas,
    NB_STATEEQUATION_VALUE
};

// MassInitMethod = violeau2012 (all particles have same volumes), etc.
enum MassInitMethod
{
    violeau2012,
    NB_MASSINIT_VALUE
};

// PosLaw = Will dictate the behaviour of moving boundaries: constant, sine, exponential
enum PosLaw
{
    constant,
    sine,
    exponential,
    rotating,
    NB_SPEEDLAW_VALUE
};

// angleLaw = Will dictate the behaviour of moving boundaries: linear, sine, exponential
enum AngleLaw
{
    linearAngle,
    sineAngle,
    exponentialAngle,
    NB_ANGLELAW_VALUE
};

// Write Format output
enum Matlab
{
    noMatlab,
    fullMatlab,
    NB_MATLAB_VALUE
};

enum Paraview
{
    noParaview,
    fullParaview,
    nFreeParaview,
    nMovingFixedParaview,
    nFree_nMovingFixedParaview,
    NB_PARAVIEW_VALUE
};

// Particle type (Necessary to impose value here!)
enum ParticleType
{
    freePart = 0,
    fixedPart = 1,
    movingPart = 2
};

enum BathType
{
    dat = 0,
    txt = 1
};

struct Parameter
{
    double kh;
    double h;
    double k;
    double T;
    double densityRef;
    double B;
    double gamma;
    double g;
    double writeInterval;
    double c;
    double alpha;
    double beta;
    double epsilon;
    double molarMass;
    double temperature;
    double theta;
    double epsilonXSPH;
    Kernel kernel;
    ViscosityModel viscosityModel;
    IntegrationMethod integrationMethod;
    AdaptativeTimeStep adaptativeTimeStep;
    DensityInitMethod densityInitMethod;
    StateEquationMethod stateEquationMethod;
    MassInitMethod massInitMethod;
    std::vector<double> teta[3];
    std::vector<int> posLaw;
    std::vector<int> angleLaw;
    std::vector<double> charactTime;
    std::vector<double> movingDirection[3];
    std::vector<double> rotationCenter[3];
    std::vector<double> amplitude;
    Matlab matlab;
    Paraview paraview;
};

struct Field
{
    int nFree; 
    int nFixed; 
    int nMoving; 
    int nTotal;
    double l[3];
    double u[3];
    double nextK = 0.0;
    double currentTime = 0.0;
    std::vector<double> pos[3];
    std::vector<double> speed[3];
    std::vector<double> density;
    std::vector<double> pressure;
    std::vector<double> mass;
    std::vector<int> type;
};

struct SubdomainInfo
{
    int procID;
    int nTasks;
    int startingBox; 
    int endingBox;
    int startingParticle; 
    int endingParticle;
    double boxSize;
};

#endif
