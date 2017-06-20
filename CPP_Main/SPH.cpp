///**************************************************************************
/// SOURCE: Main function of the smoothed particle hydrodynamics (SPH) solver.
///**************************************************************************
#include "Main.h"
#include "Interface.h"
#include "Physics.h"
#include "Tools.h"
#include "Structures.h"

std::clock_t startExperimentTimeClock;

/*
*Input:
*- argv[1]: name of the input parameter file (mandatory)
*- argv[2]: name of the input geometry file (mandatory)
*- argv[3]: name of the output result (optional, default name is "result.txt")
*
*Description:
*Run the SPH solver for a given geometry and a given set of parameter and write the result in an output file.
*/
int main(int argc, char *argv[])
{
    // Record algorithm performance
    startExperimentTimeClock = std::clock();
    struct timeval time;
    gettimeofday(&time, NULL);
    double start = (double)time.tv_sec + (double)time.tv_usec * .000001;

    // MPI Initialization
    MPI_Init(&argc, &argv);
    SubdomainInfo subdomainInfo;
    MPI_Comm_size(MPI_COMM_WORLD, &(subdomainInfo.nTasks));
    MPI_Comm_rank(MPI_COMM_WORLD, &(subdomainInfo.procID));

    // Creates an error flag
    Error errorFlag = noError;

    // Checks and gets the arguments
    if (subdomainInfo.procID == 0)
    {
        std::cout << "Initialization..." << std::endl;
    }
    std::string parameterFilename;
    std::string geometryFilename;
    std::string experimentFilename;
    if (argc < 3) // Not enough arguments
    {
        std::cout << "Invalid input files.\n"
                  << std::endl;
        errorFlag = argumentError;
        MPI_Finalize();
        return errorFlag; // [RB] tester des exceptions?
    }
    else if (argc < 4) // Use default name for the experiment (result)
    {
        parameterFilename = argv[1];
        geometryFilename = argv[2];
        experimentFilename = "result";
    }
    else
    {
        parameterFilename = argv[1];
        geometryFilename = argv[2];
        experimentFilename = argv[3];
    }

    // Main variables declaration
    Parameter parameterInstance;
    Parameter *parameter = &parameterInstance; // [RB] inutile
    Field currentFieldInstance;
    Field *currentField = &currentFieldInstance; // [RB] inutile
    Field nextFieldInstance;
    Field *nextField = &nextFieldInstance;     // [RB] inutile
    Field globalFieldInstance;                 // Used by node 0 only
    Field *globalField = &globalFieldInstance; // Used by node 0 only // [RB] inutile

    // Reads parameters (each process) and geometry (process 0) and checks their consistency
    errorFlag = readParameter(parameterFilename, parameter);
    if (errorFlag != noError)
    {
        MPI_Finalize();
        return errorFlag; // [RB] tester des exceptions?
    }
    if (subdomainInfo.procID == 0)
    {
        errorFlag = initializeField(geometryFilename, globalField, parameter);
    }
    MPI_Bcast(&errorFlag, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (errorFlag != noError)
    {
        MPI_Finalize();
        return errorFlag; // [RB] tester des exceptions?
    }

    // Writes the initial configuration
    if (subdomainInfo.procID == 0)
    {
        writeField(globalField, 0.0, parameter, parameterFilename, geometryFilename, experimentFilename);
    }
    unsigned int writeCount = 1;

    // Scatters the globalField from node 0 into the currentField of all nodes
    errorFlag = scatterField(globalField, currentField, parameter, subdomainInfo);
    if (errorFlag != noError)
    {
        MPI_Finalize();
        return errorFlag; // [RB] tester des exceptions?
    }

    // Declares the box mesh and determines their adjacent relations variables
    std::vector<std::vector<int>> boxes;
    std::vector<std::vector<int>> surrBoxesAll;
    boxMesh(currentField->l, currentField->u, subdomainInfo.boxSize, boxes, surrBoxesAll);

    // Copies the invariant information about the field
    copyField(currentField, nextField);

    // Barrier (just to synchronize the output information)
    MPI_Barrier(MPI_COMM_WORLD);

    // Initialization done
    if (subdomainInfo.procID == 0)
        std::cout << "Done.\n"
                  << std::endl;

    // Information on the simulation
    if (subdomainInfo.procID == 0)
    {
        if (parameter->adaptativeTimeStep == no)
        {
            unsigned int nMax = (unsigned int)ceil(parameter->T / parameter->k);
            std::cout << "Number of time steps = " << nMax << "\n"
                      << std::endl;
        }
        else
            std::cout << "Number of time steps = "
                      << "not defined (adaptative time step)"
                      << "\n"
                      << std::endl;
        std::cout << "Number of free particles = " << globalField->nFree << "\n"
                  << std::endl;
        std::cout << "Number of fixed particles = " << globalField->nFixed << "\n"
                  << std::endl;
        std::cout << "Number of particles with imposed speed = " << globalField->nMoving << "\n"
                  << std::endl;
    }

    // ------------ LOOP ON TIME ------------
    if (subdomainInfo.procID == 0)
    {
        std::cout << "Time integration progress:\n"
                  << std::endl;
        std::cout << "0%-----------------------------------------------100%\n[";
    }
    unsigned int loadingBar = 0;
    double currentTime = 0.0; // Current time of the simulation
    for (unsigned int n = 1; currentTime < parameter->T; n++)
    {
        // Previous time step for reference
        currentField->nextK = parameter->k;

        // Next field
        copyField(currentField, nextField);
        // ---

        // Solve the time step
        timeIntegration(currentField, nextField, parameter, subdomainInfo, boxes,
                        surrBoxesAll, currentTime, parameter->k);
        currentTime += parameter->k;

        // Adaptive time step
        if (parameter->adaptativeTimeStep)
            timeStepUpdate(parameter->k, currentField->nextK, subdomainInfo);

        // Swap the two fields
        swapField(&currentField, &nextField);
        // ---

        // Major MPI communication: the local field is updated
        processUpdate(*currentField, subdomainInfo);

        // Write field when needed
        if (writeCount * parameter->writeInterval <= currentTime + 0.000001 * currentTime)
        {
            gatherField(globalField, currentField, subdomainInfo);
            globalField->currentTime = currentTime;
            if (subdomainInfo.procID == 0)
            {
                writeField(globalField, n, parameter, parameterFilename, geometryFilename, experimentFilename);
            }
            writeCount++;
        }

        // Fancy progress bar (ok if at least 50 time step)
        if (subdomainInfo.procID == 0 && currentTime > loadingBar * parameter->T / 50.0)
        {
            std::cout << ">" << std::flush;
            loadingBar++;
        }
    }

    // Time information printing
    if (subdomainInfo.procID == 0)
    {
        // Gets final time
        gettimeofday(&time, NULL);
        double final = (double)time.tv_sec + (double)time.tv_usec * .000001;
        std::cout << "]\n"
                  << std::endl;
        std::cout << "Real elapsed time \t" << final - start << "\n";
        std::cout << "Clock estimated time \t" << (std::clock() - startExperimentTimeClock) / (double)CLOCKS_PER_SEC << "\n";
    }

    // MPI Finalize
    MPI_Finalize();

    //*/

    return 0;
}
