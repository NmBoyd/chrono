// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2016 projectchrono.org
// All right reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Nic Olsen
// =============================================================================

#include "chrono_distributed/physics/ChDomainDistr.h"
#include "chrono_distributed/physics/ChSystemDistr.h"
#include "chrono_distributed/other_types.h"

#include "chrono_parallel/ChDataManager.h"

#include "chrono/physics/ChBody.h"
#include "chrono/core/ChVector.h"

#include <iostream>
#include <stdlib.h>
#include <mpi.h>
#include <memory>

using namespace chrono;

ChDomainDistr::ChDomainDistr(ChSystemDistr *sys)
{
	this->my_sys = sys;
	long_axis = 0;
	split = false;
}

ChDomainDistr::~ChDomainDistr() {}

// Takes in the user specified coordinates of the bounding box for the simulation.
void ChDomainDistr::SetSimDomain(double xlo, double xhi, double ylo, double yhi, double zlo, double zhi)
{
	assert(!split);

	boxlo.Set(xlo, ylo, zlo);
	boxhi.Set(xhi, yhi, zhi);

	double len_x = boxhi.x() - boxlo.x();
	double len_y = boxhi.y() - boxlo.y();
	double len_z = boxhi.z() - boxlo.z();

	if (len_x <= 0 || len_y <= 0 || len_z <=0) my_sys->ErrorAbort("Invalid domain dimensions.");

	// Index of the longest domain axis 0=x, 1=y, 2=z
	long_axis = (len_x >= len_y) ? 0 : 1;
	long_axis = (len_z >= boxhi[long_axis] - boxlo[long_axis]) ? 2 : long_axis;

	SplitDomain();
}

///  Divides the domain into equal-volume, orthogonal, axis-aligned regions along
/// the longest axis. Needs to be called right after the system is created so that
/// bodies are added correctly.
void ChDomainDistr::SplitDomain()
{
	// Length of this subdomain along the long axis
	double sub_len = (boxhi[long_axis] - boxlo[long_axis]) / (double) my_sys->GetNumRanks();

	for (int i = 0; i < 3; i++)
	{
		if (long_axis == i)
		{
			sublo[i] = boxlo[i] + my_sys->GetMyRank() * sub_len;
			subhi[i] = sublo[i] + sub_len;
		}
		else
		{
			sublo[i] = boxlo[i];
			subhi[i] = boxhi[i];
		}
	}
	split = true;
}

distributed::COMM_STATUS ChDomainDistr::GetRegion(double pos)
{
	int num_ranks = my_sys->GetNumRanks();
	if (num_ranks == 1)
	{
		return distributed::OWNED;
	}
	int my_rank = my_sys->GetMyRank();
	double ghost_layer = my_sys->GetGhostLayer();
	double high = subhi[long_axis];
	double low = sublo[long_axis];

	if (my_rank != 0 && my_rank != num_ranks - 1)
	{
		if (pos >= low + ghost_layer && pos < high - ghost_layer)
		{
			return distributed::OWNED;
		}
		else if (pos >= high && pos < high + ghost_layer)
		{
			return distributed::GHOST_UP;
		}
		else if (pos >= high - ghost_layer && pos < high)
		{
			return distributed::SHARED_UP;
		}
		else if (pos >= low && pos < low + ghost_layer)
		{
			return distributed::SHARED_DOWN;
		}
		else if (pos >= low - ghost_layer && pos < low)
		{
			return distributed::GHOST_DOWN;
		}
		else if (pos >= high + ghost_layer)
		{
			return distributed::UNOWNED_UP;
		}
		else if (pos < low - ghost_layer)
		{
			return distributed::UNOWNED_DOWN;
		}
	}

	else if (my_rank == 0)
	{
		if (pos >= low && pos < high - ghost_layer)
		{
			return distributed::OWNED;
		}
		else if (pos >= high && pos < high + ghost_layer)
		{
			return distributed::GHOST_UP;
		}
		else if (pos >= high - ghost_layer && pos < high)
		{
			return distributed::SHARED_UP;
		}
		else if (pos >= high + ghost_layer)
		{
			return distributed::UNOWNED_UP;
		}
		else if (pos < low)
		{
			return distributed::UNOWNED_DOWN;
		}
	}

	else if (my_rank == num_ranks - 1)
	{
		if (pos >= low + ghost_layer && pos < high)
		{
			return distributed::OWNED;
		}
		else if (pos >= low && pos < low + ghost_layer)
		{
			return distributed::SHARED_DOWN;
		}
		else if (pos >= low - ghost_layer && pos < low)
		{
			return distributed::GHOST_DOWN;
		}
		else if (pos >= high)
		{
			return distributed::UNOWNED_UP;
		}
		else if (pos < low - ghost_layer)
		{
			return distributed::UNOWNED_DOWN;
		}
	}

	GetLog() << "Error classifying body\n";
	return distributed::UNDEFINED;
}

distributed::COMM_STATUS ChDomainDistr::GetBodyRegion(int index)
{
	return GetRegion(my_sys->data_manager->host_data.pos_rigid[index][long_axis]);
}

distributed::COMM_STATUS ChDomainDistr::GetBodyRegion(std::shared_ptr<ChBody> body)
{
	return GetRegion(body->GetPos()[long_axis]);
}


void ChDomainDistr::PrintDomain()
{
	GetLog() << "Domain:\n"
			"Box:\n"
				"\tX: " << boxlo.x() << " to " << boxhi.x() << "\n"
				"\tY: " << boxlo.y() << " to " << boxhi.y() << "\n"
				"\tZ: " << boxlo.z() << " to " << boxhi.z() << "\n"
			"Subdomain: Rank " << my_sys->GetMyRank() << "\n"
				"\tX: " << sublo.x() << " to " << subhi.x() << "\n"
				"\tY: " << sublo.y() << " to " << subhi.y() << "\n"
				"\tZ: " << sublo.z() << " to " << subhi.z() << "\n";
}
