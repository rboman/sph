#include "Main.h"
#include "Interface.h"
#include "Physics.h"
#include "Tools.h"
#include "Structures.h"
#include <ctime>

std::clock_t startExperimentTimeClock;

int main(int argc, char *argv[])
{
    //Input parameters
    if (argc != 5)
    {
        std::cout << "Invalid input parameters. Must be: s kh l eps\n";
        return EXIT_FAILURE;
    }
    double s = atof(argv[1]);
    double kh = atof(argv[2]);
    double l = atof(argv[3]);
    double eps = atof(argv[4]);
    std::cout << "\nParameter list: " << s << ", " << kh << ", " << l << ", " << eps << "\n";

    double o[3] = {0.0, 0.0, 0.0};
    double L[3] = {l, 4, 5};
    double teta[3] = {0.0, 0.0, 0.0};

    //Generate cube
    std::vector<double> pos;
    int nPart;
    double volPart;
    meshcube(o, L, teta, s, pos, &nPart, &volPart, eps);

    double ll[3] = {-L[0] / 2, -L[1] / 2, -L[2] / 2};
    double uu[3] = {L[0] / 2, L[1] / 2, L[2] / 2};

    // Kernel type
    Kernel kernelType = Quintic_spline;

    // Vectors declaration
    std::vector<std::vector<int>> neighborsAll_naive;
    //std::vector<std::vector<int> > neighborsAll_linked;
    std::vector<std::vector<int>> neighborsAll_splitted;
    std::vector<std::vector<int>> neighborsAll_splitted_s;
    std::vector<std::vector<double>> kernelGradientsAll;
    std::vector<std::vector<double>> kernelGradientsAll_splitted;
    std::vector<std::vector<double>> kernelGradientsAll_splitted_s;

    for (int i = 0; i < pos.size() / 3; i++)
    {
        std::vector<int> A;
        //std::vector<int> B;
        std::vector<int> C;
        std::vector<int> C2;
        neighborsAll_naive.push_back(A);
        //neighborsAll_linked.push_back(B);
        neighborsAll_splitted.push_back(C);
        neighborsAll_splitted_s.push_back(C2);

        std::vector<double> D;
        kernelGradientsAll.push_back(D);
        std::vector<double> E;
        kernelGradientsAll_splitted.push_back(E);
        std::vector<double> F;
        kernelGradientsAll_splitted_s.push_back(E);
    }

    //Record algorithm performance
    std::clock_t start;
    double duration;

    // ALL PAIRS - NAIVE
    start = std::clock();

    //neighborAllPair(pos, kh, neighborsAll_naive, kernelGradientsAll, kernelType);

    duration = (std::clock() - start) / (double)CLOCKS_PER_SEC;
    std::cout << "\nElapsed time AllPair: " << duration << " [s]\n";

    // LINKED LIST
    /*
    start = std::clock();

    neighborLinkedList(pos, ll, uu, kh, neighborsAll_linked, kernelGradientsAll, kernelType);

    duration = ( std::clock() - start ) / (double) CLOCKS_PER_SEC;
    std::cout<<"Elapsed time Linked List: " << duration <<" [s]\n";
    */

    // SPLITTED NEIGHBORS
    start = std::clock();

    std::vector<std::vector<int>> boxes;
    std::vector<std::vector<int>> surrBoxesAll;
    boxMesh(ll, uu, kh, boxes, surrBoxesAll);
    sortParticles(pos, ll, uu, kh, boxes);
    for (int box = 0; box < boxes.size(); box++)
    {
        for (unsigned int part = 0; part < boxes[box].size(); part++)
        {
            std::vector<int> neighbors;
            std::vector<double> kernelGradients;
            int particleID = boxes[box][part];
            findNeighbors(particleID, pos, kh, boxes, surrBoxesAll[box],
                          neighbors, kernelGradients, kernelType);
            kernelGradientsAll_splitted[particleID] = kernelGradients;
            neighborsAll_splitted[particleID] = neighbors;
        }
    }

    duration = (std::clock() - start) / (double)CLOCKS_PER_SEC;
    std::cout << "Elapsed time Splitted: " << duration << " [s]\n";

    // SPLITTED WITH SAMPLING

    start = std::clock();

    std::vector<double> kernelGradientsSamples;
    int resolution = 200;
    kernelGradPre(kernelType, resolution, kh, kernelGradientsSamples);

    std::vector<std::vector<int>> boxes_s;
    std::vector<std::vector<int>> surrBoxesAll_s;
    boxMesh(ll, uu, kh, boxes_s, surrBoxesAll_s);
    sortParticles(pos, ll, uu, kh, boxes_s);
    for (int box = 0; box < boxes_s.size(); box++)
    {
        for (unsigned int part = 0; part < boxes_s[box].size(); part++)
        {
            std::vector<int> neighbors;
            std::vector<double> kernelGradients;
            int particleID = boxes_s[box][part];
            findNeighbors(particleID, pos, kh, boxes_s, surrBoxesAll_s[box],
                          neighbors, kernelGradients, kernelType,
                          kernelGradientsSamples, resolution);
            kernelGradientsAll_splitted_s[particleID] = kernelGradients;
            neighborsAll_splitted_s[particleID] = neighbors;
        }
    }

    duration = (std::clock() - start) / (double)CLOCKS_PER_SEC;
    std::cout << "Elapsed time sampled Splitted: " << duration << " [s]\n\n";

    // Comparison of the neighbors
    bool naive_ll = true;
    bool naive_splitted = true;

    int nbNaive = 0;
    int nbLinked = 0;
    int nbSplitted = 0;

    for (int i = 0; i < pos.size() / 3; i++)
    {
        //std::cout << kernelGradientsAll[i][1] << " and " << kernelGradientsAll_splitted[i][1] << "\n";
        /*
                if(neighborsAll_naive[i].size() != neighborsAll_linked[i].size() && naive_ll == true){
                naive_ll = false;
                std::cout<<"Difference for " << i << "th particle between naive and linked\n";
            }
            */
        if (neighborsAll_naive[i].size() != neighborsAll_splitted[i].size() && naive_splitted == true)
        {
            naive_splitted = false;
            std::cout << "Difference for " << i << "th particle between naive and splitted\n";
        }

        nbNaive += neighborsAll_naive[i].size();
        //nbLinked += neighborsAll_linked[i].size();
        nbSplitted += neighborsAll_splitted[i].size();

        std::sort(neighborsAll_naive[i].begin(), neighborsAll_naive[i].end());
        //std::sort(neighborsAll_linked[i].begin(), neighborsAll_linked[i].end());
        std::sort(neighborsAll_splitted[i].begin(), neighborsAll_splitted[i].end());

        for (int j = 0; j < neighborsAll_naive[i].size(); j++)
        {
            /*
                if(neighborsAll_naive[i][j] != neighborsAll_linked[i][j] && naive_ll == true){
                naive_ll = false;
                std::cout<<"Difference for " << i << "th particle between naive and linked\n";
            }
            */
            if (neighborsAll_naive[i][j] != neighborsAll_splitted[i][j] && naive_splitted == true)
            {
                naive_splitted = false;
                std::cout << "Difference for " << i << "th particle between naive and splitted\n";
            }
        }
    }

    if (naive_splitted == true)
    {
        std::cout << "All functions lead to the same neighbors!\n";
    }

    std::cout << "\nNeighbor pairs for Naive: " << nbNaive << "\n";
    //std::cout<<"Neighbor pairs for Linked-list: " << nbLinked << "\n";
    std::cout << "Neighbor pairs for Splitted: " << nbSplitted << "\n";

    std::cout << "\n";

    return 0;
}
