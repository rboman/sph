#include "Main.h"
#include "Interface.h"
#include "Tools.h"
#include "paraview.h"

/*
 * In: field = stucture containing value to write
 *     t = time corresponding to the file to write
 *     filename = Name given to the file
 *     parameterFilename = Fluid parameter file used
 *     geometryFilename = geometry file used
 * Out: speed_t.vtk, pos_t.vtk, or .txt
 */
void writeField(Field *field, double t, Parameter *parameter,
                std::string const &parameterFilename,
                std::string const &geometryFilename,
                std::string const &filename)
{
    std::map<std::string, std::vector<double> *> scalars;
    std::map<std::string, std::vector<double>(*)[3]> vectors;
    Field newFieldInstance;
    Field *newField = &newFieldInstance;
    int count = 0;
    if (parameter->paraview != noParaview || parameter->matlab != noMatlab)
    {
        newField->nFree = field->nFree;
        newField->nFixed = field->nFixed;
        newField->nMoving = field->nMoving;
        newField->l[0] = field->l[0];
        newField->l[1] = field->l[1];
        newField->l[2] = field->l[2];
        newField->u[0] = field->u[0];
        newField->u[1] = field->u[1];
        newField->u[2] = field->u[2];
        newField->currentTime = field->currentTime;

        // Reserves the space for the vectors
        for (int i = 0; i < 3; i++)
        {
            newField->pos[i].reserve(field->nTotal);
            newField->speed[i].reserve(field->nTotal);
        }
        newField->density.reserve(field->nTotal);
        newField->pressure.reserve(field->nTotal);
        newField->mass.reserve(field->nTotal);
        newField->type.reserve(field->nTotal);

        for (int i = 0; i < field->pos[0].size(); ++i)
        {
            if (field->type[i] == 0)
            {
                newField->pos[0].push_back(field->pos[0][i]);
                newField->pos[1].push_back(field->pos[1][i]);
                newField->pos[2].push_back(field->pos[2][i]);
                newField->speed[0].push_back(field->speed[0][i]);
                newField->speed[1].push_back(field->speed[1][i]);
                newField->speed[2].push_back(field->speed[2][i]);
                newField->density.push_back(field->density[i]);
                newField->pressure.push_back(field->pressure[i]);
                newField->mass.push_back(field->mass[i]);
                count = count + 1;
            }
        }
        for (int i = 0; i < field->pos[0].size(); ++i)
        {
            if (field->type[i] != 0)
            {
                newField->pos[0].push_back(field->pos[0][i]);
                newField->pos[1].push_back(field->pos[1][i]);
                newField->pos[2].push_back(field->pos[2][i]);
                newField->speed[0].push_back(field->speed[0][i]);
                newField->speed[1].push_back(field->speed[1][i]);
                newField->speed[2].push_back(field->speed[2][i]);
                newField->density.push_back(field->density[i]);
                newField->pressure.push_back(field->pressure[i]);
                newField->mass.push_back(field->mass[i]);
            }
        }
    }

    // Save results to disk (ParaView or Matlab)
    if (parameter->paraview != noParaview) // .vtk in ParaView
    {
        scalars["pressure"] = &newField->pressure;
        scalars["density"] = &newField->density;
        vectors["velocity"] = &newField->speed;

        // nbr of particles should be multiple of 3
        int nbp = newField->pos[0].size(), nbpStart, nbpEnd;

        // !! CHOOSE YOUR FORMAT !!
        //PFormat format = LEGACY_TXT;
        //PFormat format = LEGACY_BIN;
        //PFormat format = XML_BIN;
        PFormat format = XML_BINZ;

        // Selection of the output format
        // Full
        if (parameter->paraview == fullParaview)
        {
            nbpStart = 0;
            nbpEnd = nbp;
            paraview(filename + "_Full", t, newField->pos, scalars, vectors, nbpStart, nbpEnd, format);
        }

        // Only nFree
        if (parameter->paraview == nFreeParaview || parameter->paraview == nFree_nMovingFixedParaview)
        {
            nbpStart = 0;
            nbpEnd = count;
            paraview(filename + "_Free", t, newField->pos, scalars, vectors, nbpStart, nbpEnd, format);
        }

        // Only nFree and nMoving
        if (parameter->paraview == nMovingFixedParaview || parameter->paraview == nFree_nMovingFixedParaview)
        {
            nbpStart = count;
            nbpEnd = nbp;
            paraview(filename + "_MovingFixed", t, newField->pos, scalars, vectors, nbpStart, nbpEnd, format);
        }
    }

    if (parameter->matlab != noMatlab) // .txt in Matlab
        matlab(filename, parameterFilename, geometryFilename, t, parameter, newField);

    // Free Memory
    newField->pos[0].clear();
    newField->pos[0].shrink_to_fit();
    newField->pos[1].clear();
    newField->pos[1].shrink_to_fit();
    newField->pos[2].clear();
    newField->pos[2].shrink_to_fit();
    newField->speed[0].clear();
    newField->speed[0].shrink_to_fit();
    newField->speed[1].clear();
    newField->speed[1].shrink_to_fit();
    newField->speed[2].clear();
    newField->speed[2].shrink_to_fit();
    newField->density.clear();
    newField->density.shrink_to_fit();
    newField->pressure.clear();
    newField->pressure.shrink_to_fit();
    newField->mass.clear();
    newField->mass.shrink_to_fit();
}

// export results to Matlab (.txt)
//   filename: file name without txt extension
//   pos:     positions (vector of size 3*number of particles)
//   speed:   velocity  (vector of size 3*number of particles)
//   density: density   (vector of size number of particles)
//   pressure:pressure  (vector of size number of particles)
//   mass:    mass      (vector of size number of particles)
//   step:    time step number
void matlab(std::string const &filename,
            std::string const &parameterFilename,
            std::string const &geometryFilename,
            int step, Parameter *parameter, Field *field)
{
    int nbp = field->pos[0].size();

    // Set Chronos and time variable
    std::chrono::time_point<std::chrono::system_clock> start, end;

    // Start Chrono
    start = std::chrono::system_clock::now();

    // Date
    std::time_t Date = std::chrono::system_clock::to_time_t(start);
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    // build file name + stepno + vtk extension
    std::stringstream s;
    s << "Results/" << filename << "_" << std::setw(8) << std::setfill('0') << step << ".txt";

    // open file
    //std::cout << "Writing results to " << s.str() << std::endl;
    std::ofstream f(s.str().c_str());
    f << std::scientific;

    //Record Time
    double duration = (std::clock() - startExperimentTimeClock) / (double)CLOCKS_PER_SEC;
    // header
    f << "#EXPERIMENT: " << filename << std::endl;
    f << std::endl;
    f << "Date : " << asctime(timeinfo);
#if defined(_WIN32) || defined(WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
    f << "Computer Name : " << getenv("COMPUTERNAME") << std::endl;
    f << "Username : " << getenv("USERNAME") << std::endl;
#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
    f << "Computer Name : None" << std::endl; // To be implemented
    f << "Username : None" << std::endl; // To be implemented
#else
#error "Cannot define GetMemory( ) or GetMemoryProcessPeak( ) or GetMemoryProcess() for an unknown OS."
#endif
    f << "File Used : " << geometryFilename << "   &   " << parameterFilename << std::endl;
    f << std::endl;
    f << "CPU Time : " << duration << " [s]" << std::endl;
    f << "Memory Usage : " << GetMemoryProcess(false, false) << " [kB]" << std::endl;
    f << "Memory Usage Peak : " << GetMemoryProcessPeak(false, false) << " [kB]" << std::endl;
    f << std::endl;
    f << "Step Time (k) : " << parameter->k << " [s]" << std::endl;
    f << "Write interval : " << parameter->writeInterval << " [s]" << std::endl;
    f << "Simulation Time (T) : " << parameter->T << " [s]" << std::endl;
    f << "Current Time Simulation : " << field->currentTime << " [s]" << std::endl;
    f << std::endl;
    f << "Domain (lower l) : " << field->l[0] << "   " << field->l[1] << "   " << field->l[2] << "    [m]" << std::endl;
    f << "Domain (upper u) : " << field->u[0] << "   " << field->u[1] << "   " << field->u[2] << "    [m]" << std::endl;
    f << "Number of Particules (nFree/nMoving/nFixed) : " << field->nFree << "   " << field->nMoving << "   " << field->nFixed << std::endl;
    f << "\n";
    f << " posX\t        posY\t        posZ\t     velocityX\t     velocityY\t     velocityZ\t     density\t     pressure\t     mass" << std::endl;

    // Fill f:
    for (int i = 0; i < nbp; ++i)
    {
        f << field->pos[0][i] << "\t" << field->pos[1][i] << "\t" << field->pos[2][i] << "\t"
          << field->speed[0][i] << "\t" << field->speed[1][i] << "\t" << field->speed[2][i] << "\t"
          << field->density[i] << "\t"
          << field->pressure[i] << "\t"
          << field->mass[i] << "\t" << std::endl;
    }

    // End Chrono
    end = std::chrono::system_clock::now();

    // Write result on file
    std::chrono::duration<double> elapsed_seconds = end - start;
    std::time_t end_time = std::chrono::system_clock::to_time_t(end);
    //std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";

    f.close();
}
