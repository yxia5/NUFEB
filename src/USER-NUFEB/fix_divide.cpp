/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "math.h"
#include "string.h"
#include "stdlib.h"
#include "fix_divide.h"
#include "atom.h"
#include "atom_vec.h"
#include "update.h"
#include "group.h"
#include "modify.h"
#include "force.h"
#include "pair.h"
#include "pair_hybrid.h"
#include "kspace.h"
#include "fix_store.h"
#include "input.h"
#include "variable.h"
#include "respa.h"
#include "comm.h"
#include "domain.h"
#include "region.h"
#include "region_block.h"
#include "region_cylinder.h"
#include "random_park.h"
#include "math_extra.h"
#include "math_const.h"
#include "memory.h"
#include "error.h"

using namespace LAMMPS_NS;
using namespace FixConst;
using namespace MathConst;

#define EPSILON 0.001
#define DELTA 1.005

// enum{PAIR,KSPACE,ATOM};
// enum{DIAMETER,CHARGE};

/* ---------------------------------------------------------------------- */

FixDivide::FixDivide(LAMMPS *lmp, int narg, char **arg) : Fix(lmp, narg, arg)
{
  if (narg != 7) error->all(FLERR,"Illegal fix divide command: Missing arguments");

  nevery = force->inumeric(FLERR,arg[3]);
  if (nevery < 0) error->all(FLERR,"Illegal fix divide command: calling steps should be positive integer");

  int n = strlen(&arg[4][2]) + 1;
  var = new char[n];
  strcpy(var,&arg[4][2]);

  growthFactor = atof(arg[5]);

  seed = atoi(arg[6]);

  if (seed <= 0) error->all(FLERR,"Illegal fix divide command: seed should be greater than 0");

  // preExchangeCalled = false;

  // Random number generator, same for all procs
  random = new RanPark(lmp,seed);  

  find_maxid();

  if (domain->triclinic == 0) {
  	xlo = domain->boxlo[0];
  	xhi = domain->boxhi[0];
  	ylo = domain->boxlo[1];
  	yhi = domain->boxhi[1];
  	zlo = domain->boxlo[2];
  	zhi = domain->boxhi[2];
  }
  else {
  	xlo = domain->boxlo_bound[0];
  	xhi = domain->boxhi_bound[0];
  	ylo = domain->boxlo_bound[1];
  	yhi = domain->boxhi_bound[1];
  	zlo = domain->boxlo_bound[2];
  	zhi = domain->boxhi_bound[2];
  }
   
  force_reneighbor = 1;
  next_reneighbor = update->ntimestep+1;
}

/* ---------------------------------------------------------------------- */

FixDivide::~FixDivide()
{
  delete random;
  delete [] var;
}

/* ---------------------------------------------------------------------- */

int FixDivide::setmask()
{
  int mask = 0;
  mask |= PRE_EXCHANGE;
  return mask;
}

/* ----------------------------------------------------------------------
   if need to restore per-atom quantities, create new fix STORE styles
------------------------------------------------------------------------- */


void FixDivide::init()
{
     // fprintf(stdout, "called once?\n");
  if (!atom->radius_flag)
    error->all(FLERR,"Fix divide requires atom attribute diameter");

  ivar = input->variable->find(var);
  if (ivar < 0)
    error->all(FLERR,"Variable name for fix divide does not exist");
  if (!input->variable->equalstyle(ivar))
    error->all(FLERR,"Variable for fix divide is invalid style");

}

void FixDivide::pre_exchange()
{
  if (next_reneighbor != update->ntimestep) return;

  double EPSdens = input->variable->compute_equal(ivar);
  double density;
  int nlocal = atom->nlocal;
  int nall = nlocal + atom->nghost;
  int i;

  double averageMass;// = getAverageMass();
  // int nnew = countNewAtoms(averageMass);

  double *sublo,*subhi;
  if (domain->triclinic == 0) {
    sublo = domain->sublo;
    subhi = domain->subhi;
  } else {
    sublo = domain->sublo_lamda;
    subhi = domain->subhi_lamda;
  }

  for (i = 0; i < nall; i++) {
    if (atom->type[i] == 1 || atom->type[i] == 2 || atom->type[i] == 3) {
      averageMass = 1e-16;
    } else continue;

    if ((atom->mask[i] & groupbit) &&
    	  atom->x[i][0] >= sublo[0] && atom->x[i][0] < subhi[0] &&
          atom->x[i][1] >= sublo[1] && atom->x[i][1] < subhi[1] &&
          atom->x[i][2] >= sublo[2] && atom->x[i][2] < subhi[2]) {
      density = atom->rmass[i] / (4.0*MY_PI/3.0 *
      					atom->radius[i]*atom->radius[i]*atom->radius[i]);

      if (atom->rmass[i] >= growthFactor * averageMass) {
      	double newX, newY, newZ;

        double splitF = 0.4 + (random->uniform()*0.2);
        double parentMass = atom->rmass[i] * splitF;
        double childMass = atom->rmass[i] - parentMass;

        double parentOuterMass = atom->outerMass[i] * splitF;
        double childOuterMass = atom->outerMass[i] - parentOuterMass;

        double parentSub = atom->sub[i];
        double childSub =  atom->sub[i];

        double parentO2 = atom->o2[i];
        double childO2 =  atom->o2[i];

        double parentNH4 = atom->nh4[i];
        double childNH4 =  atom->nh4[i];

        double parentNO2 = atom->no2[i];
        double childNO2 =  atom->no2[i];

        double parentNO3 = atom->no3[i];
        double childNO3 =  atom->no3[i];

        double parentfx = atom->f[i][0] * splitF;
        double childfx =  atom->f[i][0] - parentfx;

        double parentfy = atom->f[i][1] * splitF;
        double childfy =  atom->f[i][1] - parentfy;

        double parentfz = atom->f[i][2] * splitF;
        double childfz =  atom->f[i][2] - parentfz;

        double thetaD = random->uniform() * 2*MY_PI;
        double phiD = random->uniform() * (MY_PI);

        double oldX = atom->x[i][0];
        double oldY = atom->x[i][1];
        double oldZ = atom->x[i][2];

        //double separation = radius[i] * 0.005;

        //Update parent
        atom->rmass[i] = parentMass;
        atom->outerMass[i] = parentOuterMass;
        atom->sub[i] = parentSub;
        atom->o2[i] = parentO2;
        atom->nh4[i] = parentNH4;
        atom->no2[i] = parentNO2;
        atom->no3[i] = parentNO3;
        atom->f[i][0] = parentfx;
        atom->f[i][1] = parentfy;
        atom->f[i][2] = parentfz;
        atom->radius[i] = pow(((6*atom->rmass[i])/(density*MY_PI)),(1.0/3.0))*0.5;
        atom->outerRadius[i] = pow((3.0/(4.0*MY_PI))*((atom->rmass[i]/density)+(parentOuterMass/EPSdens)),(1.0/3.0));
        newX = oldX + (atom->outerRadius[i]*cos(thetaD)*sin(phiD)*DELTA);
        newY = oldY + (atom->outerRadius[i]*sin(thetaD)*sin(phiD)*DELTA);
        newZ = oldZ + (atom->outerRadius[i]*cos(phiD)*DELTA);
        if (newX - atom->outerRadius[i] < xlo) {
        	newX = xlo + atom->outerRadius[i];
        }
        else if (newX + atom->outerRadius[i] > xhi) {
        	newX = xhi - atom->outerRadius[i];
        }
        if (newY - atom->outerRadius[i] < ylo) {
        	newY = ylo + atom->outerRadius[i];
        }
        else if (newY + atom->outerRadius[i] > yhi) {
        	newY = yhi - atom->outerRadius[i];
        }
        if (newZ - atom->outerRadius[i] < zlo) {
        	newZ = zlo + atom->outerRadius[i];
        }
        else if (newZ + atom->outerRadius[i] > zhi) {
        	newZ = zhi - atom->outerRadius[i];
        }
        atom->x[i][0] = newX;
        atom->x[i][1] = newY;
        atom->x[i][2] = newZ;
     //   fprintf(stdout, "Diameter of atom: %f\n", radius[i]*2);

        // fprintf(stdout, "Moved and resized parent\n");

        //create child
        double childRadius = pow(((6*childMass)/(density*MY_PI)),(1.0/3.0))*0.5;
        double childOuterRadius = pow((3.0/(4.0*MY_PI))*((childMass/density)+(childOuterMass/EPSdens)),(1.0/3.0));
        double* coord = new double[3];
        newX = oldX - (childOuterRadius*cos(thetaD)*sin(phiD)*DELTA);
        newY = oldY - (childOuterRadius*sin(thetaD)*sin(phiD)*DELTA);
        newZ = oldZ - (childOuterRadius*cos(phiD)*DELTA);
        if (newX - childOuterRadius < xlo) {
        	newX = xlo + childOuterRadius;
        }
        else if (newX + childOuterRadius > xhi) {
        	newX = xhi - childOuterRadius;
        }
        if (newY - childOuterRadius < ylo) {
        	newY = ylo + childOuterRadius;
        }
        else if (newY + childOuterRadius > yhi) {
        	newY = yhi - childOuterRadius;
        }
        if (newZ - childOuterRadius < zlo) {
        	newZ = zlo + childOuterRadius;
        }
        else if (newZ + childOuterRadius > zhi) {
        	newZ = zhi - childOuterRadius;
        }
        coord[0] = newX;
        coord[1] = newY;
        coord[2] = newZ;
        find_maxid();
        atom->avec->create_atom(atom->type[i],coord);
        // fprintf(stdout, "Created atom\n");
        int n = atom->nlocal - 1;
        atom->tag[n] = maxtag_all+1;
        atom->mask[n] = atom->mask[i];
        atom->image[n] = atom->image[i];

        atom->v[n][0] = atom->v[i][0];
        atom->v[n][1] = atom->v[i][1];
        atom->v[n][2] = atom->v[i][2];
        atom->f[n][0] = atom->f[i][0];
        atom->f[n][1] = atom->f[i][1];
        atom->f[n][2] = atom->f[i][2];

        atom->omega[n][0] = atom->omega[i][0];
        atom->omega[n][1] = atom->omega[i][1];
        atom->omega[n][2] = atom->omega[i][2];

        atom->rmass[n] = childMass;
        atom->outerMass[n] = childOuterMass;

        atom->sub[n] = childSub;
        atom->o2[n] = childO2;
        atom->nh4[n] = childNH4;
        atom->no2[n] = childNO2;
        atom->no3[n] = childNO3;

        atom->f[n][0] = childfx;
        atom->f[n][1] = childfy;
        atom->f[n][2] = childfz;

        atom->torque[n][0] = atom->torque[i][0];
        atom->torque[n][1] = atom->torque[i][1];
        atom->torque[n][2] = atom->torque[i][2];

        atom->radius[n] = childRadius;
        atom->outerRadius[n] = childOuterRadius;

        atom->natoms++;

        delete[] coord;
      }
    }
  }
	//fprintf(stdout, "after divide ,overlap pair= %i\n", overlap());
	if (atom->map_style) {
		atom->nghost = 0;
		atom->map_init();
		atom->map_set();
	}
  next_reneighbor += nevery;
}


/* ----------------------------------------------------------------------
   maxtag_all = current max atom ID for all atoms
------------------------------------------------------------------------- */


void FixDivide::find_maxid()
{
  tagint *tag = atom->tag;
  tagint *molecule = atom->molecule;
  int nlocal = atom->nlocal;

  tagint max = 0;
  for (int i = 0; i < nlocal; i++) max = MAX(max,tag[i]);
  MPI_Allreduce(&max,&maxtag_all,1,MPI_LMP_TAGINT,MPI_MAX,world);
}


int FixDivide::overlap()
{

	int n = 0;
	int** ptr = new int*[atom->nlocal];
	for(int m =0; m < atom->nlocal; m++)
	{
		ptr[m] = new int[atom->nlocal];
	}

	for(int i = 0; i < atom->nlocal; i++){
		for(int j = 0; j < atom->nlocal; j++){
			ptr[i][j] = 0;
		}
	}

	for(int i = 0; i < atom->nlocal; i++){
		for(int j = 0; j < atom->nlocal; j++){
			if(i != j){
				double xd = atom->x[i][0] - atom->x[j][0];
				double yd = atom->x[i][1] - atom->x[j][1];
				double zd = atom->x[i][2] - atom->x[j][2];

				double rsq = (xd*xd + yd*yd + zd*zd);
				double cut = (atom->radius[i] + atom->radius[j] + 5.0e-7) * (atom->radius[i] + atom->radius[j]+ 5.0e-7);

				if (rsq <= cut && ptr[i][j] == 0 && ptr[j][i] == 0) {
					n++;
					ptr[i][j] = 1;
					ptr[j][i] = 1;

					//fprintf(stdout, "overlap! i=%i ,j= %i, rsq=%e, cut=%e, bool = %i\n", i, j, rsq, cut, ptr[j][i]);
				}
			}
		}
	}
	for (int i = 0; i < atom->nlocal; i++) {
	  delete[] ptr[i];
	}
	delete[] ptr;
	return n;
}



