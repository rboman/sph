///**************************************************************************
/// HEADER: Function That Load/Read Input Files And Generate Output Files
///**************************************************************************

#ifndef INTERFACE_H
#define INTERFACE_H
#include "Structures.h"
#include <map>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <sstream>
#include <iomanip>

// inputReader.cpp
Error readParameter(std::string filename, Parameter *parameter);
Error readGeometry(std::string filename, Field *currentField, Parameter *parameter, std::vector<double> *volVector);
Error initializeField(std::string filename, Field *currentField, Parameter *parameter);

// writeField.cpp
std::string creatDirectory(std::string dirname);

void writeField(Field *field, double t, Parameter *parameter,
                std::string const &parameterFilename = "Undefined",
                std::string const &geometryFilename = "Undefined",
                std::string const &filename = "result");

void matlab(std::string const &filename,
            std::string const &parameterFilename,
            std::string const &geometryFilename,
            int step, Parameter *parameter, Field *field);

// ConsistencyCheck.cpp
Error consistencyParameters(Parameter *param);
Error consistencyField(Field *field);

#endif
