///**************************************************************************
/// SOURCE: Functions to initialise a field and compute pressure from density.
///**************************************************************************
#include "Main.h"
#include "Physics.h"
#define R 8.314
/*
*Input:
*- field: field whose speeds will be initialised
*- parameter: pointer the the structure containing parameters
*Decscription:
*Initialise speed from field.
*/
void speedInit(Field *field, Parameter *parameter)
{
	for (int j = 0; j < 3; j++)
		// Initial state is zero speed; other choice could be implemented
		field->speed[j].assign(field->nTotal, 0.0);
	if (field->nMoving != 0)
	{
		int start = field->nFree + field->nFixed;
		int end = field->nTotal;
		for (int i = start; i < end; i++)
		{
			updateMovingSpeed(field, parameter, 0.0, 0.0, i);
		}
	}
}

/*
*Input:
*- field: field whose densities will be initialised
*- parameter: pointer the the structure containing parameters
*Decscription:
*Initialise densities from field.
*/
void densityInit(Field *field, Parameter *parameter)
{
	//Parameter withdrawal
	double rho_0 = parameter->densityRef;
	double B = parameter->B;
	double gamma = parameter->gamma;
	double g = parameter->g;

	switch (parameter->densityInitMethod)
	{
	case hydrostatic:
	{
		double zMax = 0.0;
		double H;

		for (int j = 0; j < field->nTotal; j++)
		{
			if (field->type[j] == freePart && field->pos[2][j] > zMax)
			{
				zMax = field->pos[2][j];
			}
		}
		switch (parameter->stateEquationMethod)
		{
		case quasiIncompressible:

			for (int i = 0; i < field->nTotal; i++)
			{
				if (field->type[i] == freePart)
				{
					H = zMax - field->pos[2][i];
					double rho = (1 + (1 / B) * rho_0 * g * H);
					field->density.push_back(rho_0 * pow(rho, 1.0 / gamma));
				}
			}
			break;

		case perfectGas:
			for (int i = 0; i < field->nTotal; i++)
			{
				if (field->type[i] == freePart)
				{
					H = zMax - field->pos[2][i];
					double rho = rho_0 * (1 + (parameter->molarMass / R / parameter->temperature) * rho_0 * g * H);
					field->density.push_back(rho);
				}
			}
			break;
		}
		// Boundaries have constant densities
		for (int k = 0; k < field->nTotal; k++)
		{
			if (field->type[k] != freePart)
				field->density.push_back(parameter->densityRef);
		}
	}
	break;

	case homogeneous:
		field->density.assign(field->nTotal, rho_0);
		break;
	}
}

/*
*Input:
*- field: field whose pressure will be initialised
*- parameter: pointer the the structure containing parameters
*Decscription:
*Initialise pressure from field.
*/
void pressureInit(Field *field, Parameter *parameter)
{
	field->pressure.resize(field->nTotal);
	for (int i = 0; i < field->nTotal; i++)
	{
		pressureComputation(field, parameter, i);
	}
}

/*
*Input:
*- field: field whose pressure will be updated
*- parameter: pointer the the structure containing parameters
*Decscription:
*Compute pressure from field.
*/
void pressureComputation(Field *field, Parameter *parameter, int particleID)
{
	//Parameter withdrawal
	double rho_0 = parameter->densityRef;
	double B = parameter->B;
	double gamma = parameter->gamma;
	double g = parameter->g;

	switch (parameter->stateEquationMethod)
	{
	case quasiIncompressible:
	{
		double rho = field->density[particleID];
		double p = B * (pow(rho / rho_0, gamma) - 1);
		field->pressure[particleID] = p;
	}
	break;

	case perfectGas:
	{
		double rho = field->density[particleID];
		double p = rho * R * parameter->temperature / parameter->molarMass;
		field->pressure[particleID] = p;
	}
	break;
	}
}

/*
*Input:
*- field: field whose masses are initialised
*- parameter: pointer the the structure containing parameters
*Decscription:
*Initialise mass from field.
*/
void massInit(Field *field, Parameter *parameter, std::vector<double> &vol)
{
	for (int i = 0; i < field->nTotal; i++)
	{
		double m = field->density[i] * vol[i];
		field->mass.push_back(m);
	}
}
