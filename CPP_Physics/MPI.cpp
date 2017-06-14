///**************************************************************************
/// SOURCE: Functions related to MPI.
///**************************************************************************
#include "Main.h"
#include "Interface.h"
#include "Physics.h"
#include "Tools.h"
#include "Structures.h"

// For particle exchange/sharing
enum overlap
{
    leftOverlap,
    noOverlap,
    rightOverlap,
    NB_OVERLAP_VALUE
};
enum migrate
{
    noMigrate,
    leftMigrate,
    rightMigrate,
    NB_MIGRATE_VALUE
};
enum mpiMessage
{
    overlap,
    migration,
    dataExch,
    RK2Exch,
    NB_MPIMESSAGE_VALUE
};
enum insertion
{
    begin,
    end
};

/*
Input:
    - globalField: filled only on process #0, to be scattered
    - localField: partial field specific to each process. Will contain
    both the domain to be solved and the halos. Need to be done in two
    steps because MPI_Scatterv does not allow overlaps.
    - parameter: to get kh and integrationMethod
Ouput:
    - errorFlag: tells if the number of processor was acceptable or not
*/
Error scatterField(Field *globalField, Field *localField, Parameter *parameter,
                   SubdomainInfo &subdomainInfo)
{
    // Error flag
    Error errorFlag = noError;

    // Basic MPI process information
    int nTasks = subdomainInfo.nTasks;
    int procID = subdomainInfo.procID;

    // Box size (bigger if RK2 to avoid sorting twice at each time step)
    double boxSize = boxSizeCalc(parameter->kh, parameter->integrationMethod);
    subdomainInfo.boxSize = boxSize;

    // Checks if the number of processor appropriate (only node 0 has the info)
    int nTotalBoxesX;
    if (procID == 0)
    {
        for (int i = 0; i < 3; i++)
        {
            localField->l[i] = globalField->l[i]; // x component will be changed
            localField->u[i] = globalField->u[i]; // x component will be changed
        }
        nTotalBoxesX = ceil((globalField->u[0] - globalField->l[0]) / boxSize);
        if (nTotalBoxesX < 2 * nTasks)
        {
            std::cout << "Too much processors for the domain" << std::endl;
            std::cout << "The size along x must be sufficient to contain at least 2*nTasks boxes." << std::endl;
            errorFlag = consistencyError;
        }
        else
        {
            std::cout << "Appropriate number of processors" << std::endl;
        }
    }
    MPI_Bcast(&errorFlag, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (errorFlag != noError)
    {
        return errorFlag;
    }

    // Broadcasts the total number of boxes along x
    MPI_Bcast(&nTotalBoxesX, 1, MPI_INT, 0, MPI_COMM_WORLD);

    std::vector<int> startBoxX(nTasks + 1); // The last element helps the last process
    for (int i = 0; i <= nTasks; i++)
    {
        startBoxX[i] = (nTotalBoxesX * i) / nTasks; // can be optimized
    }

    // Broadcasts the global l and u and determines the correct l[0] and u[0]
    MPI_Bcast(localField->l, 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(localField->u, 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    double globall0 = localField->l[0];

    // Number of boxes along the Y and Z directions
    int nBoxesY = ceil((localField->u[1] - localField->l[1]) / boxSize);
    int nBoxesZ = ceil((localField->u[2] - localField->l[2]) / boxSize);

    // Left boundary and starting box
    if (procID == 0)
    {
        localField->l[0] = globall0;
        subdomainInfo.startingBox = 0;
    }
    else
    {
        localField->l[0] = globall0 + (startBoxX[procID] - 1) * boxSize;
        subdomainInfo.startingBox = nBoxesY * nBoxesZ;
    }

    // Right boundary and ending box
    if (procID == nTasks - 1)
    {
        localField->u[0] = globall0 + startBoxX[procID + 1] * boxSize;
        subdomainInfo.endingBox = subdomainInfo.startingBox + (startBoxX[procID + 1] - startBoxX[procID]) * nBoxesY * nBoxesZ - 1;
    }
    else
    {
        localField->u[0] = globall0 + (startBoxX[procID + 1] + 1) * boxSize;
        subdomainInfo.endingBox = subdomainInfo.startingBox + (startBoxX[procID + 1] - startBoxX[procID]) * nBoxesY * nBoxesZ - 1;
    }

    // Computes indices and sorts particles
    std::vector<int> nPartNode(nTasks, 0);
    std::vector<std::pair<int, int>> domainIndex;
    std::vector<double> limits(nTasks); // left boundary of each domain along x
    std::vector<int> offset(nTasks);

    if (procID == 0)
    {
        for (int i = 0; i < nTasks; i++)
            limits[i] = globall0 + startBoxX[i] * boxSize; // Without overlap
        computeDomainIndex(globalField->pos[0], limits, nPartNode, domainIndex, nTasks);
        sortParticles(*globalField, domainIndex);
        // Offset vector
        offset[0] = 0;
        for (int i = 1; i < nTasks; i++)
        {
            offset[i] = offset[i - 1] + nPartNode[i - 1];
        }
    }

    // Shares the number of particle per domain and prepares the vector size
    MPI_Scatter(&nPartNode[0], 1, MPI_INT, &localField->nTotal, 1, MPI_INT, 0, MPI_COMM_WORLD);
    for (int i = 0; i < 3; i++)
    {
        localField->pos[i].resize(localField->nTotal);
        localField->speed[i].resize(localField->nTotal);
    }
    localField->density.resize(localField->nTotal);
    localField->pressure.resize(localField->nTotal);
    localField->mass.resize(localField->nTotal);
    localField->type.resize(localField->nTotal);

    // Scatters globalField into localFields
    for (int i = 0; i < 3; i++)
    {
        MPI_Scatterv(&(globalField->pos[i][0]), &nPartNode[0], &offset[0], MPI_DOUBLE,
                     &(localField->pos[i][0]), localField->nTotal, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        MPI_Scatterv(&globalField->speed[i][0], &nPartNode[0], &offset[0], MPI_DOUBLE,
                     &localField->speed[i][0], localField->nTotal, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    }
    MPI_Scatterv(&(globalField->density[0]), &nPartNode[0], &offset[0], MPI_DOUBLE,
                 &(localField->density[0]), localField->nTotal, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Scatterv(&globalField->pressure[0], &nPartNode[0], &offset[0], MPI_DOUBLE,
                 &localField->pressure[0], localField->nTotal, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Scatterv(&globalField->mass[0], &nPartNode[0], &offset[0], MPI_DOUBLE,
                 &localField->mass[0], localField->nTotal, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Scatterv(&(globalField->type[0]), &nPartNode[0], &offset[0], MPI_INT,
                 &(localField->type[0]), localField->nTotal, MPI_INT, 0, MPI_COMM_WORLD);

    std::cout << localField->pos[0].size() << " particles on node " << procID << std::endl;

    // Sharing boundaries
    shareOverlap(*localField, subdomainInfo);

    // Computes nTotal
    localField->nTotal = localField->pos[0].size();

    // Computes nFree, nMoving and nFixed
    localField->nFree = 0;
    localField->nFixed = 0;
    localField->nMoving = 0;
    for (int i = 0; i < localField->nTotal; i++)
    {
        switch (localField->type[i])
        {
        case freePart:
            localField->nFree++;
            break;
        case fixedPart:
            localField->nFixed++;
            break;
        default:
            localField->nMoving++;
        }
    }

    std::cout << localField->nTotal << " total particles on node " << procID << std::endl;

    // Shares the moving boundaries information
    int nbMB1;
    if (procID == 0)
    {
        nbMB1 = parameter->posLaw.size();
    }
    MPI_Bcast(&nbMB1, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (procID != 0)
    {
        parameter->posLaw.resize(nbMB1);
        parameter->charactTime.resize(nbMB1);
        parameter->amplitude.resize(nbMB1);
        parameter->angleLaw.resize(nbMB1);
        for (int i = 0; i < 3; i++)
        {
            parameter->teta[i].resize(nbMB1);
            parameter->movingDirection[i].resize(nbMB1);
            parameter->rotationCenter[i].resize(nbMB1);
        }
    }

    MPI_Bcast(&(parameter->posLaw[0]), nbMB1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&(parameter->angleLaw[0]), nbMB1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&(parameter->charactTime[0]), nbMB1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(&(parameter->amplitude[0]), nbMB1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    for (int i = 0; i < 3; i++)
    {
        MPI_Bcast(&(parameter->teta[i][0]), nbMB1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        MPI_Bcast(&(parameter->movingDirection[i][0]), nbMB1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        MPI_Bcast(&(parameter->rotationCenter[i][0]), nbMB1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    }
    return noError;
}

/* Gathers all the current fields into the global Field */
void gatherField(Field *globalField, Field *localField, SubdomainInfo &subdomainInfo)
{
    // Gathers the number of particles for each node in node 0
    int nbPart = subdomainInfo.endingParticle - subdomainInfo.startingParticle + 1;
    int start = subdomainInfo.startingParticle;
    std::vector<int> allNbPart;
    if (subdomainInfo.procID == 0)
        allNbPart.resize(subdomainInfo.nTasks);
    MPI_Gather(&nbPart, 1, MPI_INT, &(allNbPart[0]), 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Computes the offsets for scatterv
    std::vector<int> offsets;
    if (subdomainInfo.procID == 0)
    {
        offsets.resize(subdomainInfo.nTasks);
        offsets[0] = 0;
        for (int i = 1; i < subdomainInfo.nTasks; i++)
            offsets[i] = offsets[i - 1] + allNbPart[i - 1];
    }

    // Gathers the fields
    for (int i = 0; i < 3; i++)
    {
        MPI_Gatherv(&(localField->pos[i][start]), nbPart, MPI_DOUBLE,
                    &(globalField->pos[i][0]), &(allNbPart[0]), &(offsets[0]), MPI_DOUBLE,
                    0, MPI_COMM_WORLD);
        MPI_Gatherv(&(localField->speed[i][start]), nbPart, MPI_DOUBLE,
                    &(globalField->speed[i][0]), &(allNbPart[0]), &(offsets[0]), MPI_DOUBLE,
                    0, MPI_COMM_WORLD);
    }
    MPI_Gatherv(&(localField->density[start]), nbPart, MPI_DOUBLE,
                &(globalField->density[0]), &(allNbPart[0]), &(offsets[0]), MPI_DOUBLE,
                0, MPI_COMM_WORLD);
    MPI_Gatherv(&(localField->pressure[start]), nbPart, MPI_DOUBLE,
                &(globalField->pressure[0]), &(allNbPart[0]), &(offsets[0]), MPI_DOUBLE,
                0, MPI_COMM_WORLD);
    MPI_Gatherv(&(localField->mass[start]), nbPart, MPI_DOUBLE,
                &(globalField->mass[0]), &(allNbPart[0]), &(offsets[0]), MPI_DOUBLE,
                0, MPI_COMM_WORLD);
    MPI_Gatherv(&(localField->type[start]), nbPart, MPI_INT,
                &(globalField->type[0]), &(allNbPart[0]), &(offsets[0]), MPI_INT,
                0, MPI_COMM_WORLD);
}

void deleteHalos(Field &field, SubdomainInfo &subdomainInfo)
{
    // Starting and ending particles
    int start = subdomainInfo.startingParticle;
    int end = subdomainInfo.endingParticle;

    // Deletes in all fields
    for (int i = 0; i < 3; i++)
    {
        // Cut position
        field.pos[i].resize(end + 1);
        field.pos[i].erase(field.pos[i].begin(), field.pos[i].begin() + start);
        // Cut speed
        field.speed[i].resize(end + 1);
        field.speed[i].erase(field.speed[i].begin(), field.speed[i].begin() + start);
    }
    // Cut density
    field.density.resize(end + 1);
    field.density.erase(field.density.begin(), field.density.begin() + start);
    // Cut pressure
    field.pressure.resize(end + 1);
    field.pressure.erase(field.pressure.begin(), field.pressure.begin() + start);
    // Cut mass
    field.mass.resize(end + 1);
    field.mass.erase(field.mass.begin(), field.mass.begin() + start);
    // Cut type
    field.type.resize(end + 1);
    field.type.erase(field.type.begin(), field.type.begin() + start);
}

// Generalizes the MPI_Send function to all fields to send
void MPI_Send_All(Field &field, int startingPoint, int size, int recvProcID, mpiMessage message)
{
    for (int i = 0; i < 3; i++)
    {
        MPI_Send(&field.pos[i][startingPoint], size, MPI_DOUBLE, recvProcID, message, MPI_COMM_WORLD);
        MPI_Send(&field.speed[i][startingPoint], size, MPI_DOUBLE, recvProcID, message, MPI_COMM_WORLD);
    }
    MPI_Send(&field.density[startingPoint], size, MPI_DOUBLE, recvProcID, message, MPI_COMM_WORLD);
    MPI_Send(&field.pressure[startingPoint], size, MPI_DOUBLE, recvProcID, message, MPI_COMM_WORLD);
    MPI_Send(&field.mass[startingPoint], size, MPI_DOUBLE, recvProcID, message, MPI_COMM_WORLD);
    MPI_Send(&field.type[startingPoint], size, MPI_INT, recvProcID, message, MPI_COMM_WORLD);
}

// Generalizes the MPI_Receive function to all fields to receive
void MPI_Recv_All(std::vector<double> (&recvBuffer)[9], std::vector<int> &recvBufferType,
                  int size, int sendProcID, mpiMessage message)
{
    // Elements: 0,x | 1,u | 2,y | 3,v | 4,z | 5,w | 6,density | 7,pressure | 8,mass
    for (int i = 0; i < 9; i++)
    {
        MPI_Recv(&recvBuffer[i][0], size, MPI_DOUBLE, sendProcID, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    MPI_Recv(&recvBufferType[0], size, MPI_INT, sendProcID, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

// Generalizes the MPI_Receive function in the case where we write directly in the field
// Use: for sharing the mid-point information of RK2
void MPI_Recv_All_RK2(Field &field, int startingPoint, int size, int sendProcID, mpiMessage message)
{
    for (int i = 0; i < 3; i++)
    {
        MPI_Recv(&field.pos[i][startingPoint], size, MPI_DOUBLE, sendProcID, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&field.speed[i][startingPoint], size, MPI_DOUBLE, sendProcID, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    MPI_Recv(&field.density[startingPoint], size, MPI_DOUBLE, sendProcID, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&field.pressure[startingPoint], size, MPI_DOUBLE, sendProcID, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&field.mass[startingPoint], size, MPI_DOUBLE, sendProcID, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&field.type[startingPoint], size, MPI_INT, sendProcID, message, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

void insertParticles(Field &field, std::vector<double> (&recvBuffer)[9], std::vector<int> &recvBufferType, insertion place)
{
    if (place == end)
    { // Put it at the end
        for (int i = 0; i < 3; i++)
        {
            field.pos[i].insert(field.pos[i].end(), recvBuffer[2 * i].begin(), recvBuffer[2 * i].end());
            field.speed[i].insert(field.speed[i].end(), recvBuffer[2 * i + 1].begin(), recvBuffer[2 * i + 1].end());
        }
        field.density.insert(field.density.end(), recvBuffer[6].begin(), recvBuffer[6].end());
        field.pressure.insert(field.pressure.end(), recvBuffer[7].begin(), recvBuffer[7].end());
        field.mass.insert(field.mass.end(), recvBuffer[8].begin(), recvBuffer[8].end());
        field.type.insert(field.type.end(), recvBufferType.begin(), recvBufferType.end());
    }
    else if (place == begin)
    { // Put it at the beginning
        for (int i = 0; i < 3; i++)
        {
            field.pos[i].insert(field.pos[i].begin(), recvBuffer[2 * i].begin(), recvBuffer[2 * i].end());
            field.speed[i].insert(field.speed[i].begin(), recvBuffer[2 * i + 1].begin(), recvBuffer[2 * i + 1].end());
        }
        field.density.insert(field.density.begin(), recvBuffer[6].begin(), recvBuffer[6].end());
        field.pressure.insert(field.pressure.begin(), recvBuffer[7].begin(), recvBuffer[7].end());
        field.mass.insert(field.mass.begin(), recvBuffer[8].begin(), recvBuffer[8].end());
        field.type.insert(field.type.begin(), recvBufferType.begin(), recvBufferType.end());
    }
    else
    {
        std::cout << "Not acceptable insertion operation." << std::endl;
    }
}

void shareRKMidpoint(Field &field, SubdomainInfo &subdomainInfo)
{
    // Useful information
    int start = subdomainInfo.startingParticle;
    int end = subdomainInfo.endingParticle;
    int procID = subdomainInfo.procID;
    int nTasks = subdomainInfo.nTasks;

    // Declarations
    int sizeToLeft;
    int sizeToRight;
    int sizeFromLeft = start;
    int sizeFromRight = field.nTotal - end - 1;

    // Different possible cases
    if (nTasks > 1)
    {
        if (procID == 0)
        {
            // Sends the edge to the right
            MPI_Recv(&sizeToRight, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Send_All(field, end - sizeToRight + 1, sizeToRight, procID + 1, RK2Exch);
            // Nothing to send to the left

            // Nothing to reveive from the left
            // Receives the halo from the right
            MPI_Send(&sizeFromRight, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD);
            MPI_Recv_All_RK2(field, end + 1, sizeFromRight, procID + 1, RK2Exch);
        }
        else if (procID == nTasks - 1)
        {
            if (procID % 2 == 0)
            {
                // Nothing to send to the right
                // Sends the edge to the left (procID is even)
                MPI_Recv(&sizeToLeft, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send_All(field, start, sizeToLeft, procID - 1, RK2Exch);

                // Receives the edge from the left (procID is even)
                MPI_Send(&sizeFromLeft, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Recv_All_RK2(field, 0, sizeFromLeft, procID - 1, RK2Exch);
                // Nothing to receive from the right
            }
            else
            {
                // Receives the edge from the left (procID is odd)
                MPI_Send(&sizeFromLeft, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Recv_All_RK2(field, 0, sizeFromLeft, procID - 1, RK2Exch);
                // Nothing to receive from the right

                // Nothing to send to the right
                // Sends the edge to the left (procID is odd)
                MPI_Recv(&sizeToLeft, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send_All(field, start, sizeToLeft, procID - 1, RK2Exch);
            }
        }
        else
        {
            if (procID % 2 == 0)
            {
                // Sends the edge to the right (procID is even)
                MPI_Recv(&sizeToRight, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send_All(field, end - sizeToRight + 1, sizeToRight, procID + 1, RK2Exch);
                // Sends the edge to the left (procID is even)
                MPI_Recv(&sizeToLeft, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send_All(field, start, sizeToLeft, procID - 1, RK2Exch);

                // Receives the edge from the left (procID is even)
                MPI_Send(&sizeFromLeft, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Recv_All_RK2(field, 0, sizeFromLeft, procID - 1, RK2Exch);
                // Receives the edge from the right (procID is even)
                MPI_Send(&sizeFromRight, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD);
                MPI_Recv_All_RK2(field, end + 1, sizeFromRight, procID + 1, RK2Exch);
            }
            else
            {
                // Receives the edge from the left (procID is odd)
                MPI_Send(&sizeFromLeft, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Recv_All_RK2(field, 0, sizeFromLeft, procID - 1, RK2Exch);
                // Receives the edge from the right (procID is odd)
                MPI_Send(&sizeFromRight, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD);
                MPI_Recv_All_RK2(field, end + 1, sizeFromRight, procID + 1, RK2Exch);

                // Sends the edge to the right (procID is odd)
                MPI_Recv(&sizeToRight, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send_All(field, end - sizeToRight + 1, sizeToRight, procID + 1, RK2Exch);
                // Sends the edge to the left (procID is odd)
                MPI_Recv(&sizeToLeft, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send_All(field, start, sizeToLeft, procID - 1, RK2Exch);
            }
        }
    }
    else
    {
        return;
    }
}

void shareOverlap(Field &field, SubdomainInfo &subdomainInfo)
{
    // Declarations
    std::vector<std::pair<int, int>> indexOverlap;
    std::vector<double> recvVectL[9], recvVectR[9];
    std::vector<int> recvVectTypeL, recvVectTypeR;
    int nOverlap[2];
    int startOverlapToRight; // startOverlapToLeft is always equal to 0 !!
    int sizeRecvOverlapL, sizeRecvOverlapR;

    // Useful information
    double l0 = field.l[0];
    double u0 = field.u[0];
    double boxSize = subdomainInfo.boxSize;
    int procID = subdomainInfo.procID;
    int nTasks = subdomainInfo.nTasks;

    // Different possible cases (trivial if only one node)
    if (nTasks > 1)
    {
        if (procID == 0)
        { // No left neighbor domain
            // inner domain: [l[0] , u[0] - 2*boxSize[
            // right edge: [u[0] - 2*boxSize , u[0] - boxSize[
            // right halo: [u[0] - boxSize , u[0]]
            computeOverlapIndex(field.pos[0], indexOverlap, nOverlap,
                                l0, l0, u0 - 2 * boxSize, u0 - boxSize); // No Left Overlap
            sortParticles(field, indexOverlap);

            // Start position to send
            startOverlapToRight = field.pos[0].size() - nOverlap[1];

            // Sends the edge to the right
            MPI_Send(&nOverlap[1], 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD);
            MPI_Send_All(field, startOverlapToRight, nOverlap[1], procID + 1, overlap);

            // Receives the halo from the right
            MPI_Recv(&sizeRecvOverlapR, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            //recvVectR.resize(sizeRecvOverlapR);
            for (int i = 0; i < 9; i++)
            {
                recvVectR[i].resize(sizeRecvOverlapR);
            }
            recvVectTypeR.resize(sizeRecvOverlapR);
            MPI_Recv_All(recvVectR, recvVectTypeR, sizeRecvOverlapR, procID + 1, overlap);

            // Adding the particles to the field (at the end of the vector)
            insertParticles(field, recvVectR, recvVectTypeR, end);

            // Update the starting/ending particles
            subdomainInfo.startingParticle = 0;
            subdomainInfo.endingParticle = field.pos[0].size() - sizeRecvOverlapR - 1;
        }
        else if (procID == nTasks - 1)
        { // No right neighbor domain
            // left halo: [l[0] , l[0] + boxSize[
            // left edge: [l[0] + boxSize , l[0] + 2*boxSize[
            // inner domain: [l[0] + 2*boxSize , u[0][
            computeOverlapIndex(field.pos[0], indexOverlap, nOverlap,
                                l0 + boxSize, l0 + 2 * boxSize, u0, u0); // No right Overlap
            sortParticles(field, indexOverlap);

            if (procID % 2 == 0)
            {
                // Nothing to send to the right
                // Sends the edge to the left (procID is even)
                MPI_Send(&nOverlap[0], 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, 0, nOverlap[0], procID - 1, overlap);

                // Receives the edge from the left (procID is even)
                MPI_Recv(&sizeRecvOverlapL, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectL[i].resize(sizeRecvOverlapL);
                }
                recvVectTypeL.resize(sizeRecvOverlapL);
                MPI_Recv_All(recvVectL, recvVectTypeL, sizeRecvOverlapL, procID - 1, overlap);
                // Nothing to receive from the right
            }
            else
            {
                // Receives the edge from the left (procID is odd)
                MPI_Recv(&sizeRecvOverlapL, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectL[i].resize(sizeRecvOverlapL);
                }
                recvVectTypeL.resize(sizeRecvOverlapL);
                MPI_Recv_All(recvVectL, recvVectTypeL, sizeRecvOverlapL, procID - 1, overlap);
                // Nothing to receive from the right

                // Nothing to send to the right
                // Sends the edge to the left (procID is odd)
                MPI_Send(&nOverlap[0], 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, 0, nOverlap[0], procID - 1, overlap);
            }

            // Adding the particles to the field
            insertParticles(field, recvVectL, recvVectTypeL, begin);

            // Update the starting/ending particles
            subdomainInfo.startingParticle = sizeRecvOverlapL;
            subdomainInfo.endingParticle = field.pos[0].size() - 1;
        }
        else
        { // Domain in the middle
            // left halo: [l[0] , l[0] + boxSize[
            // left edge: [l[0] + boxSize , l[0] + 2*boxSize[
            // inner domain: [l[0] + 2*boxSize , u[0] - 2*boxSize[
            // right edge: [u[0] - 2*boxSize , u[0] - boxSize[
            // right halo: [u[0] - boxSize , u[0]]
            computeOverlapIndex(field.pos[0], indexOverlap, nOverlap,
                                l0 + boxSize, l0 + 2 * boxSize, u0 - 2 * boxSize, u0 - boxSize);
            sortParticles(field, indexOverlap);

            // Start position to send
            startOverlapToRight = field.pos[0].size() - nOverlap[1];

            if (procID % 2 == 0)
            {
                // Sends the edge to the right (procID is even)
                MPI_Send(&nOverlap[1], 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, startOverlapToRight, nOverlap[1], procID + 1, overlap);
                // Sends the edge to the left (procID is even)
                MPI_Send(&nOverlap[0], 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, 0, nOverlap[0], procID - 1, overlap);

                // Receives the edge from the left (procID is even)
                MPI_Recv(&sizeRecvOverlapL, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectL[i].resize(sizeRecvOverlapL);
                }
                recvVectTypeL.resize(sizeRecvOverlapL);
                MPI_Recv_All(recvVectL, recvVectTypeL, sizeRecvOverlapL, procID - 1, overlap);
                // Receives the edge from the right (procID is even)
                MPI_Recv(&sizeRecvOverlapR, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectR[i].resize(sizeRecvOverlapR);
                }
                recvVectTypeR.resize(sizeRecvOverlapR);
                MPI_Recv_All(recvVectR, recvVectTypeR, sizeRecvOverlapR, procID + 1, overlap);

                // Adding the particles to the field
            }
            else
            {
                // Receives the edge from the left (procID is even)
                MPI_Recv(&sizeRecvOverlapL, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectL[i].resize(sizeRecvOverlapL);
                }
                recvVectTypeL.resize(sizeRecvOverlapL);
                MPI_Recv_All(recvVectL, recvVectTypeL, sizeRecvOverlapL, procID - 1, overlap);
                // Receives the edge from the right (procID is even)
                MPI_Recv(&sizeRecvOverlapR, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectR[i].resize(sizeRecvOverlapR);
                }
                recvVectTypeR.resize(sizeRecvOverlapR);
                MPI_Recv_All(recvVectR, recvVectTypeR, sizeRecvOverlapR, procID + 1, overlap);

                // Sends the edge to the right (procID is even)
                MPI_Send(&nOverlap[1], 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, startOverlapToRight, nOverlap[1], procID + 1, overlap);
                // Sends the edge to the left (procID is even)
                MPI_Send(&nOverlap[0], 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, 0, nOverlap[0], procID - 1, overlap);
            }

            // Adding the particles to the field
            insertParticles(field, recvVectL, recvVectTypeL, begin);
            insertParticles(field, recvVectR, recvVectTypeR, end);

            // Update the starting/ending particles
            subdomainInfo.startingParticle = sizeRecvOverlapL;
            subdomainInfo.endingParticle = field.pos[0].size() - sizeRecvOverlapR - 1;
        }
    }
    else
    {
        // Single processor
        subdomainInfo.startingParticle = 0;
        subdomainInfo.endingParticle = field.pos[0].size() - 1;
        return;
    }
}

void shareMigrate(Field &field, SubdomainInfo &subdomainInfo)
{
    // Declarations
    std::vector<std::pair<int, int>> indexMigrate;
    std::vector<double> recvVectL[9], recvVectR[9];
    std::vector<int> recvVectTypeL, recvVectTypeR;
    int nMigrate[2];
    int startMigrateToLeft, startMigrateToRight;
    int sizeRecvMigrate;

    // Useful information
    double l0 = field.l[0];
    double u0 = field.u[0];
    double boxSize = subdomainInfo.boxSize;
    int procID = subdomainInfo.procID;
    int nTasks = subdomainInfo.nTasks;

    // Different possible cases (trivial if only one node)
    if (nTasks > 1)
    {
        if (procID == 0)
        { // No left neighbor domain
            // inner domain: [l[0] , u[0] - 2*boxSize[
            // right edge: [u[0] - 2*boxSize , u[0] - boxSize[
            // right halo: [u[0] - boxSize , u[0]]
            computeMigrateIndex(field.pos[0], indexMigrate, nMigrate, l0, u0 - boxSize);
            sortParticles(field, indexMigrate);

            // Start position to send
            startMigrateToRight = field.pos[0].size() - nMigrate[1];

            // Sends the edge to the right
            MPI_Send(&nMigrate[1], 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD);
            MPI_Send_All(field, startMigrateToRight, nMigrate[1], procID + 1, migration);
            // Nothing to send to the left

            // Nothing to receive from the left
            // Receives the halo from the right
            MPI_Recv(&sizeRecvMigrate, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            for (int i = 0; i < 9; i++)
            {
                recvVectR[i].resize(sizeRecvMigrate);
            }
            recvVectTypeR.resize(sizeRecvMigrate);
            MPI_Recv_All(recvVectR, recvVectTypeR, sizeRecvMigrate, procID + 1, migration);
            // Removing previous particles (even those who go out of the domain! Keep it? )
            resizeField(field, nMigrate[0] + nMigrate[1]); // OR: remove only the right ones (on the left, out of the domain but keep them)
            // Adding the particles to the field
            insertParticles(field, recvVectR, recvVectTypeR, end);
        }
        else if (procID == nTasks - 1)
        { // No right neighbor domain
            // left halo: [l[0] , l[0] + boxSize[
            // left edge: [l[0] + boxSize , l[0] + 2*boxSize[
            // inner domain: [l[0] + 2*boxSize , u[0][
            computeMigrateIndex(field.pos[0], indexMigrate, nMigrate, l0 + boxSize, u0);
            sortParticles(field, indexMigrate);

            // Start position to send
            //startMigrateToRight = field.pos[0].size() - nMigrate[1]; USELESS
            startMigrateToLeft = field.pos[0].size() - nMigrate[1] - nMigrate[0];

            if (procID % 2 == 0)
            {
                // Nothing to send to the right
                // Sends the edge to the left (procID is even)
                MPI_Send(&nMigrate[0], 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, startMigrateToLeft, nMigrate[0], procID - 1, migration);

                // Receives the edge from the left (procID is even)
                MPI_Recv(&sizeRecvMigrate, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectL[i].resize(sizeRecvMigrate);
                }
                recvVectTypeL.resize(sizeRecvMigrate);
                MPI_Recv_All(recvVectL, recvVectTypeL, sizeRecvMigrate, procID - 1, migration);
                // Nothing to receive from the right

                // Removing previous particles
                resizeField(field, nMigrate[0] + nMigrate[1]);
                // Adding the particles to the field
            }
            else
            {
                // Receives the edge from the left (procID is odd)
                MPI_Recv(&sizeRecvMigrate, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectL[i].resize(sizeRecvMigrate);
                }
                recvVectTypeL.resize(sizeRecvMigrate);
                MPI_Recv_All(recvVectL, recvVectTypeL, sizeRecvMigrate, procID - 1, migration);
                // Nothing to receive from the right

                // Nothing to send to the right
                // Sends the edge to the left (procID is odd)
                MPI_Send(&nMigrate[0], 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, startMigrateToLeft, nMigrate[0], procID - 1, migration);

                // Removing previous particles
                resizeField(field, nMigrate[0] + nMigrate[1]);
            }
            // Adding the particles to the field
            insertParticles(field, recvVectL, recvVectTypeL, end);
        }
        else
        { // Domain in the middle
            // left halo: [l[0] , l[0] + boxSize[
            // left edge: [l[0] + boxSize , l[0] + 2*boxSize[
            // inner domain: [l[0] + 2*boxSize , u[0] - 2*boxSize[
            // right edge: [u[0] - 2*boxSize , u[0] - boxSize[
            // right halo: [u[0] - boxSize , u[0]]
            computeMigrateIndex(field.pos[0], indexMigrate, nMigrate, l0 + boxSize, u0 - boxSize);
            sortParticles(field, indexMigrate);

            // Start positions to send
            startMigrateToRight = field.pos[0].size() - nMigrate[1];
            startMigrateToLeft = field.pos[0].size() - nMigrate[1] - nMigrate[0];

            if (procID % 2 == 0)
            {
                // Sends the edge to the right (procID is even)
                MPI_Send(&nMigrate[1], 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, startMigrateToRight, nMigrate[1], procID + 1, migration);
                // Sends the edge to the left (procID is even)
                MPI_Send(&nMigrate[0], 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, startMigrateToLeft, nMigrate[0], procID - 1, migration);

                // Receives the edge from the left (procID is even)
                MPI_Recv(&sizeRecvMigrate, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectL[i].resize(sizeRecvMigrate);
                }
                recvVectTypeL.resize(sizeRecvMigrate);
                MPI_Recv_All(recvVectL, recvVectTypeL, sizeRecvMigrate, procID - 1, migration);
                // Receives the edge from the right (procID is even)
                MPI_Recv(&sizeRecvMigrate, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectR[i].resize(sizeRecvMigrate);
                }
                recvVectTypeR.resize(sizeRecvMigrate);
                MPI_Recv_All(recvVectR, recvVectTypeR, sizeRecvMigrate, procID + 1, migration);

                // Removing previous particles
                resizeField(field, nMigrate[0] + nMigrate[1]);
            }
            else
            {
                // Receives the edge from the left (procID is odd)
                MPI_Recv(&sizeRecvMigrate, 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectL[i].resize(sizeRecvMigrate);
                }
                recvVectTypeL.resize(sizeRecvMigrate);
                MPI_Recv_All(recvVectL, recvVectTypeL, sizeRecvMigrate, procID - 1, migration);
                // Receives the edge from the right (procID is odd)
                MPI_Recv(&sizeRecvMigrate, 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (int i = 0; i < 9; i++)
                {
                    recvVectR[i].resize(sizeRecvMigrate);
                }
                recvVectTypeR.resize(sizeRecvMigrate);
                MPI_Recv_All(recvVectR, recvVectTypeR, sizeRecvMigrate, procID + 1, migration);

                // Sends the edge to the right (procID is odd)
                MPI_Send(&nMigrate[1], 1, MPI_INT, procID + 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, startMigrateToRight, nMigrate[1], procID + 1, migration);
                // Sends the edge to the left (procID is odd)
                MPI_Send(&nMigrate[0], 1, MPI_INT, procID - 1, dataExch, MPI_COMM_WORLD);
                MPI_Send_All(field, startMigrateToLeft, nMigrate[0], procID - 1, migration);

                // Removing previous particles
                resizeField(field, nMigrate[0] + nMigrate[1]);
            }
            // Adding the particles to the field
            insertParticles(field, recvVectL, recvVectTypeL, end);
            insertParticles(field, recvVectR, recvVectTypeR, end);
        }
    }
    else
    {
        // Single processor
        return;
    }
}

void processUpdate(Field &localField, SubdomainInfo &subdomainInfo)
{
    if (subdomainInfo.nTasks == 1)
    {
        return;
    }
    // --- call deleteHalos ---
    deleteHalos(localField, subdomainInfo);
    // --- call sendMigrate ---
    shareMigrate(localField, subdomainInfo);
    // --- call shareOverlap ---
    shareOverlap(localField, subdomainInfo);

    localField.nTotal = localField.pos[0].size();
    // Computes nFree, nMoving and nFixed
    localField.nFree = 0;
    localField.nFixed = 0;
    localField.nMoving = 0;
    for (int i = 0; i < localField.nTotal; i++)
    {
        switch (localField.type[i])
        {
        case freePart:
            localField.nFree++;
            break;
        case fixedPart:
            localField.nFixed++;
            break;
        default:
            localField.nMoving++;
        }
    }
}

void timeStepUpdate(double &nextK, double &localProposition, SubdomainInfo &subdomainInfo)
{
    // If only one task, no communication is needed
    if (subdomainInfo.nTasks == 1)
    {
        nextK = localProposition;
        return;
    }

    // Declarations
    std::vector<double> allPropositions(subdomainInfo.nTasks);

    // Gathering information
    MPI_Gather(&localProposition, 1, MPI_DOUBLE, &(allPropositions[0]), 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (subdomainInfo.procID == 0)
    {
        nextK = *std::min_element(allPropositions.begin(), allPropositions.begin() + subdomainInfo.nTasks - 1);
    }

    // Scattering information
    MPI_Bcast(&nextK, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

void computeDomainIndex(std::vector<double> &posX,
                        std::vector<double> &limits, std::vector<int> &nbPartNode,
                        std::vector<std::pair<int, int>> &index, int nTasks)
{
    // Loop over particles
    for (int i = 0; i < posX.size(); i++)
    {
        int domainNumber = getDomainNumber(posX[i], limits, nTasks);
        index.push_back(std::make_pair(domainNumber, i));
        nbPartNode[domainNumber]++;
    }
}

int getDomainNumber(double x, std::vector<double> &limits, int nTasks)
{
    //Improvement: implement a tree based search.
    int i;
    for (i = 0; i < nTasks && x > limits[i]; i++)
        ;
    return i - 1;
}

// left halo: [l[0] , l[0] + boxSize[
// left edge: [l[0] + boxSize , l[0] + 2*boxSize[
// inner domain: [l[0] + 2*boxSize , u[0] - 2*boxSize[
// right edge: [u[0] - 2*boxSize , u[0] - boxSize[
// right halo: [u[0] - boxSize , u[0]]
void computeMigrateIndex(std::vector<double> &posX,
                         std::vector<std::pair<int, int>> &index, int *nMigrate,
                         double Xmin, double Xmax)
{

    nMigrate[0] = 0;
    nMigrate[1] = 0;
    for (unsigned int i = 0; i < posX.size(); ++i)
    {
        if (posX[i] > Xmax)
        {
            index.push_back(std::make_pair(rightMigrate, i));
            ++nMigrate[1];
        }
        else if (posX[i] <= Xmin)
        {
            index.push_back(std::make_pair(leftMigrate, i));
            ++nMigrate[0];
        }
        else
        {
            index.push_back(std::make_pair(noMigrate, i));
        }
    }
}

void computeOverlapIndex(std::vector<double> &posX,
                         std::vector<std::pair<int, int>> &index, int *nOverlap,
                         double leftMinX, double leftMaxX, double rightMinX, double rightMaxX)
{

    nOverlap[0] = 0;
    nOverlap[1] = 0;
    for (unsigned int i = 0; i < posX.size(); ++i)
    {
        if (posX[i] > rightMinX && posX[i] <= rightMaxX)
        {
            index.push_back(std::make_pair(rightOverlap, i));
            ++nOverlap[1];
        }
        else if (posX[i] <= leftMaxX && posX[i] > leftMinX)
        {
            index.push_back(std::make_pair(leftOverlap, i));
            ++nOverlap[0];
        }
        else if (posX[i] < leftMinX || posX[i] > rightMaxX)
        { // TO MAKE SURE EVERYTHING IS OK !
            std::cout << "Particle " << i << " with position " << posX[i] << " should not be here !!" << std::endl;
            std::cout << "This particle has travelled more than one subdomain in one time step." << std::endl;
            std::cout << "The time step is probably too large and the numerical integration has diverged." << std::endl;
        }
        else
        {
            index.push_back(std::make_pair(noOverlap, i));
        }
    }
}

bool sortFunction(const std::pair<int, int> &one, const std::pair<int, int> &two)
{
    return (one.first < two.first);
}

void sortParticles(Field &field, std::vector<std::pair<int, int>> &index)
{
    // Sort the index vector
    std::sort(index.begin(), index.end(), sortFunction);
    int N = field.pos[0].size();

    // Temporary vectors for sorting
    std::vector<double> tmp(N);
    std::vector<int> tmpType(N);

    // --- Sorts all data one by one ---
    int i, coord;
    for (coord = 0; coord < 3; coord++)
    {
        // Position reordering
        for (i = 0; i < N; ++i)
            tmp[i] = field.pos[coord][index[i].second];
        (field.pos[coord]).swap(tmp);
        // Speed reordering
        for (i = 0; i < N; ++i)
            tmp[i] = field.speed[coord][index[i].second];
        (field.speed[coord]).swap(tmp);
    }
    // Density reordering
    for (i = 0; i < N; ++i)
        tmp[i] = field.density[index[i].second];
    (field.density).swap(tmp);
    // Pressure reordering
    for (i = 0; i < N; ++i)
        tmp[i] = field.pressure[index[i].second];
    (field.pressure).swap(tmp);
    // Mass reordering
    for (i = 0; i < N; ++i)
        tmp[i] = field.mass[index[i].second];
    (field.mass).swap(tmp);
    // Type reordering
    for (i = 0; i < N; ++i)
        tmpType[i] = field.type[index[i].second];
    (field.type).swap(tmpType);
}

void resizeField(Field &field, int nMigrate)
{
    int finalSize = (field.pos[0]).size() - nMigrate;
    int coord;
    for (coord = 0; coord < 3; coord++)
    {
        // Position resize
        (field.pos[coord]).resize(finalSize);
        // Speed resize
        (field.speed[coord]).resize(finalSize);
    }
    // Density resize
    (field.density).resize(finalSize);
    // Pressure resize
    (field.pressure).resize(finalSize);
    // Mass resize
    (field.mass).resize(finalSize);
    // Type resize
    (field.type).resize(finalSize);
}
