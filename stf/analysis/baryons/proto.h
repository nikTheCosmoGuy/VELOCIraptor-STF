/*! \file proto.h
 *  \brief this file contains all function prototypes of the code

 \section TOC Function Type Table of contents
 */

#include "allvars.h"

#ifndef BPROTO_H
#define BPROTO_H

//-- Prototypes

using namespace std;
using namespace Math;
using namespace NBody;

/// \name UI subroutines
/// see \ref ui.cxx for implementation
//@{

void usage(void);
void GetArgs(const int argc, char *argv[], Options &opt);
void GetParamFile(Options &opt);
inline void ConfigCheck(Options &opt);

//@}

/// \name IO routines
/// see \ref io.cxx for implementation
//@{

///simple check to see if file exists
bool FileExists(const char *fname);

//-- Read routines

///Reads the header information
Int_t ReadHeader(Options &opt);
///Reads particle data
void ReadParticleData(Options &opt, const Int_t nbodies, Particle *Part, const Int_t ngroups, HaloParticleData *hp);
///Read tipsy file
void ReadTipsy(Options &opt, const Int_t nbodies, Particle *Part, const Int_t ngroups, HaloParticleData *hp);
///Read gadget file
void ReadGadget(Options &opt, const Int_t nbodies, Particle *Part, const Int_t ngroups, HaloParticleData *hp);
///Reads VELOCIraptor like Group Catalog Data
HaloParticleData *ReadHaloGroupCatalogData(char* infile, Int_t &numhalos, Int_t &nbodies, int mpi_ninput=0, int ibinary=1, int ifieldhalos=1);
///Reads VELOCIraptor produced properties data
PropData *ReadHaloPropertiesData(char* infile, const Int_t numhalos, int mpi_ninput, int ibinary=1, int ifieldhalos=1);

///Write Properties
void WriteProperties(Options &opt, const Int_t ngroups, PropData *pdata, int ptype, int ibound=0);
///Write radial profiles
void WriteProfiles(Options &opt, const Int_t ngroups, PropData *pdata, int ptype, int ibound=0);
///Write cylindrical profiles
void WriteCylProfiles(Options &opt, const Int_t ngroups, PropData *pdata, int ptype, int ibound=0);
///Get number of bins
inline int GetNBins(Int_t n, int ibintype=1);
//@}

/// \name for mapping ids to index routines
//@{
///map particle id to index position, need to make inline function pointer so that mapping easily altered
//inline Int_t MapPIDStoIndex(Options &opt, long unsigned &i);

//@}

///\name Binding routines in \ref binding.cxx
//@{
///subroutine that generates node list for tree gravity calculation
void GetNodeList(Node *np, Int_t &ncell, Node **nodelist, const Int_t bsize);
///subroutine that marks a cell for a given particle in tree-walk
inline void MarkCell(Node *np, Int_t *marktreecell, Int_t *markleafcell, Int_t &ntreecell, Int_t &nleafcell, Double_t *r2val, const Int_t bsize, Double_t *cR2max, Coordinate *cm, Double_t *cmtot, Coordinate xpos, Double_t eps2);
///calculate potential
void Potential(Options &opt, Int_t nbodies, Particle *Part);
///calculate potential energy for all groups
void GetPotentialEnergy(Options &opt, Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp);
///Gas energy routines, assumes that particle class has temparture
inline Double_t GetInternalEnergy(Particle &p);
///gets binding energy relative to some reference frame stored in the correctly passed PropData
void GetBindingEnergy(Options &opt, Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp);
///sorts particles according to value stored in \ref NBody::Particle.GetPotential which should contain the
///the ratio of kinetic energy to binding energy
void SortAccordingtoBindingEnergy(Options &opt, Particle *Part, Int_t ngroup, PropData **allpdata, HaloParticleData *hp, int icalcbindeflag=1, int itypesubsort=1, int ipotsort=1);
///get most bound particle of a specific type and store position/velocity in the appropriate PropData
void GetMostBoundParticle(Options &opt, Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp);
void GetBoundParticles(Options &opt, Particle *Part, Int_t ngroup, PropData **allpdata, HaloParticleData *hp, Double_t TVratio=-1.0);

//@}

///\name properties routines in \ref properties.cxx
//@{
///Gets cm, assumes that particles in a structure are sorted according to type and then binding energy
void GetCM(Options &opt, Particle *Part, Int_t ngroup,PropData *pdata,HaloParticleData *hp, int ptype=-1);
///move all particles in structure regardless of type to cm reference frame
void MovetoCMFrame(Options &opt,Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp);
///move all particles in structure regardless of type to most bound particle reference frame
void MovetoMBFrame(Options &opt,Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp);
///move all particles in structure regardless of type to particle with deepest potential well
void MovetoPotFrame(Options &opt,Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp);
///Gets bulk properties
void GetBulkProp(Options &opt, Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp, int ptype=-1);
///get radial profiles
void GetProfiles(Options &opt, Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp, int ptype=DMTYPE);
///get cylindrical profiles in angular momentum frame
void GetCylProfiles(Options &opt, Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp, int ptype=DMTYPE);

///Get spatial morphology using iterative procedure
void GetGlobalSpatialMorphology(const Int_t nbodies, Particle *p, Double_t& q, Double_t& s, Double_t Error, Matrix& eigenvec, int imflag=0);
///calculate the inertia tensor
void CalcITensor(const Int_t n, Particle *p, Double_t &a, Double_t &b, Double_t &c, Matrix& eigenvec, Matrix &I);
///calculate the weighted reduced inertia tensor assuming particles are the same mass
void CalcMTensor(Matrix& M, const Double_t q, const Double_t s, const Int_t n, Particle *p);
///calculate the weighted reduced inertia tensor
void CalcMTensorWithMass(Matrix& M, const Double_t q, const Double_t s, const Int_t n, Particle *p);
///rotate particles
void RotParticles(const Int_t n, Particle *p, Matrix &R);
///adjust particles for period using cm in PropData for reference 
void AdjustForPeriod(Options &opt, Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp);

///go to cylindrical frame set by the PropData of dark matter's angular momentum defining the z-axis
void GetCylFrame(Options &opt, Particle *Part, Int_t ngroup, PropData *pdata, HaloParticleData *hp);
//@}

///\name comparison functions
//@{
///sort in ascending particle cylindrical radius
int CylRadCompare (const void *a, const void *b);
//@}

///\name utilities
//@{
double MyGetTime();
//@}

#endif


