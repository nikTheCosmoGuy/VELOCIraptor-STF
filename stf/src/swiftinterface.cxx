/*! \file swiftinterface.cxx
 *  \brief this file contains routines that allow the velociraptor library to interface with the swift N-body code from within swift.
 */


#include "swiftinterface.h"

Options libvelociraptorOpt;
void InitVelociraptor(char* configname, char* outputname, cosmoinfo c, unitinfo u, siminfo s)
{

#ifdef USEMPI
    //find out how big the SPMD world is
    MPI_Comm_size(MPI_COMM_WORLD,&NProcs);
    //mpi_domain=new MPI_Domain[NProcs];
    mpi_nlocal=new Int_t[NProcs];
    mpi_nsend=new Int_t[NProcs*NProcs];
    mpi_ngroups=new Int_t[NProcs];
    //and this processes' rank is
    MPI_Comm_rank(MPI_COMM_WORLD,&ThisTask);
    //store MinSize as when using mpi prior to stitching use min of 2;
    MinNumMPI=2;

#endif
    cout<<"Initialising VELOCIraptor..."<< endl;

    libvelociraptorOpt.pname = configname;
    libvelociraptorOpt.outname = outputname;

    cout<<"Reading VELOCIraptor config file..."<< endl;
    GetParamFile(libvelociraptorOpt);
    cout<<"Setting cosmology, units, sim stuff "<<endl;
    ///set units, here idea is to convert internal units so that have kpc, km/s, solar mass
    libvelociraptorOpt.lengthtokpc=1.0;
    libvelociraptorOpt.velocitytokms=1.0;
    libvelociraptorOpt.masstosolarmass=1.0;
    libvelociraptorOpt.L=u.lengthtokpc;
    libvelociraptorOpt.M=u.masstosolarmass;
    libvelociraptorOpt.V=u.velocitytokms;
    ///these should be in units of kpc, km/s, and solar mass
    libvelociraptorOpt.G=u.gravity;
    libvelociraptorOpt.H=u.hubbleunit;
    ///set cosmology
    libvelociraptorOpt.a=c.atime;
    libvelociraptorOpt.h=c.littleh;
    libvelociraptorOpt.Omega_m=c.Omega_m;
    libvelociraptorOpt.Omega_b=c.Omega_b;
    libvelociraptorOpt.Omega_cdm=c.Omega_cdm;
    libvelociraptorOpt.Omega_Lambda=c.Omega_Lambda;
    libvelociraptorOpt.w_de=c.w_de;

    //if libvelociraptorOpt.virlevel<0, then use virial overdensity based on Bryan and Norman 1998 virialization level is given by
    if (libvelociraptorOpt.virlevel<0)
    {
        Double_t bnx=-((1-libvelociraptorOpt.Omega_m-libvelociraptorOpt.Omega_Lambda)*pow(libvelociraptorOpt.a,-2.0)+libvelociraptorOpt.Omega_Lambda)/((1-libvelociraptorOpt.Omega_m-libvelociraptorOpt.Omega_Lambda)*pow(libvelociraptorOpt.a,-2.0)+libvelociraptorOpt.Omega_m*pow(libvelociraptorOpt.a,-3.0)+libvelociraptorOpt.Omega_Lambda);
        libvelociraptorOpt.virlevel=(18.0*M_PI*M_PI+82.0*bnx-39*bnx*bnx)/libvelociraptorOpt.Omega_m;
    }
    //set some sim information
    libvelociraptorOpt.p=s.period;
    libvelociraptorOpt.zoomlowmassdm=s.zoomhigresolutionmass;
    libvelociraptorOpt.icosmologicalin=s.icosmologicalsim;
    libvelociraptorOpt.ellxscale=s.interparticlespacing;
    libvelociraptorOpt.uinfo.eps*=libvelociraptorOpt.ellxscale;
    if (libvelociraptorOpt.icosmologicalin) {
        libvelociraptorOpt.ellxscale*=libvelociraptorOpt.a;
        libvelociraptorOpt.uinfo.eps*=libvelociraptorOpt.a;
        double Hubble=libvelociraptorOpt.h*libvelociraptorOpt.H*sqrt((1-libvelociraptorOpt.Omega_m-libvelociraptorOpt.Omega_Lambda)*pow(libvelociraptorOpt.a,-2.0)+libvelociraptorOpt.Omega_m*pow(libvelociraptorOpt.a,-3.0)+libvelociraptorOpt.Omega_Lambda);
        libvelociraptorOpt.rhobg=3.*Hubble*Hubble/(8.0*M_PI*libvelociraptorOpt.G)*libvelociraptorOpt.Omega_m;
    }
    else {
        libvelociraptorOpt.rhobg=1.0;
    }
    //assume above is in comoving is cosmological simulation and then need to correct to physical
    if (libvelociraptorOpt.icosmologicalin) {
        libvelociraptorOpt.p*=libvelociraptorOpt.a;
        libvelociraptorOpt.ellxscale*=libvelociraptorOpt.a;
        libvelociraptorOpt.uinfo.eps*=libvelociraptorOpt.a;
    }
    libvelociraptorOpt.uinfo.icalculatepotential=true;

    // Set mesh information.
    libvelociraptorOpt.spacedimension[0] = s.spacedimension[0];
    libvelociraptorOpt.spacedimension[1] = s.spacedimension[1];
    libvelociraptorOpt.spacedimension[2] = s.spacedimension[2];
    libvelociraptorOpt.cellwidth[0] = s.cellwidth[0];
    libvelociraptorOpt.cellwidth[1] = s.cellwidth[1];
    libvelociraptorOpt.cellwidth[2] = s.cellwidth[2];
    libvelociraptorOpt.icellwidth[0] = s.icellwidth[0];
    libvelociraptorOpt.icellwidth[1] = s.icellwidth[1];
    libvelociraptorOpt.icellwidth[2] = s.icellwidth[2];
    libvelociraptorOpt.numcells = s.numcells;
    libvelociraptorOpt.numcellsperdim = cbrt(s.numcells);
    libvelociraptorOpt.cellloc = s.cellloc;

    cout<<"Finished initialising VELOCIraptor"<<endl;
    if (libvelociraptorOpt.HaloMinSize==-1) libvelociraptorOpt.HaloMinSize=libvelociraptorOpt.MinSize;

#ifdef USEMPI
    //if single halo, use minsize to initialize the old minimum number
    //else use the halominsize since if mpi and not single halo, halos localized to mpi domain for substructure search
    if (libvelociraptorOpt.iSingleHalo) MinNumOld=libvelociraptorOpt.MinSize;
    else MinNumOld=libvelociraptorOpt.HaloMinSize;
    mpi_period = libvelociraptorOpt.p;
#endif

    //write velociraptor info
    WriteVELOCIraptorConfig(libvelociraptorOpt);
    WriteSimulationInfo(libvelociraptorOpt);
    WriteUnitInfo(libvelociraptorOpt);

}

void InvokeVelociraptor(const int num_gravity_parts, struct gpart *gravity_parts, const int *cell_node_ids) {
#ifndef USEMPI
    int ThisTask=0;
    int NProcs=1;
    int Nlocal, Ntotal, Nmemlocal;
#endif
    int nthreads;
#ifdef USEOPENMP
#pragma omp parallel
{
    if (omp_get_thread_num()==0) nthreads=omp_get_num_threads();
}
#else
    nthreads=1;
#endif

    Particle *parts;
    Int_t *pfof,*numingroup,**pglist;
    Int_t ngroup, nhalos;
    //Int_t nbodies,nbaryons,ndark;
    //KDTree *tree;
    //to store information about the group
    PropData *pdata=NULL,*pdatahalos=NULL;
    double time1;
    Coordinate minc,maxc,avec,sumave,sumsigma,totave,totsigma,totmin,totmax;


    /// Set pointer to cell node IDs
    libvelociraptorOpt.cellnodeids = cell_node_ids;

    Nlocal=Nmemlocal=num_gravity_parts;
    Nmemlocal*=(1+libvelociraptorOpt.mpipartfac); /* JSW: Not set in parameter file. */

    //get spatial statistics from gparts
    for (auto j=0;j<3;j++) {maxc[j]=0;minc[j]=libvelociraptorOpt.p;avec[j]=0;sumave[j]=sumsigma[j]=0;}
    for(auto i=0; i<Nlocal; i++) {
        for (auto j=0;j<3;j++) {
            if (gravity_parts[i].x[j]>maxc[j]) maxc[j]=gravity_parts[i].x[j];
            if (gravity_parts[i].x[j]<minc[j]) minc[j]=gravity_parts[i].x[j];
            avec[j]+=gravity_parts[i].x[j];
            sumave[j]+=gravity_parts[i].x[j];
            sumsigma[j]+=gravity_parts[i].x[j]*gravity_parts[i].x[j];
        }
    }
    avec=avec*(1.0/(double)Nlocal);
    cout<<"Local gravity_parts based MPI domain Stats of positions (min,ave,max)"<<endl;
    for (auto j=0;j<3;j++) {
        cout<<j<<" : "<<minc[j]<<", "<<avec[j]<<", "<<maxc[j]<<endl;
    }
#ifdef USEMPI
    for (auto j=0;j<3;j++) {
        double blahx;
        MPI_Allreduce(&sumave[j], &blahx, 1, MPI_Real_t, MPI_SUM, MPI_COMM_WORLD);
        totave[j]=blahx;
        MPI_Allreduce(&sumsigma[j], &blahx, 1, MPI_Real_t, MPI_SUM, MPI_COMM_WORLD);
        totsigma[j]=blahx;
        MPI_Allreduce(&minc[j], &blahx, 1, MPI_Real_t, MPI_MIN, MPI_COMM_WORLD);
        totmin[j]=blahx;
        MPI_Allreduce(&maxc[j], &blahx, 1, MPI_Real_t, MPI_MAX, MPI_COMM_WORLD);
        totmax[j]=blahx;
    }
#else
    totave=sumave;
    totsigma=sumsigma;
    totmin=minc;
    totmax=maxc;
#endif
#ifdef USEMPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif
    if (ThisTask==0) {
        cout.precision(10);
        cout<<"Global gravity_parts stats of positions (min,ave,sigma,max)"<<endl;
        for (auto j=0;j<3;j++) {
            cout<<j<<" : "<<totmin[j]<<", "<<totave[j]/(double)Ntotal<<", "<<totsigma[j]/(double)Ntotal<<", "<<totmax[j]<<endl;
        }
    }
    #ifdef USEMPI
        MPI_Barrier(MPI_COMM_WORLD);
    #endif

    parts=new Particle[Nmemlocal];
    cout<<"Copying particle data..."<< endl;
    time1=MyGetTime();
    for(auto i=0; i<Nlocal; i++) {
        parts[i] = Particle(gravity_parts[i], libvelociraptorOpt.L, libvelociraptorOpt.V, libvelociraptorOpt.M, libvelociraptorOpt.icosmologicalin,libvelociraptorOpt.a,libvelociraptorOpt.h);
        parts[i].SetType(DARKTYPE);
    }
    time1=MyGetTime()-time1;
    cout<<"Finished copying particle data."<< endl;
#ifdef USEMPI
    MPI_Allreduce(&num_gravity_parts, &Ntotal, 1, MPI_Int_t, MPI_SUM, MPI_COMM_WORLD);
#endif
    Nlocal=num_gravity_parts; /* JSW: Already set previously to same value. */
    cout<<"TIME::"<<ThisTask<<" took "<<time1<<" to copy "<<Nlocal<<" particles from SWIFT to a local format. Out of "<<Ntotal<<endl;
    cout<<ThisTask<<" There are "<<Nlocal<<" particles and have allocated enough memory for "<<Nmemlocal<<" requiring "<<Nmemlocal*sizeof(Particle)/1024./1024./1024.<<"GB of memory "<<endl;
    //if (libvelociraptorOpt.iBaryonSearch>0) cout<<ThisTask<<"There are "<<Nlocalbaryon[0]<<" baryon particles and have allocated enough memory for "<<Nmemlocalbaryon<<" requiring "<<Nmemlocalbaryon*sizeof(Particle)/1024./1024./1024.<<"GB of memory "<<endl;
    cout<<ThisTask<<" will also require additional memory for FOF algorithms and substructure search. Largest mem needed for preliminary FOF search. Rough estimate is "<<Nlocal*(sizeof(Int_tree_t)*8)/1024./1024./1024.<<"GB of memory"<<endl;

    //
    // Calculate some statistics: min, max, avg positions and particle potentials.
    //
    for (auto j=0;j<3;j++) {maxc[j]=0;minc[j]=libvelociraptorOpt.p;avec[j]=0;sumave[j]=sumsigma[j]=0;}
    for(auto i=0; i<Nlocal; i++) {
        for (auto j=0;j<3;j++) {
            if (parts[i].GetPosition(j)>maxc[j]) maxc[j]=parts[i].GetPosition(j);
            if (parts[i].GetPosition(j)<minc[j]) minc[j]=parts[i].GetPosition(j);
            avec[j]+=parts[i].GetPosition(j);
            sumave[j]+=parts[i].GetPosition(j);
            sumsigma[j]+=parts[i].GetPosition(j)*parts[i].GetPosition(j);
        }
    }
    avec=avec*(1.0/(double)Nlocal);
    cout<<"Local MPI domain Stats of positions (min,ave,max)"<<endl;
    for (auto j=0;j<3;j++) {
        cout<<j<<" : "<<minc[j]<<", "<<avec[j]<<", "<<maxc[j]<<endl;
    }
    Double_t minphi,maxphi,avephi;
    minphi=1e16;maxphi=-1e16;avephi=0;
    for(auto i=0; i<Nlocal; i++) {
        if (parts[i].GetGravityPotential()>maxphi) maxphi=parts[i].GetGravityPotential();
        if (parts[i].GetGravityPotential()<minphi) minphi=parts[i].GetGravityPotential();
        avephi+=parts[i].GetGravityPotential();
    }
    avephi=avephi*(1.0/(double)Nlocal);
    cout<<"Stats of potential "<<minphi<<" "<<maxphi<<" "<<avephi<<endl;
#ifdef USEMPI
    for (auto j=0;j<3;j++) {
        double blahx;
        MPI_Allreduce(&sumave[j], &blahx, 1, MPI_Real_t, MPI_SUM, MPI_COMM_WORLD);
        totave[j]=blahx;
        MPI_Allreduce(&sumsigma[j], &blahx, 1, MPI_Real_t, MPI_SUM, MPI_COMM_WORLD);
        totsigma[j]=blahx;
        MPI_Allreduce(&minc[j], &blahx, 1, MPI_Real_t, MPI_MIN, MPI_COMM_WORLD);
        totmin[j]=blahx;
        MPI_Allreduce(&maxc[j], &blahx, 1, MPI_Real_t, MPI_MAX, MPI_COMM_WORLD);
        totmax[j]=blahx;
    }
#else
    totave=sumave;
    totsigma=sumsigma;
    totmin=minc;
    totmax=maxc;
#endif
#ifdef USEMPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif
    if (ThisTask==0) {
        cout.precision(10);
        cout<<"Global stats of positions (min,ave,sigma,max)"<<endl;
        for (auto j=0;j<3;j++) {
            cout<<j<<" : "<<totmin[j]<<", "<<totave[j]/(double)Ntotal<<", "<<totsigma[j]/(double)Ntotal<<", "<<totmax[j]<<endl;
        }
    }
    #ifdef USEMPI
        MPI_Barrier(MPI_COMM_WORLD);
    #endif
    //
    // Perform FOF search.
    //
    time1=MyGetTime();
    pfof=SearchFullSet(libvelociraptorOpt,Nlocal,parts,ngroup);
    time1=MyGetTime()-time1;
    cout<<"TIME::"<<ThisTask<<" took "<<time1<<" to search "<<Nlocal<<" with "<<nthreads<<endl;
    nhalos=ngroup;
    //if caculating inclusive halo masses, then for simplicity, I assume halo id order NOT rearranged!
    //this is not necessarily true if baryons are searched for separately.
    if (libvelociraptorOpt.iInclusiveHalo) {
        pdatahalos=new PropData[nhalos+1];
        Int_t *numinhalos=BuildNumInGroup(Nlocal, nhalos, pfof);
        Int_t *sortvalhalos=new Int_t[Nlocal];
        Int_t *originalID=new Int_t[Nlocal];
        for (Int_t i=0;i<Nlocal;i++) {sortvalhalos[i]=pfof[i]*(pfof[i]>0)+Nlocal*(pfof[i]==0);originalID[i]=parts[i].GetID();parts[i].SetID(i);}
        Int_t *noffsethalos=BuildNoffset(Nlocal, parts, nhalos, numinhalos, sortvalhalos);
        GetInclusiveMasses(libvelociraptorOpt, Nlocal, parts, nhalos, pfof, numinhalos, pdatahalos, noffsethalos);
        qsort(parts,Nlocal,sizeof(Particle),IDCompare);
        delete[] numinhalos;
        delete[] sortvalhalos;
        delete[] noffsethalos;
        for (Int_t i=0;i<Nlocal;i++) parts[i].SetID(originalID[i]);
        delete[] originalID;
    }

    //
    // Substructure search.
    //
    if (libvelociraptorOpt.iSubSearch) {
        cout<<"Searching subset"<<endl;
        time1=MyGetTime();
        //if groups have been found (and localized to single MPI thread) then proceed to search for subsubstructures
        SearchSubSub(libvelociraptorOpt, Nlocal, parts, pfof,ngroup,nhalos,pdatahalos);
        time1=MyGetTime()-time1;
        cout<<"TIME::"<<ThisTask<<" took "<<time1<<" to search for substructures "<<Nlocal<<" with "<<nthreads<<endl;
    }
    pdata=new PropData[ngroup+1];
    //if inclusive halo mass required
    if (libvelociraptorOpt.iInclusiveHalo && ngroup>0) {
        CopyMasses(nhalos,pdatahalos,pdata);
        delete[] pdatahalos;
    }
    //need to add baryon interface

    //get mpi local hierarchy
    Int_t *nsub,*parentgid, *uparentgid,*stype;
    nsub=new Int_t[ngroup+1];
    parentgid=new Int_t[ngroup+1];
    uparentgid=new Int_t[ngroup+1];
    stype=new Int_t[ngroup+1];
    Int_t nhierarchy=GetHierarchy(libvelociraptorOpt,ngroup,nsub,parentgid,uparentgid,stype);
    CopyHierarchy(libvelociraptorOpt,pdata,ngroup,nsub,parentgid,uparentgid,stype);

    //calculate data and output
    numingroup=BuildNumInGroup(Nlocal, ngroup, pfof);
    pglist=SortAccordingtoBindingEnergy(libvelociraptorOpt,Nlocal,parts,ngroup,pfof,numingroup,pdata);//alters pglist so most bound particles first
    WriteProperties(libvelociraptorOpt,ngroup,pdata);
    WriteGroupCatalog(libvelociraptorOpt, ngroup, numingroup, pglist, parts);
    for (Int_t i=1;i<=ngroup;i++) delete[] pglist[i];
    delete[] pglist;
    delete[] parts;

    cout<<"VELOCIraptor returning."<< endl;
}
