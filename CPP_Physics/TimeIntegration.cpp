///**************************************************************************
/// SOURCE: Function to integrate one time step.
///**************************************************************************
#include "Main.h"
#include "Physics.h"
#include "Tools.h"

/*
*Input:
*- currentField: field that contains all the information at time t
*- nextField: field in which results of step n are stored
*- midField: field that contains all the information at mid time
*- parameter: pointer to the field containing the user defined parameters
*- currentDensityDerivative: vector containing derivative of density for each particle at time t
*- currentSpeedDerivative: vector containing derivative of velocity for each particle at time t
*- midDensityDerivative: vector containing derivative of density for each particle at mid time
*- midSpeedDerivative: vector containing derivative of velocity for each particle at mid time
*- t: current simulation time
*- k: timestep
*/
void RK2Update(Field *currentField, Field *midField, Field *nextField, Parameter *parameter, SubdomainInfo &subdomainInfo,
               std::vector<double> &currentDensityDerivative, std::vector<double> &currentSpeedDerivative, std::vector<double> &currentPositionDerivative,
               std::vector<double> &midDensityDerivative, std::vector<double> &midSpeedDerivative, std::vector<double> &midPositionDerivative,
               double t, double k)
{
// Loop on all the particles
#pragma omp parallel for schedule(dynamic)
    for (int i = subdomainInfo.startingParticle; i <= subdomainInfo.endingParticle; i++)
    {
        switch (currentField->type[i])
        {
        // Free particles update
        case freePart:
            nextField->density[i] = currentField->density[i] + k * ((1 - parameter->theta) * currentDensityDerivative[i] + parameter->theta * midDensityDerivative[i]);
            for (int j = 0; j <= 2; j++)
            {
                nextField->speed[j][i] = currentField->speed[j][i] + k * ((1 - parameter->theta) * currentSpeedDerivative[3 * i + j] + parameter->theta * midSpeedDerivative[3 * i + j]);
                nextField->pos[j][i] = currentField->pos[j][i] + k * ((1 - parameter->theta) * currentPositionDerivative[3 * i + j] + parameter->theta * midPositionDerivative[3 * i + j]);
            }
            break;

        // Fixed particles update
        case fixedPart:
            nextField->density[i] = currentField->density[i] + k * ((1 - parameter->theta) * currentDensityDerivative[i] + parameter->theta * midDensityDerivative[i]);
            break;

        // Moving boundary particles update
        default:
            nextField->density[i] = currentField->density[i] + k * ((1 - parameter->theta) * currentDensityDerivative[i] + parameter->theta * midDensityDerivative[i]);
            updateMovingPos(nextField, parameter, t, k, i);
            updateMovingSpeed(nextField, parameter, t, k, i);
            break;
        }
        pressureComputation(nextField, parameter, i);
    }
}

/*
*Input:
*- currentField: field that contains all the information at time t
*- nextField: field in which results of step n are stored
*- parameter: pointer to the field containing the user defined parameters
*- currentDensityDerivative: vector containing derivative of density for each particle at time t
*- currentSpeedDerivative: vector containing derivative of velocity for each particle at time t
*- t: current simulation time
*- k: timestep
*Decscription:
* Knowing the field at time t (currentField) and the density and velocity derivative,
* computes the field at time t+k with euler integration method and store it in structure nextField
*/
void eulerUpdate(Field *currentField, Field *nextField, Parameter *parameter, SubdomainInfo &subdomainInfo,
                 std::vector<double> &currentDensityDerivative, std::vector<double> &currentSpeedDerivative,
                 std::vector<double> &currentPositionDerivative, double t, double k)
{
// Loop on all the particles
#pragma omp parallel for schedule(dynamic)
    for (int i = subdomainInfo.startingParticle; i <= subdomainInfo.endingParticle; i++)
    {
        switch (currentField->type[i])
        {
        // Free particles update
        case freePart:
            nextField->density[i] = currentField->density[i] + k * currentDensityDerivative[i];
            for (int j = 0; j <= 2; j++)
            {
                nextField->speed[j][i] = currentField->speed[j][i] + k * currentSpeedDerivative[3 * i + j];
                nextField->pos[j][i] = currentField->pos[j][i] + k * currentPositionDerivative[3 * i + j];
            }

            // Fixed particles update
            break;
        case fixedPart:
            nextField->density[i] = currentField->density[i] + k * currentDensityDerivative[i];
            break;

        // Moving boundary particles update
        default:
            nextField->density[i] = currentField->density[i] + k * currentDensityDerivative[i];
            updateMovingPos(nextField, parameter, t, k, i);
            updateMovingSpeed(nextField, parameter, t, k, i);
            break;
        }
        pressureComputation(nextField, parameter, i);
    }
}

/*
*Input:
*- currentField: field that contains all the variables
*- parameter: pointer to the field containing the user defined parameters
*- boxes: vector of vectors of int, each vector is related to a given box and contains a list
with the particles ID of the particles that are inside this box
*- surrBoxesAll: vector of vector of int, each vector is related to a given box and contains
a list with the box ID of the boxes that are adjacent to this box
*- currentDensityDerivative: vector containing derivative of density for each particle at time t
*- currentSpeedDerivative: vector containing derivative of velocity for each particle at time t
*Description:
* Knowing the field (currentField), computes the density and velocity derivatives and store them in vectors
*/
void derivativeComputation(Field *currentField, Parameter *parameter,
                           SubdomainInfo &subdomainInfo,
                           std::vector<std::vector<int>> &boxes,
                           std::vector<std::vector<int>> &surrBoxesAll,
                           std::vector<double> &currentDensityDerivative,
                           std::vector<double> &currentSpeedDerivative,
                           std::vector<double> &currentPositionDerivative,
                           bool midPoint)
{
    // Neighbors vectors (declaration outside)
    std::vector<int> neighbors;
    std::vector<double> kernelValues; // for XSPH method
    std::vector<double> kernelGradients;
    std::vector<double> viscosity;

    // Sort the particles at the current time step
    if (!midPoint)
    {
        sortParticles(currentField->pos, currentField->l, currentField->u, subdomainInfo.boxSize, boxes);
    } // At each time step, restart it

// Spans the boxes
#pragma omp parallel for private(neighbors, kernelGradients, kernelValues, viscosity) schedule(dynamic)
    for (int box = subdomainInfo.startingBox; box <= subdomainInfo.endingBox; box++)
    {
        // Spans the particles in the box
        for (unsigned int part = 0; part < boxes[box].size(); part++)
        {
            // Declarations
            int particleID = boxes[box][part];
            neighbors.resize(0);
            kernelValues.resize(0);
            kernelGradients.resize(0);
            // Neighbor search
            findNeighbors(particleID, currentField->pos, parameter->kh, boxes, surrBoxesAll[box], neighbors, kernelGradients, kernelValues, parameter->kernel);
            // Continuity equation
            currentDensityDerivative[particleID] = continuity(particleID, neighbors, kernelGradients, currentField);
            // Momentum equation only for free particles
            if (currentField->type[particleID] == freePart)
                momentum(particleID, neighbors, kernelGradients, currentField, parameter, currentSpeedDerivative, viscosity);
            xsphCorrection(particleID, neighbors, kernelValues, currentField, parameter, currentPositionDerivative);
        }
    }
}

/*
*Input:
*- currentField: field that contains all the information about step n-1
*- nextField: field in which results of step n are stored
*- parameter: pointer to the field containing the user defined parameters
*- boxes: vector of vectors of int, each vector is related to a given box and contains a list
with the particles ID of the particles that are inside this box
*- surrBoxesAll: vector of vector of int, each vector is related to a given box and contains
a list with the box ID of the boxes that are adjacent to this box
*- n: number of the current time step
*Output:
*- Reboxing: flag that indicates if the box division need to be recomputed
*Description:
* Knowing the field at time t(currentField), computes the field at time t+k with euler integration method and store it in structure nextField
*/
void timeIntegration(Field *currentField, Field *nextField, Parameter *parameter,
                     SubdomainInfo &subdomainInfo, std::vector<std::vector<int>> &boxes,
                     std::vector<std::vector<int>> &surrBoxesAll,
                     double t, double k)
{
    std::vector<double> currentSpeedDerivative;    // [RB] ces vecteurs sont alloués à chaque pas de temps => ils pourraient être conservés
    std::vector<double> currentPositionDerivative; // For XSPH method
    std::vector<double> currentDensityDerivative;
    currentSpeedDerivative.assign(3 * currentField->nTotal, 0.0); // [RB] peut etre fait lors de la construction
    currentPositionDerivative.assign(3 * currentField->nTotal, 0.0);
    currentDensityDerivative.assign(currentField->nTotal, 0.0);
    // CPU time information
    derivativeComputation(currentField, parameter, subdomainInfo, boxes, surrBoxesAll,
                          currentDensityDerivative, currentSpeedDerivative,
                          currentPositionDerivative, false);

    switch (parameter->integrationMethod)
    {
    case euler:
    {
        eulerUpdate(currentField, nextField, parameter, subdomainInfo, currentDensityDerivative,
                    currentSpeedDerivative, currentPositionDerivative, t, k);
    }
    break;

    case RK2:
    {
        double kMid = 0.5 * k / parameter->theta;
        Field midFieldInstance;
        Field *midField = &midFieldInstance; // [RB] inutile
        std::vector<double> midSpeedDerivative;
        std::vector<double> midPositionDerivative;
        std::vector<double> midDensityDerivative;
        midSpeedDerivative.assign(3 * currentField->nTotal, 0.0); // [RB] meme remarques que pour "currentSpeedDerivative", etc
        midPositionDerivative.assign(3 * currentField->nTotal, 0.0);
        midDensityDerivative.assign(currentField->nTotal, 0.0);
        copyField(currentField, midField);
        // Storing midpoint in midField
        eulerUpdate(currentField, midField, parameter, subdomainInfo, currentDensityDerivative,
                    currentSpeedDerivative, currentPositionDerivative, t, kMid);
        // Share the mid point
        shareRKMidpoint(*midField, subdomainInfo);
        // Compute derivatives at midPoint
        derivativeComputation(midField, parameter, subdomainInfo, boxes, surrBoxesAll,
                              midDensityDerivative, midSpeedDerivative, midPositionDerivative, true);
        // Update
        RK2Update(currentField, midField, nextField, parameter, subdomainInfo, currentDensityDerivative,
                  currentSpeedDerivative, currentPositionDerivative, midDensityDerivative,
                  midSpeedDerivative, midPositionDerivative, t, k);
    }
    break;
    }
}
