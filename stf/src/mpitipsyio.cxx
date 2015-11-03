/*! \file mpitipsyio.cxx
 *  \brief this file contains routines used with MPI compilation and tipsy io and domain construction. 
 */


#ifdef USEMPI

//-- For MPI

#include "stf.h"

#include "tipsy_structs.h"
#include "endianutils.h"

/// \name Tispy Domain decomposition
//@{

/*! 
    Determine the domain decomposition.\n
    Here the domains are constructured in data units
    only ThisTask==0 should call this routine. It is tricky to get appropriate load balancing and correct number of particles per processor.\n
    
    I could use recursive binary splitting like kd-tree along most spread axis till have appropriate number of volumes corresponding 
    to number of processors.
    
    NOTE: assume that cannot store data so position information is read Nsplit times to determine boundaries of subvolumes
    could also randomly subsample system and produce tree from that 
    should store for each processor the node structure generated by the domain decomposition
    what I could do is read file twice, one to get extent and other to calculate entropy then decompose
    along some primary axis, then choose orthogonal axis, keep iterating till have appropriate number of subvolumes
    then store the boundaries of the subvolume. this means I don't store data but get at least reasonable domain decomposition
    
    NOTE: pkdgrav uses orthoganl recursive bisection along with kd-tree, gadget-2 uses peno-hilbert curve to map particles and oct-trees 
    the question with either method is guaranteeing load balancing. for ORB achieved by splitting (sub)volume along a dimension (say one with largest spread or max entropy)
    such that either side of the cut has approximately the same number of particles (ie: median splitting). But for both cases, load balancing requires particle information
    so I must load the system then move particles about to ensure load balancing.
    
    Main thing first is get the dimensional extent of the system.
    then I could get initial splitting just using mid point between boundaries along each dimension.
    once have that initial splitting just load data then start shifting data around.
*/
void MPIDomainExtentTipsy(Options &opt){
    struct dump tipsyheader;
    struct gas_particle gas;
    struct dark_particle dark;
    struct star_particle star;
    Int_t  count,ngas,nstar,ndark,Ntot;
    int temp;
    fstream Ftip;
    Double_t time,aadjust;
    Double_t posfirst[3];
    //if using MPI have task zero read file to determine extent of system
    if (ThisTask==0) {

    Ftip.open(opt.fname, ios::in | ios::binary);
    if (!Ftip){cerr<<"ERROR: Unable to open " <<opt.fname<<endl;exit(8);}
    else cout<<"Reading tipsy format from "<<opt.fname<<endl;

    //read tipsy header.
    Ftip.read((char*)&tipsyheader,sizeof(dump));
    Ftip.close();
    //offset stream by a double (time),  an integer (nbodies) ,integer (ndim), an integer (ngas)
    //read an integer (ndark), skip an integer (nstar), then data begins.
    time=tipsyheader.time;
    if ((opt.a-time)/opt.a>1e-2)cout<<"Note that atime provided != to time in tipsy file (a,t): "<<opt.a<<","<<time<<endl;
    if (opt.comove) aadjust=1.0;
    else aadjust=opt.a;
    Ntot=tipsyheader.nbodies;
    ngas=tipsyheader.nsph;
    nstar=tipsyheader.nstar;
    ndark=tipsyheader.ndark;
    opt.numpart[GASTYPE]=ngas;
    opt.numpart[DARKTYPE]=ndark;
    opt.numpart[STARTYPE]=nstar;

    cout<<"File contains "<<Ntot<<" particles at is at time "<<opt.a<<endl;
    cout<<"There "<<ngas<<" gas, "<<ndark<<" dark, "<<nstar<<" stars."<<endl;
    cout<<"System to be searched contains "<<Ntot<<" particles of type "<<opt.partsearchtype<<" at time "<<opt.a<<endl;
    cout<<"Starting domain decomposition for MPI by recursively splitting halo "<<log((float)NProcs)/log(2.0)<<" times into "<<NProcs<<" volumes"<<endl;

    count=0;
    Ftip.open(opt.fname, ios::in | ios::binary);
    Ftip.read((char*)&tipsyheader,sizeof(dump));
    for (Int_t i=0;i<ngas;i++)
    {
        Ftip.read((char*)&gas,sizeof(gas_particle));
        if ((opt.partsearchtype==PSTALL||opt.partsearchtype==PSTGAS)&&count==0) {
            posfirst[0]=gas.pos[0];posfirst[1]=gas.pos[1];posfirst[2]=gas.pos[2];
            count++;
            break;
        }
    }
    if (count==0) {
    for (Int_t i=0;i<ndark;i++)
    {
        Ftip.read((char*)&dark,sizeof(dark_particle));
        if ((opt.partsearchtype==PSTALL||opt.partsearchtype==PSTDARK)&&count==0) {
            posfirst[0]=dark.pos[0];posfirst[1]=dark.pos[1];posfirst[2]=dark.pos[2];
            count++;
            break;
        }
    }
    }
    if (count==0) {
    for (Int_t i=0;i<nstar;i++)
    {
        Ftip.read((char*)&star,sizeof(star_particle));
        if ((opt.partsearchtype==PSTALL||opt.partsearchtype==PSTSTAR)&&count==0) {
            posfirst[0]=star.pos[0];posfirst[1]=star.pos[1];posfirst[2]=star.pos[2];
            count++;
            break;
        }
    }
    }
    Ftip.close();
    mpi_xlim[0][0]=mpi_xlim[0][1]=posfirst[0];mpi_xlim[1][0]=mpi_xlim[1][1]=posfirst[1];mpi_xlim[2][0]=mpi_xlim[2][1]=posfirst[2];
    //determine the dimensional extend of the system
    Ftip.open(opt.fname, ios::in | ios::binary);
    Ftip.read((char*)&tipsyheader,sizeof(dump));
    for (Int_t i=0;i<ngas;i++)
    {
        Ftip.read((char*)&gas,sizeof(gas_particle));
        if (opt.partsearchtype==PSTALL||opt.partsearchtype==PSTGAS) {
        if (opt.p>0.0)
        {
            for (int j=0;j<3;j++) {
                if (gas.pos[j]-posfirst[j]>opt.p/2.0) gas.pos[j]-=opt.p;
                else if (gas.pos[j]-posfirst[j]<-opt.p/2.0) gas.pos[j]+=opt.p;
            }
        }
        for (int j=0;j<3;j++) {if (gas.pos[j]<mpi_xlim[j][0]) mpi_xlim[j][0]=gas.pos[j];if (gas.pos[j]>mpi_xlim[j][1]) mpi_xlim[j][1]=gas.pos[j];}
        }
    }
    for (Int_t i=0;i<ndark;i++)
    {
        Ftip.read((char*)&dark,sizeof(dark_particle));
        if (opt.partsearchtype==PSTALL||opt.partsearchtype==PSTDARK) {
        if (opt.p>0.0)
        {
            for (int j=0;j<3;j++) {
                if (dark.pos[j]-posfirst[j]>opt.p/2.0) dark.pos[j]-=opt.p;
                else if (dark.pos[j]-posfirst[j]<-opt.p/2.0) dark.pos[j]+=opt.p;
            }
        }
        for (int j=0;j<3;j++) {if (dark.pos[j]<mpi_xlim[j][0]) mpi_xlim[j][0]=dark.pos[j];if (dark.pos[j]>mpi_xlim[j][1]) mpi_xlim[j][1]=dark.pos[j];}
        }
    }
    for (Int_t i=0;i<nstar;i++)
    {
        Ftip.read((char*)&star,sizeof(star_particle));
        if (opt.partsearchtype==PSTALL||opt.partsearchtype==PSTSTAR) {
        if (opt.p>0.0)
        {
            for (int j=0;j<3;j++) {
                if (star.pos[j]-posfirst[j]>opt.p/2.0) star.pos[j]-=opt.p;
                else if (star.pos[j]-posfirst[j]<-opt.p/2.0) star.pos[j]+=opt.p;
            }
        }
        for (int j=0;j<3;j++) {if (star.pos[j]<mpi_xlim[j][0]) mpi_xlim[j][0]=star.pos[j];if (star.pos[j]>mpi_xlim[j][1]) mpi_xlim[j][1]=star.pos[j];}
        }
    }

cout<<"MPI Domain Extent is :"<<endl;
for (int k=0;k<3;k++) cout<<k<<" "<<mpi_xlim[k][0]<<" "<<mpi_xlim[k][1]<<endl;
    }
    //make sure limits have been found
    MPI_Barrier(MPI_COMM_WORLD);
}


void MPINumInDomainTipsy(Options &opt)
{
}

//@}

#endif