/*! \file substructureproperties.cxx
 *  \brief this file contains routines to characterize the bulk properties of the (sub)structures found.
 */

#include "stf.h"


///\name Routines calculating numerous properties of groups
//@{
/*!
    Get properties of the substructures, specifically
    \f$ m,\ (x,y,z)_{\rm cm},\ (vx,vy,vz)_{\rm cm},\ V_{\rm max},\ R_{\rm max}, \f$
    and possibly other quantities. The centre of mass quantities are interated to the innermost opt.pinfo.cmfrac percent
    The code assumes that the structures are local to the MPI domain

    Note that this routine will be deprecated but has been left as an example of what properties are typically of interest and
    how these quantities are calculated.

*/
void GetProperties(Options &opt, const Int_t nbodies, Particle *Part, Int_t ngroup, Int_t *&pfof, Int_t *numingroup, Int_t **pglist)
{
    PropData *pdata=new PropData[ngroup+1];
    Particle *Pval, *gPart;
    Int_t i,j,k,ii;
    int inflag=0, ipflag=0;
    Int_t *noffset=new Int_t[ngroup+1];
    Double_t eps2=opt.uinfo.eps*opt.uinfo.eps;
    Double_t ri,rcmv,r2,cmx,cmy,cmz,EncMass,Ninside;
    Double_t vc,rc,x,y,z;
    Coordinate cmold(0.),cmref;
    Double_t change=MAXVALUE,tol=1e-2;

    if (numingroup==NULL) {numingroup=BuildNumInGroup(nbodies, ngroup, pfof);inflag=1;}
    //sort the particle data according to their group id so that one can then sort particle data
    //of a group however one sees fit.
    ///\todo adjust type sort so that keep type information, just need array to store type, then sort and set type.
    for (i=0;i<nbodies;i++) {
        if (pfof[i]>0) Part[i].SetType(pfof[i]);
        else Part[i].SetType(ngroup+1);//here move all particles not in groups to the back of the particle array
    }
    qsort(Part, nbodies, sizeof(Particle), TypeCompare);
    noffset[0]=noffset[1]=0;
    for (i=2;i<=ngroup;i++) noffset[i]=noffset[i-1]+numingroup[i-1];
    if (pglist==NULL) {pglist=BuildPGList(nbodies, ngroup, numingroup, pfof);ipflag=1;}

    //calculation is split between small and large groups for omp purposes

#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
    for (i=1;i<=ngroup;i++) pdata[i].num=numingroup[i];
#ifdef USEOPENMP
}
#endif
    //for small groups loop over groups
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,Pval,ri,rcmv,r2,cmx,cmy,cmz,EncMass,Ninside,cmold,change,tol,x,y,z,vc,rc)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
    for (i=1;i<=ngroup;i++) if (numingroup[i]<ompunbindnum)
    {
        //first we calculate centre-of-mass
        for (k=0;k<3;k++) pdata[i].gcm[k]=pdata[i].gcmvel[k]=0;
        pdata[i].gmass=pdata[i].gmaxvel=0.0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            pdata[i].gmass+=(*Pval).GetMass();
            for (k=0;k<3;k++) {
                pdata[i].gcm[k]+=(*Pval).GetPosition(k)*(*Pval).GetMass();
                pdata[i].gcmvel[k]+=(*Pval).GetVelocity(k)*(*Pval).GetMass();
            }
        }
        for (k=0;k<3;k++){pdata[i].gcm[k]*=(1.0/pdata[i].gmass);pdata[i].gcmvel[k]*=(1.0/pdata[i].gmass);}
        pdata[i].gsize=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            r2=0.0;
            for (k=0;k<3;k++) r2+=(pdata[i].gcm[k]-(*Pval).GetPosition(k))*(pdata[i].gcm[k]-(*Pval).GetPosition(k));
            if (sqrt(r2)>pdata[i].gsize)pdata[i].gsize=sqrt(r2);
        }
        //iterate for better cm if group large enough
        cmold[0]=cmold[1]=cmold[2]=0.;
        change=MAXVALUE;tol=1e-2;
        if (numingroup[i]*opt.pinfo.cmfrac>50) {
            ri=pdata[i].gsize;
            cmold=pdata[i].gcm;
            rcmv=ri;
            while (true)
            {
                ri*=opt.pinfo.cmadjustfac;;
                // find c/m of all particles within ri
                cmx=cmy=cmz=0.;
                EncMass=0.;
                Ninside=0;
                for (j=0;j<numingroup[i];j++)
                {
                    Pval=&Part[j+noffset[i]];
                    //Pval=&Part[pglist[i][j]];
                    //Pval=&gPart[i][j];
                    x = (*Pval).X() - cmold[0];
                    y = (*Pval).Y() - cmold[1];
                    z = (*Pval).Z() - cmold[2];
                    if (sqrt(x*x + y*y + z*z) <= ri)
                    {
                        cmx += (*Pval).GetMass()*(*Pval).X();
                        cmy += (*Pval).GetMass()*(*Pval).Y();
                        cmz += (*Pval).GetMass()*(*Pval).Z();
                        EncMass += (*Pval).GetMass();
                        Ninside++;
                    }
                }
                if (EncMass>0) {
                    pdata[i].gcm[0]=cmx;pdata[i].gcm[1]=cmy;pdata[i].gcm[2]=cmz;
                    for (k=0;k<3;k++) pdata[i].gcm[k] /= EncMass;
                    cmold=pdata[i].gcm;
                    rcmv=ri;
                }
                // keep making radius smaller until there's
                // less than 10% of the particles inside
                if (Ninside < opt.pinfo.cmfrac * numingroup[i])  break;
            }
            cmx=cmy=cmz=EncMass=0.;
            for (j=0;j<numingroup[i];j++)
            {
                Pval=&Part[j+noffset[i]];
                //Pval=&Part[pglist[i][j]];
                //Pval=&gPart[i][j];
                x = (*Pval).X() - pdata[i].gcm[0];
                y = (*Pval).Y() - pdata[i].gcm[1];
                z = (*Pval).Z() - pdata[i].gcm[2];
                Pval->SetPosition(x,y,z);
                if (sqrt(x*x + y*y + z*z) <= rcmv)
                {
                    cmx += (*Pval).GetMass()*(*Pval).Vx();
                    cmy += (*Pval).GetMass()*(*Pval).Vy();
                    cmz += (*Pval).GetMass()*(*Pval).Vz();
                    EncMass += (*Pval).GetMass();
                }
            }
            pdata[i].gcmvel[0]=cmx;pdata[i].gcmvel[1]=cmy;pdata[i].gcmvel[2]=cmz;
            for (k=0;k<3;k++) pdata[i].gcmvel[k] /= EncMass;
        }
        //then we sort by radius. Note that though qsort is more efficient, it does consume memory
        //whereas gsl_heapsort does not. Therefore despite the fact that heapsorts are not stable we use it here to
        //mimize memory allocation within an omp loop
#ifdef USEOPENMP
        gsl_heapsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), RadCompare);
#else
        qsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), RadCompare);
#endif
        //then determine enclose mass based properties like vmax.
        pdata[i].gmaxvel=0.;
        EncMass=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            EncMass+=Pval->GetMass();
            rc=Pval->Radius();
            if (EncMass>0) vc=sqrt(opt.G*EncMass/rc);
            if (vc>pdata[i].gmaxvel) {pdata[i].gmaxvel=vc;pdata[i].gRmaxvel=rc;pdata[i].gMmaxvel=EncMass;}
            x = (*Pval).X()+pdata[i].gcm[0];
            y = (*Pval).Y()+pdata[i].gcm[1];
            z = (*Pval).Z()+pdata[i].gcm[2];
            Pval->SetPosition(x,y,z);
        }
    }
#ifdef USEOPENMP
}
#endif

    //for large groups loop over particles themselves
    for (i=1;i<=ngroup;i++) if (numingroup[i]>=ompunbindnum)
    {
        //calculate cm
        for (k=0;k<3;k++) pdata[i].gcm[k]=pdata[i].gcmvel[k]=0;
        pdata[i].gmass=pdata[i].gmaxvel=0.0;
        EncMass=cmx=cmy=cmz=0.;
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(j,Pval)
{
    #pragma omp for reduction(+:EncMass,cmx,cmy,cmz)
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            EncMass+=(*Pval).GetMass();
            cmx+=(*Pval).X()*(*Pval).GetMass();
            cmy+=(*Pval).Y()*(*Pval).GetMass();
            cmz+=(*Pval).Z()*(*Pval).GetMass();
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].gcm[0]=cmx;pdata[i].gcm[1]=cmy;pdata[i].gcm[2]=cmy;
        pdata[i].gmass=EncMass;
        for (k=0;k<3;k++){pdata[i].gcm[k]*=(1.0/pdata[i].gmass);pdata[i].gcmvel[k]*=(1.0/pdata[i].gmass);}
        pdata[i].gsize=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            for (k=0;k<3;k++) {
                Pval->SetPosition(k,(*Pval).GetPosition(k)-pdata[i].gcm[k]);
            }
        }
        qsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), RadCompare);
        ri=Part[noffset[i]+numingroup[i]-1].Radius();
        ri=ri*ri;
        //iterate for better cm if group large enough
        cmold[0]=cmold[1]=cmold[2]=0.;
        change=MAXVALUE;tol=1e-2;
        cmref=pdata[i].gcm;//cmold=pdata[i].gcm;
        rcmv=ri;
        ii=numingroup[i];
        while (true)
        {
            //ri*=opt.pinfo.cmadjustfac;
            ii*=opt.pinfo.cmadjustfac;
            // find c/m of all particles within ri
            cmx=cmy=cmz=0.;
            EncMass=0.;
            Ninside=0;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z)
{
#pragma omp for reduction(+:EncMass,Ninside,cmx,cmy,cmz)
#endif
            for (j=0;j<ii;j++)
            {
                Pval=&Part[j+noffset[i]];
                x = (*Pval).X() - cmold[0];
                y = (*Pval).Y() - cmold[1];
                z = (*Pval).Z() - cmold[2];
                if ((x*x + y*y + z*z) <= ri)
                {
                    cmx += (*Pval).GetMass()*(*Pval).X();
                    cmy += (*Pval).GetMass()*(*Pval).Y();
                    cmz += (*Pval).GetMass()*(*Pval).Z();
                    EncMass += (*Pval).GetMass();
                    Ninside++;
                }
            }
#ifdef USEOPENMP
}
#endif
            x = Part[noffset[i]+numingroup[i]-1].X() - cmold[0];
            y = Part[noffset[i]+numingroup[i]-1].Y() - cmold[1];
            z = Part[noffset[i]+numingroup[i]-1].Z() - cmold[2];
            ri=x*x+y*y+z*z;
            if (EncMass>0) {
                pdata[i].gcm[0]=cmx;pdata[i].gcm[1]=cmy;pdata[i].gcm[2]=cmz;
                for (k=0;k<3;k++) pdata[i].gcm[k] /= EncMass;
                cmold=pdata[i].gcm;
                rcmv=ri;
            }
            if (Ninside < opt.pinfo.cmfrac * numingroup[i]) break;
        }
        cmx=cmy=cmz=EncMass=0.;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z)
{
    #pragma omp for reduction(+:EncMass,cmx,cmy,cmz)
#endif
        for (j=0;j<numingroup[i];j++)
        {
            Pval=&Part[j+noffset[i]];
            x = (*Pval).X() - pdata[i].gcm[0];
            y = (*Pval).Y() - pdata[i].gcm[1];
            z = (*Pval).Z() - pdata[i].gcm[2];
            Pval->SetPosition(x,y,z);
            if ((x*x + y*y + z*z) <= rcmv)
            {
                cmx += (*Pval).GetMass()*(*Pval).Vx();
                cmy += (*Pval).GetMass()*(*Pval).Vy();
                cmz += (*Pval).GetMass()*(*Pval).Vz();
                EncMass += (*Pval).GetMass();
            }
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].gcmvel[0]=cmx;pdata[i].gcmvel[1]=cmy;pdata[i].gcmvel[2]=cmz;
        for (k=0;k<3;k++) pdata[i].gcmvel[k] /= EncMass;
        for (k=0;k<3;k++) pdata[i].gcm[k] += cmref[k];
        pdata[i].gmaxvel=0.;
        //now sort by radius and determine enclosed mass like properties
        qsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), RadCompare);
        pdata[i].gsize=Part[noffset[i]+numingroup[i]-1].Radius();
        EncMass=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            EncMass+=Pval->GetMass();
            rc=Pval->Radius();
            if (EncMass>0) vc=sqrt(opt.G*EncMass/rc);
            if (vc>pdata[i].gmaxvel) {pdata[i].gmaxvel=vc;pdata[i].gRmaxvel=rc;pdata[i].gMmaxvel=EncMass;}
            x = (*Pval).X()+pdata[i].gcm[0];
            y = (*Pval).Y()+pdata[i].gcm[1];
            z = (*Pval).Z()+pdata[i].gcm[2];
            Pval->SetPosition(x,y,z);
        }
    }


    //one cm info properties has been calculate, determine the bound mass.
    //again loop over groups but calculation is split between large and small groups.
    //the reason is that a simple PP calculation is more efficient than a tree calculation simply due to the overhead
    //of producing a tree
#ifdef NOMASS
    Double_t mw2=opt.MassValue*opt.MassValue;
#endif
        //calculate the potential energy
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
    for (i=1;i<=ngroup;i++) if (numingroup[i]<ompunbindnum) {
        Double_t r2=0.0,v2,poti,Ti,pot;
        for (j=0;j<numingroup[i];j++) {
            for (k=j+1;k<numingroup[i];k++) {
                r2=0.;for (int n=0;n<3;n++) r2+=pow(Part[j+noffset[i]].GetPosition(n)-Part[k+noffset[i]].GetPosition(n),2.0);
                r2+=eps2;
                r2=1.0/sqrt(r2);
                pdata[i].Pot+=pot=-opt.G*(Part[j+noffset[i]].GetMass()*Part[k+noffset[i]].GetMass())*r2;
                poti=Part[j+noffset[i]].GetPotential()+pot;Part[j+noffset[i]].SetPotential(poti);
                poti=Part[k+noffset[i]].GetPotential()+pot;Part[k+noffset[i]].SetPotential(poti);
            }
            v2=0.;for (int n=0;n<3;n++) v2+=pow(Part[j+noffset[i]].GetVelocity(n)-pdata[i].gcmvel[n],2.0);
            Ti=0.5*Part[j+noffset[i]].GetMass()*v2;
#ifdef NOMASS
            Ti*=opt.MassValue;
            Part[k+noffset[i]].SetPotential(Part[k+noffset[i]].GetPotential()*mw2);
#endif
            pdata[i].T+=Ti;
            if(Ti+Part[j+noffset[i]].GetPotential()<0) pdata[i].Efrac+=1.0;
        }
        pdata[i].Efrac/=(Double_t)numingroup[i];
    }
#ifdef USEOPENMP
}
#endif
    for (i=1;i<=ngroup;i++) if (numingroup[i]>=ompunbindnum) {
        //here a monopole kd tree calculation of potential
        Potential(opt,numingroup[i],&Part[noffset[i]]);
        Double_t v2,Ti;
        Double_t Tval,Potval,Efracval;
        Tval=0;Potval=0;Efracval=0;
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(j,v2,Ti)
{
    #pragma omp for reduction(+:Tval,Efracval,Potval)
#endif
        for (j=0;j<numingroup[i];j++) {
            v2=0.;for (int n=0;n<3;n++) v2+=pow(Part[j+noffset[i]].GetVelocity(n)-pdata[i].gcmvel[n],2.0);
            Tval+=Ti=0.5*Part[j+noffset[i]].GetMass()*v2;
            if(Ti<Part[j+noffset[i]].GetPotential()) Efracval+=1.0;
            Potval+=Part[j+noffset[i]].GetPotential();
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].T=Tval;pdata[i].Efrac=Efracval;pdata[i].Pot=Potval;
        pdata[i].Efrac/=(Double_t)numingroup[i];
    }
    //we then write the properties
    WriteProperties(opt,ngroup,pdata);
    delete[] pdata;
    delete[] noffset;
    if (ipflag) {
        for (i=1;i<=ngroup;i++) delete[] pglist[i];
        delete[] pglist;
    }
    if (inflag) delete[] numingroup;
    //reset particles back to id order
    qsort(Part, nbodies, sizeof(Particle), IDCompare);
    cout<<"Done"<<endl;
}

/*!
    The routine is used to calculate CM and related morphologial properties of groups. It assumes that particles have been
    arranged in group order and the indexing offsets between groups is given by noffset

    The overall structure of the code is a bit lengthy simply to break up calculations appropriately for OMP style parallization.
    For small groups it is more efficient to parallize across groups, whereas for large groups containing many particles, we loop over the particles
    to sum quantities.

 */
void GetCMProp(Options &opt, const Int_t nbodies, Particle *Part, Int_t ngroup, Int_t *&pfof, Int_t *&numingroup, PropData *&pdata, Int_t *&noffset)
{
    Particle *Pval;
    Int_t i,j,k;
    if (opt.iverbose) cout<<"Get CM"<<endl;
    Coordinate cmold(0.),cmref;
    Double_t ri,rcmv,r2,cmx,cmy,cmz,EncMass,Ninside;
    Double_t cmvx,cmvy,cmvz;
    Double_t vc,rc,x,y,z,vx,vy,vz,jzval,Rdist,zdist,Ekin,Krot,mval;
    Double_t RV_Ekin,RV_Krot;
    Double_t Tsum,tsum,Zsum,sfrsum;
    Coordinate jval;
    Double_t change=MAXVALUE,tol=1e-2;
    Int_t ii,icmv;
    Int_t RV_num;
    Double_t virval=log(opt.virlevel*opt.rhobg);
    Double_t m200val=log(opt.rhobg/opt.Omega_m*200.0);
    Double_t m200mval=log(opt.rhobg*200.0);
    Double_t mBN98val=log(opt.virBN98*opt.rhobg);
    //also calculate 500 overdensity and useful for gas/star content
    Double_t m500val=log(opt.rhobg/opt.Omega_m*500.0);
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
    for (i=1;i<=ngroup;i++) pdata[i].num=numingroup[i];
#ifdef USEOPENMP
}
#endif
    //for small groups loop over groups
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,Pval,ri,rcmv,r2,cmx,cmy,cmz,EncMass,Ninside,cmold,change,tol,x,y,z,vx,vy,vz,vc,rc,jval,jzval,Rdist,zdist,Ekin,Krot,mval,RV_Ekin,RV_Krot,RV_num)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
    for (i=1;i<=ngroup;i++) if (numingroup[i]<omppropnum)
    {
        for (k=0;k<3;k++) pdata[i].gcm[k]=pdata[i].gcmvel[k]=0;
        pdata[i].gmass=pdata[i].gmaxvel=0.0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            pdata[i].gmass+=(*Pval).GetMass();
            for (k=0;k<3;k++) {
                pdata[i].gcm[k]+=(*Pval).GetPosition(k)*(*Pval).GetMass();
                pdata[i].gcmvel[k]+=(*Pval).GetVelocity(k)*(*Pval).GetMass();
            }
        }
        for (k=0;k<3;k++){pdata[i].gcm[k]*=(1.0/pdata[i].gmass);pdata[i].gcmvel[k]*=(1.0/pdata[i].gmass);}
        pdata[i].gsize=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            r2=0.0;
            for (k=0;k<3;k++) r2+=(pdata[i].gcm[k]-(*Pval).GetPosition(k))*(pdata[i].gcm[k]-(*Pval).GetPosition(k));
            if (sqrt(r2)>pdata[i].gsize)pdata[i].gsize=sqrt(r2);
        }
        //iterate for better cm if group large enough
        cmold=pdata[i].gcm;
        change=MAXVALUE;tol=1e-2;
        if (numingroup[i]*opt.pinfo.cmfrac>=50) {
            ri=pdata[i].gsize;
            ri=ri*ri;
            cmold=pdata[i].gcm;
            rcmv=ri;
            while (true)
            {
                ri*=opt.pinfo.cmadjustfac;
                // find c/m of all particles within ri
                cmx=cmy=cmz=0.;
                EncMass=0.;
                Ninside=0;
                for (j=0;j<numingroup[i];j++)
                {
                    Pval=&Part[j+noffset[i]];
                    x = (*Pval).X() - cmold[0];
                    y = (*Pval).Y() - cmold[1];
                    z = (*Pval).Z() - cmold[2];
                    if ((x*x + y*y + z*z) <= ri)
                    {
                        cmx += (*Pval).GetMass()*(*Pval).X();
                        cmy += (*Pval).GetMass()*(*Pval).Y();
                        cmz += (*Pval).GetMass()*(*Pval).Z();
                        EncMass += (*Pval).GetMass();
                        Ninside++;
                    }
                }
                if (Ninside > opt.pinfo.cmfrac * numingroup[i]) {
                    pdata[i].gcm[0]=cmx;pdata[i].gcm[1]=cmy;pdata[i].gcm[2]=cmz;
                    for (k=0;k<3;k++) pdata[i].gcm[k] /= EncMass;
                    cmold=pdata[i].gcm;
                    rcmv=ri;
                }
                else break;
            }
            cmx=cmy=cmz=EncMass=0.;
            for (j=0;j<numingroup[i];j++)
            {
                Pval=&Part[j+noffset[i]];
                x = (*Pval).X() - pdata[i].gcm[0];
                y = (*Pval).Y() - pdata[i].gcm[1];
                z = (*Pval).Z() - pdata[i].gcm[2];
                Pval->SetPosition(x,y,z);
                if ((x*x + y*y + z*z) <= rcmv)
                {
                    cmx += (*Pval).GetMass()*(*Pval).Vx();
                    cmy += (*Pval).GetMass()*(*Pval).Vy();
                    cmz += (*Pval).GetMass()*(*Pval).Vz();
                    EncMass += (*Pval).GetMass();
                }
            }
            pdata[i].gcmvel[0]=cmx;pdata[i].gcmvel[1]=cmy;pdata[i].gcmvel[2]=cmz;
            for (k=0;k<3;k++) pdata[i].gcmvel[k] /= EncMass;
        }
        else {
            for (j=0;j<numingroup[i];j++)
            {
                Pval=&Part[j+noffset[i]];
                x = (*Pval).X() - pdata[i].gcm[0];
                y = (*Pval).Y() - pdata[i].gcm[1];
                z = (*Pval).Z() - pdata[i].gcm[2];
                Pval->SetPosition(x,y,z);
            }
        }
#ifdef NOMASS
        pdata[i].gmass*=opt.MassValue;
#endif
        if (pdata[i].gMFOF==0 && pdata[i].hostid==-1) pdata[i].gMFOF=pdata[i].gmass;
        //sort by radius (here use gsl_heapsort as no need to allocate more memory
        gsl_heapsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), RadCompare);

        //determine overdensity mass and radii. AGAIN REMEMBER THAT THESE ARE NOT MEANINGFUL FOR TIDAL DEBRIS
        //HERE MASSES ARE EXCLUSIVE!
        EncMass=pdata[i].gmass;
        if (opt.iInclusiveHalo==0 || opt.iInclusiveHalo!=0 && pdata[i].hostid!=-1) {
            for (j=numingroup[i]-1;j>=0;j--) {
                Pval=&Part[j+noffset[i]];
                rc=Pval->Radius();
                if (pdata[i].gRvir==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>virval)
                {pdata[i].gMvir=EncMass;pdata[i].gRvir=rc;}
                if (pdata[i].gR200c==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>m200val)
                {pdata[i].gM200c=EncMass;pdata[i].gR200c=rc;}
                if (pdata[i].gR200m==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>m200mval)
                {pdata[i].gM200m=EncMass;pdata[i].gR200m=rc;}
                if (pdata[i].gR500c==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>m500val)
                {pdata[i].gM500c=EncMass;pdata[i].gR500c=rc;}
                if (pdata[i].gRBN98==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>mBN98val)
                {pdata[i].gMBN98=EncMass;pdata[i].gRBN98=rc;}
                if (pdata[i].gR200m!=0&&pdata[i].gR200c!=0&&pdata[i].gRvir!=0&&pdata[i].gR500c!=0&&pdata[i].gRBN98!=0) break;
#ifdef NOMASS
                EncMass-=opt.MassValue;
#else
                EncMass-=Pval->GetMass();
#endif
            }
            if (pdata[i].gRvir==0) {pdata[i].gMvir=pdata[i].gmass;pdata[i].gRvir=pdata[i].gsize;}
            if (pdata[i].gR200c==0) {pdata[i].gM200c=pdata[i].gmass;pdata[i].gR200c=pdata[i].gsize;}
            if (pdata[i].gR200m==0) {pdata[i].gM200m=pdata[i].gmass;pdata[i].gR200m=pdata[i].gsize;}
            if (pdata[i].gR500c==0) {pdata[i].gM500c=pdata[i].gmass;pdata[i].gR500c=pdata[i].gsize;}
            if (pdata[i].gRBN98==0) {pdata[i].gMBN98=pdata[i].gmass;pdata[i].gRBN98=pdata[i].gsize;}
        }

        //determine properties like maximum circular velocity, velocity dispersion, angular momentum, etc
        pdata[i].gmaxvel=0.;
        EncMass=0;
        Ekin=0.;
        pdata[i].gJ[0]=pdata[i].gJ[1]=pdata[i].gJ[2]=0.;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
#ifdef NOMASS
            EncMass+=opt.MassValue;
#else
            EncMass+=Pval->GetMass();
#endif
            rc=Pval->Radius();
            vx = (*Pval).Vx()-pdata[i].gcmvel[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2];
            pdata[i].gJ=pdata[i].gJ+Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*Pval->GetMass();
            if (rc<pdata[i].gR200m) pdata[i].gJ200m=pdata[i].gJ200m+Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*Pval->GetMass();
            if (rc<pdata[i].gR200c) pdata[i].gJ200c=pdata[i].gJ200c+Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*Pval->GetMass();
            Ekin+=Pval->GetMass()*(vx*vx+vy*vy+vz*vz);
            pdata[i].gveldisp(0,0)+=vx*vx*Pval->GetMass();
            pdata[i].gveldisp(1,1)+=vy*vy*Pval->GetMass();
            pdata[i].gveldisp(2,2)+=vz*vz*Pval->GetMass();
            pdata[i].gveldisp(0,1)+=vx*vy*Pval->GetMass();
            pdata[i].gveldisp(0,2)+=vx*vz*Pval->GetMass();
            pdata[i].gveldisp(1,2)+=vy*vz*Pval->GetMass();
            //calculate vc
            if (rc>0) if (EncMass>0) vc=sqrt(opt.G*EncMass*opt.MassValue/rc);
            //max circ and then vir data
            if (vc>pdata[i].gmaxvel && EncMass>=1.0/sqrt(numingroup[i])*pdata[i].gmass) {pdata[i].gmaxvel=vc;pdata[i].gRmaxvel=rc;pdata[i].gMmaxvel=EncMass;RV_num=j+1;}
            if (EncMass>0.5*pdata[i].gmass && pdata[i].gRhalfmass==0) pdata[i].gRhalfmass=rc;
        }
        pdata[i].gveldisp(1,0)=pdata[i].gveldisp(0,1);
        pdata[i].gveldisp(2,0)=pdata[i].gveldisp(0,2);
        pdata[i].gveldisp(2,1)=pdata[i].gveldisp(1,2);
        if (pdata[i].gRvir==0) {pdata[i].gMvir=pdata[i].gmass;pdata[i].gRvir=pdata[i].gsize;}
        pdata[i].gveldisp=pdata[i].gveldisp*(1.0/pdata[i].gmass);
        pdata[i].gsigma_v=pow(pdata[i].gveldisp.Det(),1.0/6.0);
        Ekin*=0.5;
#ifdef NOMASS
        pdata[i].gJ=pdata[i].gJ*opt.MassValue;
        pdata[i].gMmaxvel*=opt.MassValue;
        Ekin*=opt.MassValue;
#endif
        pdata[i].glambda_B=pdata[i].gJ.Length()/(pdata[i].gM200c*sqrt(2.0*opt.G*pdata[i].gM200c*pdata[i].gR200c));

        //calculate the rotational energy about the angular momentum axis
        //this is defined as the specific angular momentum about the angular momentum
        //axis (see sales et al 2010)
        RV_Ekin=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            vx = (*Pval).Vx()-pdata[i].gcmvel[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2];
            jval=Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz));
            jzval=(jval*pdata[i].gJ)/pdata[i].gJ.Length();
            zdist=(Coordinate(Pval->GetPosition())*pdata[i].gJ)/pdata[i].gJ.Length();
            Rdist=sqrt(Pval->Radius2()-zdist*zdist);
            pdata[i].Krot+=Pval->GetMass()*(jzval*jzval/(Rdist*Rdist));
        }
        pdata[i].Krot*=0.5/Ekin;
#ifdef NOMASS
        pdata[i].Krot*=opt.MassValue;
#endif

        //now calculate stuff within RV knowing particle array sorted according to radius
        for (j=0;j<RV_num;j++) {
            Pval=&Part[j+noffset[i]];
            rc=Pval->Radius();
            vx = (*Pval).Vx()-pdata[i].gcmvel[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2];
            RV_Ekin+=Pval->GetMass()*(vx*vx+vy*vy+vz*vz);
            pdata[i].RV_J=pdata[i].RV_J+Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*Pval->GetMass();
            pdata[i].RV_veldisp(0,0)+=vx*vx*Pval->GetMass();
            pdata[i].RV_veldisp(1,1)+=vy*vy*Pval->GetMass();
            pdata[i].RV_veldisp(2,2)+=vz*vz*Pval->GetMass();
            pdata[i].RV_veldisp(0,1)+=vx*vy*Pval->GetMass();
            pdata[i].RV_veldisp(0,2)+=vx*vz*Pval->GetMass();
            pdata[i].RV_veldisp(1,2)+=vy*vz*Pval->GetMass();

        }
        //adjust RVmax values
        pdata[i].RV_veldisp(1,0)=pdata[i].RV_veldisp(0,1);
        pdata[i].RV_veldisp(2,0)=pdata[i].RV_veldisp(0,2);
        pdata[i].RV_veldisp(2,1)=pdata[i].RV_veldisp(1,2);
        pdata[i].RV_veldisp=pdata[i].RV_veldisp*(1.0/pdata[i].gMmaxvel);
        pdata[i].RV_sigma_v=pow(pdata[i].RV_veldisp.Det(),1.0/6.0);
        RV_Ekin*=0.5;
#ifdef NOMASS
        pdata[i].RV_J=pdata[i].RV_J*opt.MassValue;
        RV_Ekin*=opt.MassValue;
#endif
        pdata[i].RV_lambda_B=pdata[i].RV_J.Length()/(pdata[i].gMmaxvel*sqrt(2.0*opt.G*pdata[i].gMmaxvel*pdata[i].gRmaxvel));
        for (j=0;j<RV_num;j++) {
            Pval=&Part[j+noffset[i]];
            vx = (*Pval).Vx()-pdata[i].gcmvel[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2];
            jval=Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz));
            jzval=(jval*pdata[i].RV_J)/pdata[i].RV_J.Length();
            zdist=(Coordinate(Pval->GetPosition())*pdata[i].RV_J)/pdata[i].RV_J.Length();
            Rdist=sqrt(Pval->Radius2()-zdist*zdist);
            pdata[i].RV_Krot+=Pval->GetMass()*(jzval*jzval/(Rdist*Rdist));
        }
        pdata[i].RV_Krot*=0.5/RV_Ekin;
#ifdef NOMASS
        pdata[i].RV_Krot*=opt.MassValue;
#endif

        //calculate the concentration based on prada 2012 where [(Vmax)/(GM/R)]^2-(0.216*c)/f(c)=0,
        //where f(c)=ln(1+c)-c/(1+c) and M is some "virial" mass and associated radius
        if (pdata[i].gR200c==0) pdata[i].VmaxVvir2=(pdata[i].gmaxvel*pdata[i].gmaxvel)/(opt.G*pdata[i].gmass/pdata[i].gsize);
        else pdata[i].VmaxVvir2=(pdata[i].gmaxvel*pdata[i].gmaxvel)/(opt.G*pdata[i].gM200c/pdata[i].gR200c);
        //always possible halo severly truncated before so correct if necessary and also for tidal debris, both vmax concentration pretty meaningless
        if (pdata[i].VmaxVvir2<=1.05) {
            if (pdata[i].gM200c==0) pdata[i].cNFW=pdata[i].gsize/pdata[i].gRmaxvel;
            else pdata[i].cNFW=pdata[i].gR200c/pdata[i].gRmaxvel;
        }
        else {
            if (numingroup[i]>=100) GetConcentration(pdata[i]);
            else {
                if (pdata[i].gM200c==0) pdata[i].cNFW=pdata[i].gsize/pdata[i].gRmaxvel;
                else pdata[i].cNFW=pdata[i].gR200c/pdata[i].gRmaxvel;
            }
        }

        //baryons
#if defined(GASON)
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE) {
                pdata[i].n_gas++;
                pdata[i].M_gas+=Pval->GetMass();
            }
        }
        Ekin=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE) {
                mval=Pval->GetMass();
                //store temperature in units of internal energy
                pdata[i].Temp_gas+=mval*Pval->GetU();
                //pdata[i].sphden_gas+=Pval->GetMass()*Pval->GetSPHDen();
#ifdef STARON
                pdata[i].Z_gas+=mval*Pval->GetZmet();
                pdata[i].SFR_gas+=mval*Pval->GetSFR();
#endif
                x = (*Pval).X();
                y = (*Pval).Y();
                z = (*Pval).Z();
                pdata[i].cm_gas[0]+=x*mval;
                pdata[i].cm_gas[1]+=y*mval;
                pdata[i].cm_gas[2]+=z*mval;

                vx = (*Pval).Vx()-pdata[i].gcmvel[0];
                vy = (*Pval).Vy()-pdata[i].gcmvel[1];
                vz = (*Pval).Vz()-pdata[i].gcmvel[2];
                pdata[i].cmvel_gas[0]+=vx*mval;
                pdata[i].cmvel_gas[1]+=vy*mval;
                pdata[i].cmvel_gas[2]+=vz*mval;

                pdata[i].L_gas=pdata[i].L_gas+Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*mval;
                if (pdata[i].n_gas>=10) {
                    pdata[i].veldisp_gas(0,0)+=vx*vx*mval;
                    pdata[i].veldisp_gas(1,1)+=vy*vy*mval;
                    pdata[i].veldisp_gas(2,2)+=vz*vz*mval;
                    pdata[i].veldisp_gas(0,1)+=vx*vy*mval;
                    pdata[i].veldisp_gas(0,2)+=vx*vz*mval;
                    pdata[i].veldisp_gas(1,2)+=vy*vz*mval;
                    pdata[i].veldisp_gas(1,0)+=vx*vy*mval;
                    pdata[i].veldisp_gas(2,0)+=vx*vz*mval;
                    pdata[i].veldisp_gas(2,1)+=vy*vz*mval;
                }
            }
        }

        if (pdata[i].M_gas>0) {
          pdata[i].veldisp_gas=pdata[i].veldisp_gas*(1.0/pdata[i].M_gas);
          pdata[i].cm_gas=pdata[i].cm_gas*(1.0/pdata[i].M_gas);
          pdata[i].cmvel_gas=pdata[i].cm_gas*(1.0/pdata[i].M_gas);
          pdata[i].Temp_gas/=pdata[i].M_gas;
#ifdef STARON
          pdata[i].Z_gas/=pdata[i].M_gas;
          pdata[i].SFR_gas/=pdata[i].M_gas;
#endif
        }

        //iterate for better cm if group large enough
        cmold=pdata[i].cm_gas;
        change=MAXVALUE;tol=1e-2;
        if (pdata[i].n_gas*opt.pinfo.cmfrac>=50) {
            ri=pdata[i].gsize;
            ri=ri*ri;
            cmold=pdata[i].cm_gas;
            rcmv=ri;
            while (true)
            {
                ri*=opt.pinfo.cmadjustfac;
                // find c/m of all particles within ri
                cmx=cmy=cmz=0.;
                EncMass=0.;
                Ninside=0;
                for (j=0;j<numingroup[i];j++) {
                Pval=&Part[j+noffset[i]];
                if (Pval->GetType()==GASTYPE)
                {
                    x = (*Pval).X() - cmold[0];
                    y = (*Pval).Y() - cmold[1];
                    z = (*Pval).Z() - cmold[2];
                    if ((x*x + y*y + z*z) <= ri)
                    {
                        cmx += (*Pval).GetMass()*(*Pval).X();
                        cmy += (*Pval).GetMass()*(*Pval).Y();
                        cmz += (*Pval).GetMass()*(*Pval).Z();
                        EncMass += (*Pval).GetMass();
                        Ninside++;
                    }
                }
                }
                if (Ninside > opt.pinfo.cmfrac * pdata[i].n_gas) {
                    pdata[i].cm_gas[0]=cmx;pdata[i].cm_gas[1]=cmy;pdata[i].cm_gas[2]=cmz;
                    for (k=0;k<3;k++) pdata[i].cm_gas[k] /= EncMass;
                    cmold=pdata[i].cm_gas;
                    rcmv=ri;
                }
                else break;
            }
            cmx=cmy=cmz=EncMass=0.;
            for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE)
            {
                x = (*Pval).X() - pdata[i].cm_gas[0];
                y = (*Pval).Y() - pdata[i].cm_gas[1];
                z = (*Pval).Z() - pdata[i].cm_gas[2];
                if ((x*x + y*y + z*z) <= rcmv)
                {
                    cmx += (*Pval).GetMass()*(*Pval).Vx();
                    cmy += (*Pval).GetMass()*(*Pval).Vy();
                    cmz += (*Pval).GetMass()*(*Pval).Vz();
                    EncMass += (*Pval).GetMass();
                }
            }
            }
            pdata[i].cmvel_gas[0]=cmx;pdata[i].cmvel_gas[1]=cmy;pdata[i].cmvel_gas[2]=cmz;
            for (k=0;k<3;k++) pdata[i].cmvel_gas[k] /= EncMass;
        }

        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE) {
                x = (*Pval).X()-pdata[i].cm_gas[0];
                y = (*Pval).Y()-pdata[i].cm_gas[1];
                z = (*Pval).Z()-pdata[i].cm_gas[2];
                r2=x*x+y*y+z*z;
                if (r2<=pdata[i].gRmaxvel*pdata[i].gRmaxvel) pdata[i].M_gas_rvmax+=Pval->GetMass();
                if (r2<=opt.lengthtokpc30pow2) pdata[i].M_gas_30kpc+=Pval->GetMass();
                if (r2<=opt.lengthtokpc50pow2) pdata[i].M_gas_50kpc+=Pval->GetMass();
                if (r2<=pdata[i].gR500c*pdata[i].gR500c) pdata[i].M_gas_500c+=Pval->GetMass();
            }
        }

        //rotational calcs
        if (pdata[i].n_gas>=10) {
        EncMass=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE) {
            x = (*Pval).X()-pdata[i].cm_gas[0];
            y = (*Pval).Y()-pdata[i].cm_gas[1];
            z = (*Pval).Z()-pdata[i].cm_gas[2];
            vx = (*Pval).Vx()-pdata[i].gcmvel[0]-pdata[i].cmvel_gas[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1]-pdata[i].cmvel_gas[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2]-pdata[i].cmvel_gas[2];
            mval=Pval->GetMass();
            EncMass+=mval;
            if (EncMass>0.5*pdata[i].M_gas && pdata[i].Rhalfmass_gas==0) pdata[i].Rhalfmass_gas=sqrt(x*x+y*y+z*z);
            jval=Coordinate(x,y,z).Cross(Coordinate(vx,vy,vz));
            jzval=(jval*pdata[i].L_gas)/pdata[i].L_gas.Length();
            zdist=(Coordinate(x,y,z)*pdata[i].L_gas)/pdata[i].L_gas.Length();
            Rdist=sqrt(x*x+y*y+z*z-zdist*zdist);
            pdata[i].Krot_gas+=mval*(jzval*jzval/(Rdist*Rdist));
            Ekin+=mval*(vx*vx+vy*vy+vz*vz);
            }
        }
        pdata[i].Krot_gas/=Ekin;
	    pdata[i].T_gas=0.5*Ekin;
        }
        if (pdata[i].n_gas>=10) GetGlobalSpatialMorphology(numingroup[i], &Part[noffset[i]], pdata[i].q_gas, pdata[i].s_gas, 1e-2, pdata[i].eigvec_gas,0,GASTYPE,0);
#endif
#ifdef STARON
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE) {
                pdata[i].n_star++;
                pdata[i].M_star+=Pval->GetMass();
            }
        }
        Ekin=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE) {
                mval=Pval->GetMass();
                pdata[i].t_star+=mval*Pval->GetTage();
#ifdef STARON
                pdata[i].Z_star+=mval*Pval->GetZmet();
#endif
                x = (*Pval).X();
                y = (*Pval).Y();
                z = (*Pval).Z();
                pdata[i].cm_star[0]+=x*Pval->GetMass();
                pdata[i].cm_star[1]+=y*Pval->GetMass();
                pdata[i].cm_star[2]+=z*Pval->GetMass();

                vx = (*Pval).Vx()-pdata[i].gcmvel[0];
                vy = (*Pval).Vy()-pdata[i].gcmvel[1];
                vz = (*Pval).Vz()-pdata[i].gcmvel[2];
                pdata[i].cmvel_star[0]+=vx*mval;
                pdata[i].cmvel_star[1]+=vy*mval;
                pdata[i].cmvel_star[2]+=vz*mval;

                pdata[i].L_star=pdata[i].L_star+Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*mval;
                if (pdata[i].n_star>=10) {
                    pdata[i].veldisp_star(0,0)+=vx*vx*mval;
                    pdata[i].veldisp_star(1,1)+=vy*vy*mval;
                    pdata[i].veldisp_star(2,2)+=vz*vz*mval;
                    pdata[i].veldisp_star(0,1)+=vx*vy*mval;
                    pdata[i].veldisp_star(0,2)+=vx*vz*mval;
                    pdata[i].veldisp_star(1,2)+=vy*vz*mval;
                    pdata[i].veldisp_star(1,0)+=vx*vy*mval;
                    pdata[i].veldisp_star(2,0)+=vx*vz*mval;
                    pdata[i].veldisp_star(2,1)+=vy*vz*mval;
                }
            }
        }
        if (pdata[i].M_star>0) {
            pdata[i].veldisp_star=pdata[i].veldisp_star*(1.0/pdata[i].M_star);
            pdata[i].cm_star=pdata[i].cm_star*(1.0/pdata[i].M_star);
            pdata[i].cmvel_star=pdata[i].cm_star*(1.0/pdata[i].M_star);
            pdata[i].t_star/=pdata[i].M_star;
            pdata[i].Z_star/=pdata[i].M_star;
        }
        //iterate for better cm if group large enough
        cmold=pdata[i].cm_star;
        change=MAXVALUE;tol=1e-2;
        if (pdata[i].n_star*opt.pinfo.cmfrac>=50) {
            ri=pdata[i].gsize;
            ri=ri*ri;
            cmold=pdata[i].cm_star;
            rcmv=ri;
            while (true)
            {
                ri*=opt.pinfo.cmadjustfac;
                // find c/m of all particles within ri
                cmx=cmy=cmz=0.;
                EncMass=0.;
                Ninside=0;
                for (j=0;j<numingroup[i];j++) {
                Pval=&Part[j+noffset[i]];
                if (Pval->GetType()==STARTYPE)
                {
                    x = (*Pval).X() - cmold[0];
                    y = (*Pval).Y() - cmold[1];
                    z = (*Pval).Z() - cmold[2];
                    if ((x*x + y*y + z*z) <= ri)
                    {
                        cmx += (*Pval).GetMass()*(*Pval).X();
                        cmy += (*Pval).GetMass()*(*Pval).Y();
                        cmz += (*Pval).GetMass()*(*Pval).Z();
                        EncMass += (*Pval).GetMass();
                        Ninside++;
                    }
                }
                }
                if (Ninside > opt.pinfo.cmfrac * pdata[i].n_star) {
                    pdata[i].cm_star[0]=cmx;pdata[i].cm_star[1]=cmy;pdata[i].cm_star[2]=cmz;
                    for (k=0;k<3;k++) pdata[i].cm_star[k] /= EncMass;
                    cmold=pdata[i].cm_star;
                    rcmv=ri;
                }
                else break;
            }
            cmx=cmy=cmz=EncMass=0.;
            for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE)
            {
                x = (*Pval).X() - pdata[i].cm_star[0];
                y = (*Pval).Y() - pdata[i].cm_star[1];
                z = (*Pval).Z() - pdata[i].cm_star[2];
                if ((x*x + y*y + z*z) <= rcmv)
                {
                    cmx += (*Pval).GetMass()*(*Pval).Vx();
                    cmy += (*Pval).GetMass()*(*Pval).Vy();
                    cmz += (*Pval).GetMass()*(*Pval).Vz();
                    EncMass += (*Pval).GetMass();
                }
            }
            }
            pdata[i].cmvel_star[0]=cmx;pdata[i].cmvel_star[1]=cmy;pdata[i].cmvel_star[2]=cmz;
            for (k=0;k<3;k++) pdata[i].cmvel_star[k] /= EncMass;
        }
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE) {
                x = (*Pval).X()-pdata[i].cm_star[0];
                y = (*Pval).Y()-pdata[i].cm_star[1];
                z = (*Pval).Z()-pdata[i].cm_star[2];
                r2=x*x+y*y+z*z;
                if (r2<=pdata[i].gRmaxvel*pdata[i].gRmaxvel) pdata[i].M_star_rvmax+=Pval->GetMass();
                if (r2<=opt.lengthtokpc30pow2) pdata[i].M_star_30kpc+=Pval->GetMass();
                if (r2<=opt.lengthtokpc50pow2) pdata[i].M_star_50kpc+=Pval->GetMass();
                if (r2<=pdata[i].gR500c*pdata[i].gR500c) pdata[i].M_star_500c+=Pval->GetMass();
            }
        }

        //rotational calcs
        if (pdata[i].n_star>=10) {
        EncMass=0.;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE) {
            x = (*Pval).X()-pdata[i].cm_star[0];
            y = (*Pval).Y()-pdata[i].cm_star[1];
            z = (*Pval).Z()-pdata[i].cm_star[2];
            vx = (*Pval).Vx()-pdata[i].gcmvel[0]-pdata[i].cmvel_star[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1]-pdata[i].cmvel_star[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2]-pdata[i].cmvel_star[2];
            mval=Pval->GetMass();
            EncMass+=mval;
            if (EncMass>0.5*pdata[i].M_star && pdata[i].Rhalfmass_star==0)
                pdata[i].Rhalfmass_star=sqrt(x*x+y*y+z*z);
            jval=Coordinate(x,y,z).Cross(Coordinate(vx,vy,vz));
            jzval=(jval*pdata[i].L_star)/pdata[i].L_star.Length();
            zdist=(Coordinate(x,y,z)*pdata[i].L_star)/pdata[i].L_star.Length();
            Rdist=sqrt(x*x+y*y+z*z-zdist*zdist);
            pdata[i].Krot_star+=mval*(jzval*jzval/(Rdist*Rdist));
            Ekin+=mval*(vx*vx+vy*vy+vz*vz);
            }
        }
        pdata[i].Krot_star/=Ekin;
	    pdata[i].T_star=0.5*Ekin;
        }
        if (pdata[i].n_star>=10) GetGlobalSpatialMorphology(numingroup[i], &Part[noffset[i]], pdata[i].q_star, pdata[i].s_star, 1e-2, pdata[i].eigvec_star,0,STARTYPE,0);
#endif

#ifdef BHON
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==BHTYPE) {
                pdata[i].n_bh++;
                pdata[i].M_bh+=Pval->GetMass();
            }
        }
#endif
#ifdef HIGHRES
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==DARKTYPE&&Pval->GetMass()>opt.zoomlowmassdm) {
                pdata[i].n_interloper++;
                pdata[i].M_interloper+=Pval->GetMass();
            }
        }
#endif

        //morphology calcs
#ifdef NOMASS
        GetGlobalSpatialMorphology(numingroup[i], &Part[noffset[i]], pdata[i].gq, pdata[i].gs, 1e-2, pdata[i].geigvec,0);
        //calculate morphology based on particles within RV, the radius of maximum circular velocity
        if (RV_num>=10) GetGlobalSpatialMorphology(RV_num, &Part[noffset[i]], pdata[i].RV_q, pdata[i].RV_s, 1e-2, pdata[i].RV_eigvec,0);
#else
        GetGlobalSpatialMorphology(numingroup[i], &Part[noffset[i]], pdata[i].gq, pdata[i].gs, 1e-2, pdata[i].geigvec,1);
        if (RV_num>=10) GetGlobalSpatialMorphology(RV_num, &Part[noffset[i]], pdata[i].RV_q, pdata[i].RV_s, 1e-2, pdata[i].RV_eigvec,1);
#endif

        //reset particle positions
        for (j=0;j<numingroup[i];j++) {
            x = (*Pval).X()+pdata[i].gcm[0];
            y = (*Pval).Y()+pdata[i].gcm[1];
            z = (*Pval).Z()+pdata[i].gcm[2];
            Pval->SetPosition(x,y,z);
        }
    }
#ifdef USEOPENMP
}
#endif

    for (i=1;i<=ngroup;i++) if (numingroup[i]>=omppropnum)
    {
        for (k=0;k<3;k++) pdata[i].gcm[k]=pdata[i].gcmvel[k]=0;
        pdata[i].gmass=pdata[i].gmaxvel=0.0;
        EncMass=cmx=cmy=cmz=0.;
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(j,Pval)
{
    #pragma omp for reduction(+:EncMass,cmx,cmy,cmz)
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            EncMass+=(*Pval).GetMass();
            cmx+=(*Pval).X()*(*Pval).GetMass();
            cmy+=(*Pval).Y()*(*Pval).GetMass();
            cmz+=(*Pval).Z()*(*Pval).GetMass();
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].gcm[0]=cmx;pdata[i].gcm[1]=cmy;pdata[i].gcm[2]=cmz;
        pdata[i].gmass=EncMass;
        for (k=0;k<3;k++){pdata[i].gcm[k]*=(1.0/pdata[i].gmass);pdata[i].gcmvel[k]*=(1.0/pdata[i].gmass);}
        pdata[i].gsize=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            for (k=0;k<3;k++) {
                Pval->SetPosition(k,(*Pval).GetPosition(k)-pdata[i].gcm[k]);
            }
        }
        qsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), RadCompare);
        ri=Part[noffset[i]+numingroup[i]-1].Radius();
        ri=ri*ri;
        //iterate for better cm if group large enough
        cmold[0]=cmold[1]=cmold[2]=0.;
        change=MAXVALUE;tol=1e-2;
        cmref=pdata[i].gcm;//cmold=pdata[i].gcm;
        rcmv=ri;
        ii=numingroup[i];
        while (true)
        {
            ri*=opt.pinfo.cmadjustfac;
            // find c/m of all particles within ri
            cmx=cmy=cmz=0.;
            EncMass=0.;
            Ninside=0;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z)
{
#pragma omp for reduction(+:EncMass,Ninside,cmx,cmy,cmz)
#endif
            for (j=0;j<numingroup[i];j++)
            {
                Pval=&Part[j+noffset[i]];
                x = (*Pval).X() - cmold[0];
                y = (*Pval).Y() - cmold[1];
                z = (*Pval).Z() - cmold[2];
                if ((x*x + y*y + z*z) <= ri)
                {
                    cmx += (*Pval).GetMass()*(*Pval).X();
                    cmy += (*Pval).GetMass()*(*Pval).Y();
                    cmz += (*Pval).GetMass()*(*Pval).Z();
                    EncMass += (*Pval).GetMass();
                    Ninside++;
                }
            }
#ifdef USEOPENMP
}
#endif
            x = Part[noffset[i]+ii-1].X() - cmold[0];
            y = Part[noffset[i]+ii-1].Y() - cmold[1];
            z = Part[noffset[i]+ii-1].Z() - cmold[2];
            if (Ninside > opt.pinfo.cmfrac * numingroup[i]) {
                cmold[0]=cmx;cmold[1]=cmy;cmold[2]=cmz;
                for (k=0;k<3;k++) cmold[k] /= EncMass;
                rcmv=ri;
                icmv=ii;
            }
            else break;
        }
        for (k=0;k<3;k++) pdata[i].gcm[k]+=cmold[k];
        cmx=cmy=cmz=EncMass=0.;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z)
{
    #pragma omp for reduction(+:EncMass,cmx,cmy,cmz)
#endif
        for (j=0;j<numingroup[i];j++)
        {
            Pval=&Part[j+noffset[i]];
            x = (*Pval).X() - cmold[0];
            y = (*Pval).Y() - cmold[1];
            z = (*Pval).Z() - cmold[2];
            Pval->SetPosition(x,y,z);
            if ((x*x + y*y + z*z) <= rcmv)
            {
                cmx += (*Pval).GetMass()*(*Pval).Vx();
                cmy += (*Pval).GetMass()*(*Pval).Vy();
                cmz += (*Pval).GetMass()*(*Pval).Vz();
                EncMass += (*Pval).GetMass();
            }
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].gcmvel[0]=cmx;pdata[i].gcmvel[1]=cmy;pdata[i].gcmvel[2]=cmz;
        for (k=0;k<3;k++) pdata[i].gcmvel[k] /= EncMass;
#ifdef NOMASS
        pdata[i].gmass*=opt.MassValue;
#endif
        if (pdata[i].gMFOF==0 && pdata[i].hostid==-1) pdata[i].gMFOF=pdata[i].gmass;
        qsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), RadCompare);
        pdata[i].gsize=Part[noffset[i]+numingroup[i]-1].Radius();

        //determine overdensity mass and radii. AGAIN REMEMBER THAT THESE ARE NOT MEANINGFUL FOR TIDAL DEBRIS
        //HERE MASSES ARE EXCLUSIVE!
        EncMass=pdata[i].gmass;
        if (opt.iInclusiveHalo==0 || opt.iInclusiveHalo!=0 && pdata[i].hostid!=-1) {
            for (j=numingroup[i]-1;j>=0;j--) {
                Pval=&Part[j+noffset[i]];
                rc=Pval->Radius();
                if (pdata[i].gRvir==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>virval)
                {pdata[i].gMvir=EncMass;pdata[i].gRvir=rc;}
                if (pdata[i].gR200c==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>m200val)
                {pdata[i].gM200c=EncMass;pdata[i].gR200c=rc;}
                if (pdata[i].gR200m==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>m200mval)
                {pdata[i].gM200m=EncMass;pdata[i].gR200m=rc;}
                if (pdata[i].gR500c==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>m500val)
                {pdata[i].gM500c=EncMass;pdata[i].gR500c=rc;}
                if (pdata[i].gRBN98==0 && EncMass>=0.01*pdata[i].gmass) if (log(EncMass)-3.0*log(rc)-log(4.0*M_PI/3.0)>mBN98val)
                {pdata[i].gMBN98=EncMass;pdata[i].gRBN98=rc;}
                if (pdata[i].gR200m!=0&&pdata[i].gR200c!=0&&pdata[i].gRvir!=0&&pdata[i].gR500c!=0&&pdata[i].gRBN98!=0) break;
#ifdef NOMASS
                EncMass-=Pval->GetMass()*opt.MassValue;
#else
                EncMass-=Pval->GetMass();
#endif
            }
            if (pdata[i].gRvir==0) {pdata[i].gMvir=pdata[i].gmass;pdata[i].gRvir=pdata[i].gsize;}
            if (pdata[i].gR200c==0) {pdata[i].gM200c=pdata[i].gmass;pdata[i].gR200c=pdata[i].gsize;}
            if (pdata[i].gR200m==0) {pdata[i].gM200m=pdata[i].gmass;pdata[i].gR200m=pdata[i].gsize;}
            if (pdata[i].gR500c==0) {pdata[i].gM500c=pdata[i].gmass;pdata[i].gR500c=pdata[i].gsize;}
            if (pdata[i].gRBN98==0) {pdata[i].gMBN98=pdata[i].gmass;pdata[i].gRBN98=pdata[i].gsize;}
        }

        EncMass=0;
        Double_t Jx,Jy,Jz,sxx,sxy,sxz,syy,syz,szz;
        Double_t Jx200m,Jy200m,Jz200m;
        Double_t Jx200c,Jy200c,Jz200c;
        Coordinate J;
        Ekin=Jx=Jy=Jz=sxx=sxy=sxz=syy=syz=szz=Krot=0.;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,rc,x,y,z,vx,vy,vz,J,mval)
{
    #pragma omp for reduction(+:Jx,Jy,Jz,sxx,sxy,sxz,syy,syz,szz,Ekin)
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            mval=Pval->GetMass();
            rc=(*Pval).Radius();
#ifdef NOMASS
            mval*=opt.MassValue;
#endif
            vx = (*Pval).Vx()-pdata[i].gcmvel[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2];
            J=Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*mval;
            Jx+=J[0];Jy+=J[1];Jz+=J[2];
            if (rc<pdata[i].gR200m) Jx200m+=J[0];Jy200m+=J[1];Jz200m+=J[2];
            if (rc<pdata[i].gR200c) Jx200c+=J[0];Jy200c+=J[1];Jz200c+=J[2];
            sxx+=vx*vx*mval;
            syy+=vy*vy*mval;
            szz+=vz*vz*mval;
            sxy+=vx*vy*mval;
            sxz+=vx*vz*mval;
            syz+=vy*vz*mval;
            Ekin+=(vx*vx+vy*vy+vz*vz)*mval;
        }
#ifdef USEOPENMP
}
#endif

        pdata[i].gJ[0]=Jx;
        pdata[i].gJ[1]=Jy;
        pdata[i].gJ[2]=Jz;
        pdata[i].gJ200m[0]=Jx200m;
        pdata[i].gJ200m[1]=Jy200m;
        pdata[i].gJ200m[2]=Jz200m;
        pdata[i].gJ200c[0]=Jx200c;
        pdata[i].gJ200c[1]=Jy200c;
        pdata[i].gJ200c[2]=Jz200c;
        pdata[i].gveldisp(0,0)=sxx;
        pdata[i].gveldisp(1,1)=syy;
        pdata[i].gveldisp(2,2)=szz;
        pdata[i].gveldisp(0,1)=pdata[i].gveldisp(1,0)=sxy;
        pdata[i].gveldisp(0,2)=pdata[i].gveldisp(2,0)=sxz;
        pdata[i].gveldisp(1,2)=pdata[i].gveldisp(2,1)=syz;
        pdata[i].gveldisp=pdata[i].gveldisp*(1.0/pdata[i].gmass);
        pdata[i].gsigma_v=pow(pdata[i].gveldisp.Det(),1.0/6.0);
        Ekin*=0.5;
        pdata[i].glambda_B=pdata[i].gJ.Length()/(pdata[i].gM200c*sqrt(2.0*opt.G*pdata[i].gM200c*pdata[i].gR200c));

#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z,vx,vy,vz,jval,jzval,zdist,Rdist)
{
    #pragma omp for reduction(+:Krot)
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            x = (*Pval).X();
            y = (*Pval).Y();
            z = (*Pval).Z();
            vx = (*Pval).Vx()-pdata[i].gcmvel[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2];
            jval=Coordinate(x,y,z).Cross(Coordinate(vx,vy,vz));
            jzval=(jval*pdata[i].gJ)/pdata[i].gJ.Length();
            zdist=(Coordinate(x,y,z)*pdata[i].gJ)/pdata[i].gJ.Length();
            Rdist=sqrt(x*x+y*y+z*z-zdist*zdist);
            Krot+=Pval->GetMass()*(jzval*jzval/(Rdist*Rdist));
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].Krot=0.5*Krot/Ekin;
#ifdef NOMASS
        pdata[i].Krot*=opt.MassValue;
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            EncMass+=Pval->GetMass();
            rc=Pval->Radius();
            if (rc>0) if (EncMass>0) vc=sqrt(opt.G*EncMass*opt.MassValue/rc);
            if (vc>pdata[i].gmaxvel) {pdata[i].gmaxvel=vc;pdata[i].gRmaxvel=rc;pdata[i].gMmaxvel=EncMass;RV_num=j+1;}
            if (EncMass>0.5*pdata[i].gmass && pdata[i].gRhalfmass==0) pdata[i].gRhalfmass=rc;
        }
        if (pdata[i].gRvir==0) {pdata[i].gMvir=pdata[i].gmass;pdata[i].gRvir=pdata[i].gsize;}
#ifdef NOMASS
        pdata[i].gMmaxvel*=opt.MassValue;
#endif

        //now that we have radius of maximum circular velocity, lets calculate properties internal to this radius
        Ekin=Jx=Jy=Jz=sxx=sxy=sxz=syy=syz=szz=Krot=0.;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z,vx,vy,vz,J,mval)
{
    #pragma omp for reduction(+:Jx,Jy,Jz,sxx,sxy,sxz,syy,syz,szz,Ekin)
#endif
        for (j=0;j<RV_num;j++) {
            Pval=&Part[j+noffset[i]];
            mval=Pval->GetMass();
#ifdef NOMASS
            mval*=opt.MassValue;
#endif
            vx = (*Pval).Vx()-pdata[i].gcmvel[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2];
            J=Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*mval;
            Jx+=J[0];Jy+=J[1];Jz+=J[2];
            sxx+=vx*vx*mval;
            syy+=vy*vy*mval;
            szz+=vz*vz*mval;
            sxy+=vx*vy*mval;
            sxz+=vx*vz*mval;
            syz+=vy*vz*mval;
            Ekin+=(vx*vx+vy*vy+vz*vz)*mval;
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].RV_J[0]=Jx;
        pdata[i].RV_J[1]=Jy;
        pdata[i].RV_J[2]=Jz;
        pdata[i].RV_veldisp(0,0)=sxx;
        pdata[i].RV_veldisp(1,1)=syy;
        pdata[i].RV_veldisp(2,2)=szz;
        pdata[i].RV_veldisp(0,1)=pdata[i].RV_veldisp(1,0)=sxy;
        pdata[i].RV_veldisp(0,2)=pdata[i].RV_veldisp(2,0)=sxz;
        pdata[i].RV_veldisp(1,2)=pdata[i].RV_veldisp(2,1)=syz;
        pdata[i].RV_veldisp=pdata[i].RV_veldisp*(1.0/pdata[i].gMmaxvel);
        pdata[i].RV_sigma_v=pow(pdata[i].RV_veldisp.Det(),1.0/6.0);
        Ekin*=0.5;
        pdata[i].RV_lambda_B=pdata[i].RV_J.Length()/(pdata[i].gMmaxvel*sqrt(2.0*opt.G*pdata[i].gMmaxvel*pdata[i].gRmaxvel));
        Krot=0;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z,vx,vy,vz,jval,jzval,zdist,Rdist)
{
    #pragma omp for reduction(+:Krot)
#endif
        for (j=0;j<RV_num;j++) {
            Pval=&Part[j+noffset[i]];
            x = (*Pval).X();
            y = (*Pval).Y();
            z = (*Pval).Z();
            vx = (*Pval).Vx()-pdata[i].gcmvel[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2];
            jval=Coordinate(x,y,z).Cross(Coordinate(vx,vy,vz));
            jzval=(jval*pdata[i].RV_J)/pdata[i].RV_J.Length();
            zdist=(Coordinate(x,y,z)*pdata[i].RV_J)/pdata[i].RV_J.Length();
            Rdist=sqrt(x*x+y*y+z*z-zdist*zdist);
            Krot+=Pval->GetMass()*(jzval*jzval/(Rdist*Rdist));
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].RV_Krot=0.5*Krot/Ekin;
#ifdef NOMASS
        pdata[i].RV_Krot*=opt.MassValue;
#endif

        //calculate the concentration based on prada 2012 where [(Vmax)/(GM/R)]^2-(0.216*c)/f(c)=0,
        //where f(c)=ln(1+c)-c/(1+c) and M is some "virial" mass and associated radius
        if (pdata[i].gR200c==0) pdata[i].VmaxVvir2=(pdata[i].gmaxvel*pdata[i].gmaxvel)/(opt.G*pdata[i].gmass/pdata[i].gsize);
        else pdata[i].VmaxVvir2=(pdata[i].gmaxvel*pdata[i].gmaxvel)/(opt.G*pdata[i].gM200c/pdata[i].gR200c);
        //always possible halo severly truncated before so correct if necessary and also for tidal debris, both vmax concentration pretty meaningless
        if (pdata[i].VmaxVvir2<=1.05) {
            if (pdata[i].gM200c==0) pdata[i].cNFW=pdata[i].gsize/pdata[i].gRmaxvel;
            else pdata[i].cNFW=pdata[i].gR200c/pdata[i].gRmaxvel;
        }
        else GetConcentration(pdata[i]);

    //baryons
#if defined(GASON)
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE) {
                pdata[i].n_gas++;
                pdata[i].M_gas+=Pval->GetMass();
            }
        }

        Ekin=Krot=Jx=Jy=Jz=sxx=sxy=sxz=syy=syz=szz=0.;
        Tsum=tsum=Zsum=sfrsum=0.;
        cmx=cmy=cmz=cmvx=cmvy=cmvz=0.;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z,vx,vy,vz,J,mval)
{
    #pragma omp for reduction(+:Jx,Jy,Jz,sxx,sxy,sxz,syy,syz,szz,cmx,cmy,cmz,cmvx,cmvy,cmvz,Tsum,Zsum,sfrsum)
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE) {
                mval=Pval->GetMass();

                x = (*Pval).X();
                y = (*Pval).Y();
                z = (*Pval).Z();
                vx = (*Pval).Vx()-pdata[i].gcmvel[0];
                vy = (*Pval).Vy()-pdata[i].gcmvel[1];
                vz = (*Pval).Vz()-pdata[i].gcmvel[2];

                cmx+=x*mval;
                cmy+=y*mval;
                cmz+=z*mval;

                cmvx+=vx*mval;
                cmvy+=vy*mval;
                cmvz+=vz*mval;

                J=Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*mval;
                Jx+=J[0];Jy+=J[1];Jz+=J[2];
                sxx+=vx*vx*mval;
                syy+=vy*vy*mval;
                szz+=vz*vz*mval;
                sxy+=vx*vy*mval;
                sxz+=vx*vz*mval;
                syz+=vy*vz*mval;

                Tsum+=mval*Pval->GetU();
#ifdef STARON
                Zsum+=mval*Pval->GetZmet();
                sfrsum+=mval*Pval->GetSFR();
#endif
            }
        }
#ifdef USEOPENMP
}
#endif
        //store data
        //store temperature in units of internal energy
        pdata[i].Temp_gas=Tsum;
        //pdata[i].sphden_gas+=Pval->GetMass()*Pval->GetSPHDen();
#ifdef STARON
        pdata[i].Z_gas=Zsum;
        pdata[i].SFR_gas=sfrsum;
#endif
        pdata[i].cm_gas[0]=cmx;pdata[i].cm_gas[1]=cmy;pdata[i].cm_gas[2]=cmz;
        pdata[i].cmvel_gas[0]=cmvx;pdata[i].cmvel_gas[1]=cmvy;pdata[i].cmvel_gas[2]=cmvz;
        pdata[i].L_gas[0]=Jx;pdata[i].L_gas[1]=Jy;pdata[i].L_gas[2]=Jz;
        if (pdata[i].n_gas>=10) {
            pdata[i].veldisp_gas(0,0)=sxx;
            pdata[i].veldisp_gas(1,1)=syy;
            pdata[i].veldisp_gas(2,2)=szz;
            pdata[i].veldisp_gas(0,1)=sxy;
            pdata[i].veldisp_gas(0,2)=sxz;
            pdata[i].veldisp_gas(1,2)=syz;
            pdata[i].veldisp_gas(1,0)=sxy;
            pdata[i].veldisp_gas(2,0)=sxz;
            pdata[i].veldisp_gas(2,1)=syz;
        }
        if (pdata[i].M_gas>0) {
            pdata[i].veldisp_gas=pdata[i].veldisp_gas*(1.0/pdata[i].M_gas);
            pdata[i].cm_gas=pdata[i].cm_gas*(1.0/pdata[i].M_gas);
            pdata[i].cmvel_gas=pdata[i].cm_gas*(1.0/pdata[i].M_gas);
            pdata[i].Temp_gas/=pdata[i].M_gas;
#ifdef STARON
            pdata[i].Z_gas/=pdata[i].M_gas;
            pdata[i].SFR_gas/=pdata[i].M_gas;
#endif
        }
        //iterate for better cm if group large enough
        cmold=pdata[i].cm_gas;
        change=MAXVALUE;tol=1e-2;
        if (pdata[i].n_gas*opt.pinfo.cmfrac>=50) {
            ri=pdata[i].gsize;
            ri=ri*ri;
            cmold=pdata[i].cm_gas;
            rcmv=ri;
            while (true)
            {
                ri*=opt.pinfo.cmadjustfac;
                // find c/m of all particles within ri
                cmx=cmy=cmz=0.;
                EncMass=0.;
                Ninside=0;
                for (j=0;j<numingroup[i];j++) {
                Pval=&Part[j+noffset[i]];
                if (Pval->GetType()==GASTYPE)
                {
                    x = (*Pval).X() - cmold[0];
                    y = (*Pval).Y() - cmold[1];
                    z = (*Pval).Z() - cmold[2];
                    if ((x*x + y*y + z*z) <= ri)
                    {
                        cmx += (*Pval).GetMass()*(*Pval).X();
                        cmy += (*Pval).GetMass()*(*Pval).Y();
                        cmz += (*Pval).GetMass()*(*Pval).Z();
                        EncMass += (*Pval).GetMass();
                        Ninside++;
                    }
                }
                }
                if (Ninside > opt.pinfo.cmfrac * pdata[i].n_gas) {
                    pdata[i].cm_gas[0]=cmx;pdata[i].cm_gas[1]=cmy;pdata[i].cm_gas[2]=cmz;
                    for (k=0;k<3;k++) pdata[i].cm_gas[k] /= EncMass;
                    cmold=pdata[i].cm_gas;
                    rcmv=ri;
                }
                else break;
            }
            cmx=cmy=cmz=EncMass=0.;
            for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE)
            {
                x = (*Pval).X() - pdata[i].cm_gas[0];
                y = (*Pval).Y() - pdata[i].cm_gas[1];
                z = (*Pval).Z() - pdata[i].cm_gas[2];
                if ((x*x + y*y + z*z) <= rcmv)
                {
                    cmx += (*Pval).GetMass()*(*Pval).Vx();
                    cmy += (*Pval).GetMass()*(*Pval).Vy();
                    cmz += (*Pval).GetMass()*(*Pval).Vz();
                    EncMass += (*Pval).GetMass();
                }
            }
            }
            pdata[i].cmvel_gas[0]=cmx;pdata[i].cmvel_gas[1]=cmy;pdata[i].cmvel_gas[2]=cmz;
            for (k=0;k<3;k++) pdata[i].cmvel_gas[k] /= EncMass;
        }

        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE) {
                x = (*Pval).X()-pdata[i].cm_gas[0];
                y = (*Pval).Y()-pdata[i].cm_gas[1];
                z = (*Pval).Z()-pdata[i].cm_gas[2];
                r2=x*x+y*y+z*z;
                if (r2<=pdata[i].gRmaxvel*pdata[i].gRmaxvel) pdata[i].M_gas_rvmax+=Pval->GetMass();
                if (r2<=opt.lengthtokpc30pow2) pdata[i].M_gas_30kpc+=Pval->GetMass();
                if (r2<=opt.lengthtokpc50pow2) pdata[i].M_gas_50kpc+=Pval->GetMass();
                if (r2<=pdata[i].gR500c*pdata[i].gR500c) pdata[i].M_gas_500c+=Pval->GetMass();
            }
        }

        //rotational calcs
        if (pdata[i].n_gas>=10) {
        EncMass=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE) {
                EncMass+=Pval->GetMass();
                x = (*Pval).X()-pdata[i].cm_gas[0];
                y = (*Pval).Y()-pdata[i].cm_gas[1];
                z = (*Pval).Z()-pdata[i].cm_gas[2];
                if (EncMass>0.5*pdata[i].M_gas && pdata[i].Rhalfmass_gas==0)
                    pdata[i].Rhalfmass_gas=sqrt(x*x+y*y+z*z);
            }
        }
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z,vx,vy,vz,jval,jzval,zdist,Rdist)
{
    #pragma omp for reduction(+:Krot,Ekin)
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==GASTYPE) {
            x = (*Pval).X()-pdata[i].cm_gas[0];
            y = (*Pval).Y()-pdata[i].cm_gas[1];
            z = (*Pval).Z()-pdata[i].cm_gas[2];
            vx = (*Pval).Vx()-pdata[i].gcmvel[0]-pdata[i].cmvel_gas[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1]-pdata[i].cmvel_gas[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2]-pdata[i].cmvel_gas[2];
            jval=Coordinate(x,y,z).Cross(Coordinate(vx,vy,vz));
            jzval=(jval*pdata[i].L_gas)/pdata[i].L_gas.Length();
            zdist=(Coordinate(x,y,z)*pdata[i].L_gas)/pdata[i].L_gas.Length();
            Rdist=sqrt(x*x+y*y+z*z-zdist*zdist);
            Krot+=Pval->GetMass()*(jzval*jzval/(Rdist*Rdist));
            Ekin+=Pval->GetMass()*(vx*vx+vy*vy+vz*vz);
            }
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].Krot_gas=Krot/Ekin;
        }
        if (pdata[i].n_gas>=10) GetGlobalSpatialMorphology(numingroup[i], &Part[noffset[i]], pdata[i].q_gas, pdata[i].s_gas, 1e-2, pdata[i].eigvec_gas,0,GASTYPE,0);
#ifdef NOMASS
        pdata[i].M_gas*=opt.MassValue;
#endif

#endif

#ifdef STARON
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE) {
                pdata[i].n_star++;
                pdata[i].M_star+=Pval->GetMass();
            }
        }
        Ekin=Krot=Jx=Jy=Jz=sxx=sxy=sxz=syy=syz=szz=0.;
        Tsum=tsum=Zsum=sfrsum=0.;
        cmx=cmy=cmz=cmvx=cmvy=cmvz=0.;
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z,vx,vy,vz,J,mval)
{
    #pragma omp for reduction(+:Jx,Jy,Jz,sxx,sxy,sxz,syy,syz,szz,cmx,cmy,cmz,cmvx,cmvy,cmvz,tsum,Zsum)
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE) {
                mval=Pval->GetMass();

                x = (*Pval).X();
                y = (*Pval).Y();
                z = (*Pval).Z();
                vx = (*Pval).Vx()-pdata[i].gcmvel[0];
                vy = (*Pval).Vy()-pdata[i].gcmvel[1];
                vz = (*Pval).Vz()-pdata[i].gcmvel[2];

                cmx+=x*mval;
                cmy+=y*mval;
                cmz+=z*mval;

                cmvx+=vx*mval;
                cmvy+=vy*mval;
                cmvz+=vz*mval;

                J=Coordinate(Pval->GetPosition()).Cross(Coordinate(vx,vy,vz))*mval;
                Jx+=J[0];Jy+=J[1];Jz+=J[2];
                sxx+=vx*vx*mval;
                syy+=vy*vy*mval;
                szz+=vz*vz*mval;
                sxy+=vx*vy*mval;
                sxz+=vx*vz*mval;
                syz+=vy*vz*mval;

                tsum+=mval*Pval->GetTage();
#ifdef GASON
                Zsum+=mval*Pval->GetZmet();
#endif
            }
        }
#ifdef USEOPENMP
}
#endif
        //store data
        pdata[i].t_star=tsum;
        pdata[i].Z_star=Zsum;

        pdata[i].cm_star[0]=cmx;pdata[i].cm_star[1]=cmy;pdata[i].cm_star[2]=cmz;
        pdata[i].cmvel_star[0]=cmvx;pdata[i].cmvel_star[1]=cmvy;pdata[i].cmvel_star[2]=cmvz;
        pdata[i].L_star[0]=Jx;pdata[i].L_star[1]=Jy;pdata[i].L_star[2]=Jz;
        if (pdata[i].n_star>=10) {
            pdata[i].veldisp_star(0,0)=sxx;
            pdata[i].veldisp_star(1,1)=syy;
            pdata[i].veldisp_star(2,2)=szz;
            pdata[i].veldisp_star(0,1)=sxy;
            pdata[i].veldisp_star(0,2)=sxz;
            pdata[i].veldisp_star(1,2)=syz;
            pdata[i].veldisp_star(1,0)=sxy;
            pdata[i].veldisp_star(2,0)=sxz;
            pdata[i].veldisp_star(2,1)=syz;
        }
        if (pdata[i].M_star>0) {
            pdata[i].veldisp_star=pdata[i].veldisp_star*(1.0/pdata[i].M_star);
            pdata[i].cm_star=pdata[i].cm_star*(1.0/pdata[i].M_star);
            pdata[i].cmvel_star=pdata[i].cm_star*(1.0/pdata[i].M_star);
            pdata[i].t_star/=pdata[i].M_star;
#ifdef GASON
            pdata[i].Z_star/=pdata[i].M_star;
#endif
        }
        //iterate for better cm if group large enough
        cmold=pdata[i].cm_star;
        change=MAXVALUE;tol=1e-2;
        if (pdata[i].n_star*opt.pinfo.cmfrac>=50) {
            ri=pdata[i].gsize;
            ri=ri*ri;
            cmold=pdata[i].cm_star;
            rcmv=ri;
            while (true)
            {
                ri*=opt.pinfo.cmadjustfac;
                // find c/m of all particles within ri
                cmx=cmy=cmz=0.;
                EncMass=0.;
                Ninside=0;
                for (j=0;j<numingroup[i];j++) {
                Pval=&Part[j+noffset[i]];
                if (Pval->GetType()==STARTYPE)
                {
                    x = (*Pval).X() - cmold[0];
                    y = (*Pval).Y() - cmold[1];
                    z = (*Pval).Z() - cmold[2];
                    if ((x*x + y*y + z*z) <= ri)
                    {
                        cmx += (*Pval).GetMass()*(*Pval).X();
                        cmy += (*Pval).GetMass()*(*Pval).Y();
                        cmz += (*Pval).GetMass()*(*Pval).Z();
                        EncMass += (*Pval).GetMass();
                        Ninside++;
                    }
                }
                }
                if (Ninside > opt.pinfo.cmfrac * pdata[i].n_star) {
                    pdata[i].cm_star[0]=cmx;pdata[i].cm_star[1]=cmy;pdata[i].cm_star[2]=cmz;
                    for (k=0;k<3;k++) pdata[i].cm_star[k] /= EncMass;
                    cmold=pdata[i].cm_star;
                    rcmv=ri;
                }
                else break;
            }
            cmx=cmy=cmz=EncMass=0.;
            for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE)
            {
                x = (*Pval).X() - pdata[i].cm_star[0];
                y = (*Pval).Y() - pdata[i].cm_star[1];
                z = (*Pval).Z() - pdata[i].cm_star[2];
                if ((x*x + y*y + z*z) <= rcmv)
                {
                    cmx += (*Pval).GetMass()*(*Pval).Vx();
                    cmy += (*Pval).GetMass()*(*Pval).Vy();
                    cmz += (*Pval).GetMass()*(*Pval).Vz();
                    EncMass += (*Pval).GetMass();
                }
            }
            }
            pdata[i].cmvel_star[0]=cmx;pdata[i].cmvel_star[1]=cmy;pdata[i].cmvel_star[2]=cmz;
            for (k=0;k<3;k++) pdata[i].cmvel_star[k] /= EncMass;
        }

        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE) {
                x = (*Pval).X()-pdata[i].cm_star[0];
                y = (*Pval).Y()-pdata[i].cm_star[1];
                z = (*Pval).Z()-pdata[i].cm_star[2];
                r2=x*x+y*y+z*z;
                if (r2<=pdata[i].gRmaxvel*pdata[i].gRmaxvel) pdata[i].M_star_rvmax+=Pval->GetMass();
                if (r2<=opt.lengthtokpc30pow2) pdata[i].M_star_30kpc+=Pval->GetMass();
                if (r2<=opt.lengthtokpc50pow2) pdata[i].M_star_50kpc+=Pval->GetMass();
                if (r2<=pdata[i].gR500c*pdata[i].gR500c) pdata[i].M_star_500c+=Pval->GetMass();
            }
        }

        //rotational calcs
        if (pdata[i].n_star>=10) {
        EncMass=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE) {
                EncMass+=Pval->GetMass();
                x = (*Pval).X()-pdata[i].cm_star[0];
                y = (*Pval).Y()-pdata[i].cm_star[1];
                z = (*Pval).Z()-pdata[i].cm_star[2];
                if (EncMass>0.5*pdata[i].M_star && pdata[i].Rhalfmass_star==0)
                    pdata[i].Rhalfmass_star=sqrt(x*x+y*y+z*z);
            }
        }
#ifdef USEOPENMP
#pragma omp parallel default(shared) \
private(j,Pval,x,y,z,vx,vy,vz,jval,jzval,zdist,Rdist)
{
    #pragma omp for reduction(+:Krot,Ekin)
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==STARTYPE) {
            x = (*Pval).X()-pdata[i].cm_star[0];
            y = (*Pval).Y()-pdata[i].cm_star[1];
            z = (*Pval).Z()-pdata[i].cm_star[2];
            vx = (*Pval).Vx()-pdata[i].gcmvel[0]-pdata[i].cmvel_star[0];
            vy = (*Pval).Vy()-pdata[i].gcmvel[1]-pdata[i].cmvel_star[1];
            vz = (*Pval).Vz()-pdata[i].gcmvel[2]-pdata[i].cmvel_star[2];
            jval=Coordinate(x,y,z).Cross(Coordinate(vx,vy,vz));
            jzval=(jval*pdata[i].L_star)/pdata[i].L_star.Length();
            zdist=(Coordinate(x,y,z)*pdata[i].L_star)/pdata[i].L_star.Length();
            Rdist=sqrt(x*x+y*y+z*z-zdist*zdist);
            Krot+=Pval->GetMass()*(jzval*jzval/(Rdist*Rdist));
            Ekin+=Pval->GetMass()*(vx*vx+vy*vy+vz*vz);
            }
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].Krot_star=Krot/Ekin;
        }

        if (pdata[i].n_star>=10) GetGlobalSpatialMorphology(numingroup[i], &Part[noffset[i]], pdata[i].q_star, pdata[i].s_star, 1e-2, pdata[i].eigvec_star,0,STARTYPE,0);
#ifdef NOMASS
        pdata[i].M_star*=opt.MassValue;
#endif
#endif

#ifdef BHON
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==BHTYPE) {
                pdata[i].n_bh++;
                pdata[i].M_bh+=Pval->GetMass();
            }
        }
#endif
#ifdef HIGHRES
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            if (Pval->GetType()==DARKTYPE&&Pval->GetMass()>opt.zoomlowmassdm) {
                pdata[i].n_interloper++;
                pdata[i].M_interloper+=Pval->GetMass();
            }
        }
#endif

#ifdef NOMASS
        GetGlobalSpatialMorphology(numingroup[i], &Part[noffset[i]], pdata[i].gq, pdata[i].gs, 1e-2, pdata[i].geigvec,0);
        if (RV_num>=10) GetGlobalSpatialMorphology(RV_num, &Part[noffset[i]], pdata[i].RV_q, pdata[i].RV_s, 1e-2, pdata[i].RV_eigvec,0);
#else
        GetGlobalSpatialMorphology(numingroup[i], &Part[noffset[i]], pdata[i].gq, pdata[i].gs, 1e-2, pdata[i].geigvec,1);
        if (RV_num>=10) GetGlobalSpatialMorphology(RV_num, &Part[noffset[i]], pdata[i].RV_q, pdata[i].RV_s, 1e-2, pdata[i].RV_eigvec,1);
#endif
        //reset particle positions
        for (j=0;j<numingroup[i];j++) {
            x = (*Pval).X()+pdata[i].gcm[0];
            y = (*Pval).Y()+pdata[i].gcm[1];
            z = (*Pval).Z()+pdata[i].gcm[2];
            Pval->SetPosition(x,y,z);
        }
    }
    //loop over groups for black hole properties
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,Pval)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
    for (i=1;i<=ngroup;i++) if (numingroup[i]<omppropnum)
    {
    }
#ifdef USEOPENMP
}
#endif

        if (opt.iverbose) cout<<"Done getting properties"<<endl;
}

///Get inclusive halo FOF based masses. If requesting spherical overdensity masses then extra computation and search required
void GetInclusiveMasses(Options &opt, const Int_t nbodies, Particle *Part, Int_t ngroup, Int_t *&pfof, Int_t *&numingroup, PropData *&pdata, Int_t *&noffset)
{
    Particle *Pval;
    KDTree *tree;
    Double_t *period=NULL;
    Int_t i,j,k;
    if (opt.iverbose) {
        cout<<"Get inclusive masses"<<endl;
        if (opt.iInclusiveHalo==1) cout<<" with masses based on the FOF envelopes (quicker)"<<endl;
        else if (opt.iInclusiveHalo==2) cout<<" with masses based on full SO search (slower)"<<endl;
    }
    Double_t ri,rcmv,r2,cmx,cmy,cmz,EncMass,Ninside;
    Double_t vc,rc,x,y,z,vx,vy,vz;
    Coordinate cmold(0.),cmref;
    Double_t change=MAXVALUE,tol=1e-2;
    Int_t ii,icmv,numinvir,num200c,num200m;
    Double_t virval=log(opt.virlevel*opt.rhobg);
    Double_t mBN98val=log(opt.virBN98*opt.rhobg);
    Double_t m200val=log(opt.rhobg/opt.Omega_m*200.0);
    Double_t m200mval=log(opt.rhobg*200.0);
    Double_t m500val=log(opt.rhobg/opt.Omega_m*500.0);
    Double_t fac,rhoval,rhoval2;
    Double_t time1=MyGetTime(),time2;
    int nthreads=1,tid;
#ifdef USEOPENMP
#pragma omp parallel
    {
            if (omp_get_thread_num()==0) nthreads=omp_get_num_threads();
    }
#endif

    for (i=1;i<=ngroup;i++) pdata[i].gNFOF=numingroup[i];

    //first get center of mass and maximum size

    //for small groups loop over groups
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,Pval,ri,rcmv,r2,cmx,cmy,cmz,EncMass,Ninside,cmold,change,tol,x,y,z,vc,rc,vx,vy,vz,numinvir,num200c,num200m)\
firstprivate(virval,m200val,m200mval,mBN98val)
{
    #pragma omp for schedule(dynamic) nowait
#endif
    for (i=1;i<=ngroup;i++) if (numingroup[i]<omppropnum)
    {
        for (k=0;k<3;k++) pdata[i].gcm[k]=0;
        pdata[i].gmass=pdata[i].gmaxvel=0.0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            pdata[i].gmass+=(*Pval).GetMass();
            for (k=0;k<3;k++) {
                pdata[i].gcm[k]+=(*Pval).GetPosition(k)*(*Pval).GetMass();
            }
        }
        for (k=0;k<3;k++)pdata[i].gcm[k]*=(1.0/pdata[i].gmass);
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            x = (*Pval).X() - pdata[i].gcm[0];
            y = (*Pval).Y() - pdata[i].gcm[1];
            z = (*Pval).Z() - pdata[i].gcm[2];
            Pval->SetPosition(x,y,z);
        }
        gsl_heapsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), RadCompare);
        pdata[i].gsize=Part[noffset[i]+numingroup[i]-1].Radius();
        pdata[i].gRhalfmass=Part[noffset[i]+(numingroup[i]/2)].Radius();
#ifdef NOMASS
        pdata[i].gmass*=opt.MassValue;
#endif
        pdata[i].gMFOF=pdata[i].gmass;
    }
#ifdef USEOPENMP
}
#endif
    //now large groups
    for (i=1;i<=ngroup;i++) if (numingroup[i]>=omppropnum)
    {
        for (k=0;k<3;k++) pdata[i].gcm[k]=0;
        pdata[i].gmass=pdata[i].gmaxvel=0.0;
        EncMass=cmx=cmy=cmz=0.;
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(j,Pval)
{
#pragma omp for reduction(+:EncMass,cmx,cmy,cmz)
#endif
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            EncMass+=(*Pval).GetMass();
            cmx+=(*Pval).X()*(*Pval).GetMass();
            cmy+=(*Pval).Y()*(*Pval).GetMass();
            cmz+=(*Pval).Z()*(*Pval).GetMass();
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].gcm[0]=cmx;pdata[i].gcm[1]=cmy;pdata[i].gcm[2]=cmz;
        pdata[i].gmass=EncMass;
        pdata[i].gMFOF=EncMass;
        for (k=0;k<3;k++)pdata[i].gcm[k]*=(1.0/pdata[i].gmass);
        pdata[i].gsize=0;
        for (j=0;j<numingroup[i];j++) {
            Pval=&Part[j+noffset[i]];
            for (k=0;k<3;k++) {
                Pval->SetPosition(k,(*Pval).GetPosition(k)-pdata[i].gcm[k]);
            }
        }
        qsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), RadCompare);
        pdata[i].gsize=Part[noffset[i]+numingroup[i]-1].Radius();
        pdata[i].gRhalfmass=Part[noffset[i]+(numingroup[i]/2)].Radius();
#ifdef NOMASS
        pdata[i].gmass*=opt.MassValue;
#endif
        pdata[i].gMFOF=pdata[i].gmass;
    }
    //once center of masses have been found if want simple inclusive masses based on the FOF envelop
    if (opt.iInclusiveHalo==1) {
        fac=-log(4.0*M_PI/3.0);
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,Pval,ri,rcmv,r2,cmx,cmy,cmz,EncMass,Ninside,cmold,change,tol,x,y,z,vc,rc,vx,vy,vz,numinvir,num200c,num200m,rhoval)\
firstprivate(virval,m200val,m200mval,mBN98val)
{
    #pragma omp for schedule(dynamic) nowait
#endif
    for (i=1;i<=ngroup;i++)
    {

        //here masses are technically exclusive but this routine is generally called before objects are separated into halo/substructures
        EncMass=pdata[i].gmass;
        for (j=numingroup[i]-1;j>=0;j--) {
            Pval=&Part[j+noffset[i]];
            rc=Pval->Radius();
            rhoval=log(EncMass)-3.0*log(rc)+fac;
            if (pdata[i].gRvir==0 && EncMass>=0.01*pdata[i].gmass) if (rhoval>virval)
            {pdata[i].gMvir=EncMass;pdata[i].gRvir=rc;}
            if (pdata[i].gR200c==0 && EncMass>=0.01*pdata[i].gmass) if (rhoval>m200val)
            {pdata[i].gM200c=EncMass;pdata[i].gR200c=rc;}
            if (pdata[i].gR200m==0 && EncMass>=0.01*pdata[i].gmass) if (rhoval>m200mval)
            {pdata[i].gM200m=EncMass;pdata[i].gR200m=rc;}
            if (pdata[i].gR500c==0 && EncMass>=0.01*pdata[i].gmass) if (rhoval>m500val)
            {pdata[i].gM500c=EncMass;pdata[i].gR500c=rc;}
            if (pdata[i].gRBN98==0 && EncMass>=0.01*pdata[i].gMBN98) if (rhoval>mBN98val)
            {pdata[i].gMBN98=EncMass;pdata[i].gRBN98=rc;}
#ifdef NOMASS
            EncMass-=opt.MassValue;
#else
            EncMass-=Pval->GetMass();
#endif
            if (pdata[i].gR200m!=0&&pdata[i].gR200c!=0&&pdata[i].gRvir!=0&&pdata[i].gR500c!=0&&pdata[i].gRBN98!=0) break;
        }
        //if overdensity never drops below thresholds then masses are equal to FOF mass or total mass.
        if (pdata[i].gRvir==0) {pdata[i].gMvir=pdata[i].gmass;pdata[i].gRvir=pdata[i].gsize;}
        if (pdata[i].gR200c==0) {pdata[i].gM200c=pdata[i].gmass;pdata[i].gR200c=pdata[i].gsize;}
        if (pdata[i].gR200m==0) {pdata[i].gM200m=pdata[i].gmass;pdata[i].gR200m=pdata[i].gsize;}
        if (pdata[i].gR500c==0) {pdata[i].gM500c=pdata[i].gmass;pdata[i].gR500c=pdata[i].gsize;}
        if (pdata[i].gRBN98==0) {pdata[i].gMBN98=pdata[i].gmass;pdata[i].gRBN98=pdata[i].gsize;}
    }
#ifdef USEOPENMP
}
#endif

    //reset the positions of the particles
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,x,y,z)
{
    #pragma omp for schedule(dynamic) nowait
#endif
        for (i=1;i<=ngroup;i++)
        {
            //reset positions
            for (j=0;j<numingroup[i];j++) {
                Pval=&Part[j+noffset[i]];
                x = (*Pval).X()+pdata[i].gcm[0];
                y = (*Pval).Y()+pdata[i].gcm[1];
                z = (*Pval).Z()+pdata[i].gcm[2];
                Pval->SetPosition(x,y,z);
            }
        }
#ifdef USEOPENMP
}
#endif
    }
    //if want SO masses including all particles then must
    //search the mpi local particle data for any halos whose size
    //extends outside of the local mpi domain
    //if object does not, then can proceed locally otherwise, likely have to
    //search other mpi domains for particles of interest.
    else if (opt.iInclusiveHalo==2){
        //first we need to store the indices so we can place particles back in the order they need to be
        //as we are going to build a tree to search particles
        vector<Int_t> ids(nbodies);
        for (i=0;i<nbodies;i++) ids[i]=Part[i].GetID();

        vector<Int_t> taggedparts;
        vector<Double_t> radii;
        vector<Double_t> masses;
        vector<Int_t> indices;
        size_t n;
        Double_t dx;
        vector<Double_t> maxrdist(ngroup+1);
        //to store particle ids of those in SO volume.
        vector<Int_t> SOpids;
        vector<Int_t> *SOpartlist=new vector<Int_t>[ngroup+1];

        //set period
        if (opt.p>0) {
            period=new Double_t[3];
            for (int j=0;j<3;j++) period[j]=opt.p;
#ifdef USEMPI
            mpi_period=opt.p;
#endif
        }

        //reset the positions of the particles in local domain
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,x,y,z)
{
    #pragma omp for schedule(dynamic) nowait
#endif
        for (i=1;i<=ngroup;i++)
        {
            //reset positions
            for (j=0;j<numingroup[i];j++) {
                Pval=&Part[j+noffset[i]];
                x = (*Pval).X()+pdata[i].gcm[0];
                y = (*Pval).Y()+pdata[i].gcm[1];
                z = (*Pval).Z()+pdata[i].gcm[2];
                Pval->SetPosition(x,y,z);
            }
        }
#ifdef USEOPENMP
}
#endif
        //build tree optimised to search for more than min group size
        //this is the bottle neck for the SO calculation. Wonder if there is an easy
        //way of speeding it up
        tree=new KDTree(Part,nbodies,opt.HaloMinSize,tree->TPHYS,tree->KEPAN,100,0,0,0,period);
        //store the radii that will be used to search for each group
        //this is based on maximum radius and the enclosed density within the FOF so that if
        //this density is larger than desired overdensity then we must increase the radius
        fac=-log(4.0*M_PI/3.0)-m200mval;
        Double_t radfac;

        for (i=1;i<=ngroup;i++) {
            radfac=max(1.0,exp(1.0/3.0*(log(pdata[i].gMFOF)-3.0*log(pdata[i].gsize)+fac)));
            maxrdist[i]=pdata[i].gsize*opt.SphericalOverdensitySeachFac*radfac;
        }
#ifdef USEMPI
        //if using mpi then determine if halo's search radius overlaps another mpi domain
        vector<bool> halooverlap;
        KDTree *treeimport=NULL;
        Int_t nimport,nexport;
        if (NProcs>1) {
        halooverlap= MPIGetHaloSearchExportNum(ngroup, pdata, maxrdist);
        NNDataIn = new nndata_in[NExport];
        NNDataGet = new nndata_in[NImport];
        //build the exported halo group list using NNData structures
        MPIBuildHaloSearchExportList(ngroup, pdata, maxrdist,halooverlap);
        MPIGetHaloSearchImportNum(nbodies, tree, Part);
        PartDataIn = new Particle[NExport+1];
        PartDataGet = new Particle[NImport+1];
        //run search on exported particles and determine which local particles need to be exported back (or imported)
        nimport=MPIBuildParticleNNImportList(nbodies, tree, Part);
        if (nimport>0) treeimport=new KDTree(PartDataGet,nimport,opt.HaloMinSize,tree->TPHYS,tree->KEPAN,100,0,0,0,period);
        }
#endif
        time2=MyGetTime();
        //now loop over groups and search for particles. This is probably fast if we build a tree
        fac=-log(4.0*M_PI/3.0);
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,taggedparts,radii,masses,indices,n,dx,EncMass,rc,rhoval,rhoval2,tid,SOpids)
{
    #pragma omp for schedule(dynamic) nowait
#endif
        for (i=1;i<=ngroup;i++)
        {
            taggedparts=tree->SearchBallPosTagged(pdata[i].gcm,pow(maxrdist[i],2.0));
            radii.resize(taggedparts.size());
            masses.resize(taggedparts.size());
            if (opt.iSphericalOverdensityPartList) SOpids.resize(taggedparts.size());
            for (j=0;j<taggedparts.size();j++) {
                masses[j]=Part[taggedparts[j]].GetMass();
                if (opt.iSphericalOverdensityPartList) SOpids[j]=Part[taggedparts[j]].GetPID();
                radii[j]=0;
                for (k=0;k<3;k++) {
                    dx=Part[taggedparts[j]].GetPosition(k)-pdata[i].gcm[k];
                    //correct for period
                    if (opt.p>0) {
                        if (dx>opt.p*0.5) dx-=opt.p;
                        else if (dx<-opt.p*0.5) dx+=opt.p;
                    }
                    radii[j]+=dx*dx;
                }
                radii[j]=sqrt(radii[j]);
            }
            taggedparts.clear();
#ifdef USEMPI
            if (NProcs>1) {
                //if halo has overlap then search the imported particles as well, add them to the radii and mass vectors
                if (halooverlap[i]&&nimport>0) {
                    taggedparts=treeimport->SearchBallPosTagged(pdata[i].gcm,pow(maxrdist[i],2.0));
                    Int_t offset=radii.size();
                    radii.resize(radii.size()+taggedparts.size());
                    masses.resize(masses.size()+taggedparts.size());
                    if (opt.iSphericalOverdensityPartList) SOpids.resize(SOpids.size()+taggedparts.size());
                    for (j=0;j<taggedparts.size();j++) {
                        masses[offset+j]=Part[taggedparts[j]].GetMass();
                        if (opt.iSphericalOverdensityPartList) SOpids[j+offset]=Part[taggedparts[j]].GetPID();
                        radii[offset+j]=0;
                        for (k=0;k<3;k++) {
                            dx=Part[taggedparts[j]].GetPosition(k)-pdata[i].gcm[k];
                            //correct for period
                            if (opt.p>0) {
                                if (dx>opt.p*0.5) dx-=opt.p;
                                else if (dx<-opt.p*0.5) dx+=opt.p;
                            }
                            radii[offset+j]+=dx*dx;
                        }
                        radii[offset+j]=sqrt(radii[offset+j]);
                    }
                    taggedparts.clear();
                }
            }
#endif
            //get incides
            indices.resize(radii.size());
            n=0;generate(indices.begin(), indices.end(), [&]{ return n++; });
            //sort by radius
            auto comparator = [&radii](int a, int b){ return radii[a] < radii[b]; };
            sort(indices.begin(), indices.end(), comparator);
            //now loop over radii
            //then get overdensity working outwards from some small fraction of the mass or at least 4 particles + small fraction of min halo size
            int minnum=max((int)(0.05*radii.size()),(int)(opt.HaloMinSize*0.05+4));
            int iindex=radii.size();
            EncMass=0;for (j=0;j<minnum;j++) EncMass+=masses[indices[j]];
            rc=radii[indices[minnum-1]];
            rhoval2=log(EncMass)-3.0*log(rc)+fac;
            for (j=minnum;j<radii.size();j++) {
                rc=radii[indices[j]];
#ifdef NOMASS
                EncMass+=opt.MassValue;
#else
                EncMass+=masses[indices[j]];
#endif
                rhoval=log(EncMass)-3.0*log(rc)+fac;
                if (pdata[i].gRvir==0) if (rhoval<=virval) {
                    if (rhoval2>virval) {
                        //linearly interpolate, unless previous density also below threshold (which would happen at the start, then just set value)
                        pdata[i].gRvir=exp(log(rc/radii[indices[j-1]])/(rhoval-rhoval2)*(virval-rhoval2)+log(radii[indices[j-1]]));
                        pdata[i].gMvir=exp(log(EncMass/(EncMass-masses[indices[j-1]]))/(rhoval-rhoval2)*(virval-rhoval2)+log(EncMass-masses[indices[j-1]]));
                    }
                    else {pdata[i].gMvir=EncMass;pdata[i].gRvir=rc;}
                }
                if (pdata[i].gR200c==0) if (rhoval<=m200val)
                {
                    if (rhoval2>m200val) {
                        pdata[i].gR200c=exp(log(rc/radii[indices[j-1]])/(rhoval-rhoval2)*(m200val-rhoval2)+log(radii[indices[j-1]]));
                        pdata[i].gM200c=exp(log(EncMass/(EncMass-masses[indices[j-1]]))/(rhoval-rhoval2)*(m200val-rhoval2)+log(EncMass-masses[indices[j-1]]));
                    }
                    else {pdata[i].gM200c=EncMass;pdata[i].gR200c=rc;}
                }
                if (pdata[i].gR200m==0) if (rhoval<=m200mval) {
                    if (rhoval2>m200mval) {
                        pdata[i].gR200m=exp(log(rc/radii[indices[j-1]])/(rhoval-rhoval2)*(m200mval-rhoval2)+log(radii[indices[j-1]]));
                        pdata[i].gM200m=exp(log(EncMass/(EncMass-masses[indices[j-1]]))/(rhoval-rhoval2)*(m200mval-rhoval2)+log(EncMass-masses[indices[j-1]]));
                    }
                    else {pdata[i].gM200m=EncMass;pdata[i].gR200m=rc;}
                    //use lowest density threshold and get index
                    iindex=j-1;
                }
                if (pdata[i].gR500c==0) if (rhoval<=m500val){
                    if (rhoval2>m500val) {
                        pdata[i].gR500c=exp(log(rc/radii[indices[j-1]])/(rhoval-rhoval2)*(m500val-rhoval2)+log(radii[indices[j-1]]));
                        pdata[i].gM500c=exp(log(EncMass/(EncMass-masses[indices[j-1]]))/(rhoval-rhoval2)*(m500val-rhoval2)+log(EncMass-masses[indices[j-1]]));
                    }
                    else {pdata[i].gM500c=EncMass;pdata[i].gR500c=rc;}
                }
                if (pdata[i].gRBN98==0) if (rhoval<=mBN98val){
                    if (rhoval2>mBN98val) {
                        pdata[i].gRBN98=exp(log(rc/radii[indices[j-1]])/(rhoval-rhoval2)*(mBN98val-rhoval2)+log(radii[indices[j-1]]));
                        pdata[i].gMBN98=exp(log(EncMass/(EncMass-masses[indices[j-1]]))/(rhoval-rhoval2)*(mBN98val-rhoval2)+log(EncMass-masses[indices[j-1]]));
                    }
                    else {pdata[i].gMBN98=EncMass;pdata[i].gRBN98=rc;}
                }
                rhoval2=rhoval;
                if (pdata[i].gR200m!=0&&pdata[i].gR200c!=0&&pdata[i].gRvir!=0&&pdata[i].gR500c!=0&&pdata[i].gRBN98!=0) break;
            }
            //if overdensity never drops below thresholds then masses are equal to FOF mass or total mass.
            if (pdata[i].gRvir==0) {pdata[i].gMvir=pdata[i].gmass;pdata[i].gRvir=pdata[i].gsize;}
            if (pdata[i].gR200c==0) {pdata[i].gM200c=pdata[i].gmass;pdata[i].gR200c=pdata[i].gsize;}
            if (pdata[i].gR200m==0) {pdata[i].gM200m=pdata[i].gmass;pdata[i].gR200m=pdata[i].gsize;}
            if (pdata[i].gR500c==0) {pdata[i].gM500c=pdata[i].gmass;pdata[i].gR500c=pdata[i].gsize;}
            if (pdata[i].gRBN98==0) {pdata[i].gMBN98=pdata[i].gmass;pdata[i].gRBN98=pdata[i].gsize;}

            if (opt.iSphericalOverdensityPartList) {
                SOpartlist[i].resize(iindex);
                for (j=0;j<iindex;j++) SOpartlist[i][j]=SOpids[indices[j]];
                SOpids.clear();
            }
            indices.clear();
            radii.clear();
            masses.clear();
        }
#ifdef USEOPENMP
    }
#endif
        delete tree;
        //reset its after putting particles back in input order
        for (i=0;i<nbodies;i++) Part[i].SetID(ids[i]);
        ids.clear();
        //write the particle lists
        if (opt.iSphericalOverdensityPartList) {
            WriteSOCatalog(opt, ngroup, SOpartlist);
            delete[] SOpartlist;
        }
#ifdef USEMPI
        mpi_period=0;
        if (NProcs>1) {
            if (treeimport!=NULL) delete treeimport;
            delete[] PartDataGet;
            delete[] PartDataIn;
            delete[] NNDataGet;
            delete[] NNDataIn;
        }
#endif
    }

    if (opt.iverbose) cout<<"Done inclusive masses for field objects in "<<MyGetTime()-time1<<endl;
}
//@}

///\name Routines to calculate specific property of a set of particles
//@{
///Get spatial morphology using iterative procedure
void GetGlobalSpatialMorphology(const Int_t nbodies, Particle *p, Double_t& q, Double_t& s, Double_t Error, Matrix& eigenvec, int imflag, int itype, int iiterate)
{
    // Calculate the axial ratios q and s.
    int MAXIT=10;
    Double_t oldq,olds;
    Coordinate e;
    Matrix R(0.),M(0.0),eigenvecp(0.);
    eigenvec=Matrix(0.);
    eigenvec(0,0)=eigenvec(1,1)=eigenvec(2,2)=1.0;
    // Iterative procedure.  See Dubinski and Carlberg (1991).
    int i=0;
    if (iiterate) {
    do
    {
        M = Matrix(0.0);
        eigenvecp=Matrix(0.);
        if (imflag==1)CalcMTensorWithMass(M, q, s, nbodies, p,itype);
        else CalcMTensor(M, q, s, nbodies, p,itype);
        e = M.Eigenvalues();
        oldq = q;olds = s;
        q = sqrt(e[1] / e[0]);s = sqrt(e[2] / e[0]);
        eigenvecp=M.Eigenvectors(e);
        eigenvec=eigenvecp*eigenvec;
        RotParticles(nbodies, p, eigenvecp);
        i++;
    } while ((fabs(olds - s) > Error || fabs(oldq - q) > Error) && i<MAXIT);
    //rotate system back to original coordinate frame
    R=eigenvec.Transpose();
    RotParticles(nbodies, p, R);
    }
    else {
        if (imflag==1)CalcMTensorWithMass(M, q, s, nbodies, p,itype);
        else CalcMTensor(M, q, s, nbodies, p,itype);
        e = M.Eigenvalues();
        oldq = q;olds = s;
        q = sqrt(e[1] / e[0]);s = sqrt(e[2] / e[0]);
        eigenvecp=M.Eigenvectors(e);
        eigenvec=eigenvecp*eigenvec;
    }
}

///calculate the inertia tensor and return the dispersions (weight by 1/mtot)
void CalcITensor(const Int_t n, Particle *p, Double_t &a, Double_t &b, Double_t &c, Matrix& eigenvec, Matrix &I, int itype)
{
    Double_t r2,det,Ixx,Iyy,Izz,Ixy,Ixz,Iyz, weight;
    Coordinate e;
    I=Matrix(0.);
    Int_t i;
    Ixx=Iyy=Izz=Ixy=Ixz=Iyz=0.;
    Double_t mtot=0;
#ifdef USEOPENMP
    if (n>=ompunbindnum) {
#pragma omp parallel default(shared) \
private(i,r2,weight)
{
#pragma omp for schedule(dynamic) nowait reduction(+:Ixx,Iyy,Izz,Ixy,Ixz,Iyz,mtot)
    for (i = 0; i < n; i++)
    {
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        r2=p[i].X()*p[i].X()+p[i].Y()*p[i].Y()+p[i].Z()*p[i].Z();
        Ixx+=(r2-p[i].X()*p[i].X())*weight;
        Iyy+=(r2-p[i].Y()*p[i].Y())*weight;
        Izz+=(r2-p[i].Z()*p[i].Z())*weight;
        Ixy+=(-p[i].X()*p[i].Y())*weight;
        Ixz+=(-p[i].X()*p[i].Z())*weight;
        Iyz+=(-p[i].Y()*p[i].Z())*weight;
        mtot+=weight;
    }
}
    I(0,0)=Ixx;I(1,1)=Iyy;I(2,2)=Izz;
    I(0,1)=I(1,0)=Ixy;
    I(0,2)=I(2,0)=Ixz;
    I(1,2)=I(2,1)=Iyz;
    }
    else {
#endif
    for (i = 0; i < n; i++)
    {
        r2=p[i].X()*p[i].X()+p[i].Y()*p[i].Y()+p[i].Z()*p[i].Z();
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        for (int j = 0; j < 3; j++)
        {
            for (int k = 0; k < 3; k++)
            {
                I(j, k) += ((j==k)*r2-p[i].GetPosition(j) *p[i].GetPosition(k))*weight;
            }
        }
        mtot+=weight;
    }
#ifdef USEOPENMP
    }
#endif
    //det=I.Det();
    //for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) I(j, k) /= det;
    I=I*(1.0/mtot);
    e = I.Eigenvalues();
    a=e[0];b=e[1];c=e[2];
    eigenvec=I.Eigenvectors(e);
    I=I*mtot;
}

///calculate the position dispersion tensor
void CalcPosSigmaTensor(const Int_t n, Particle *p, Double_t &a, Double_t &b, Double_t &c, Matrix& eigenvec, Matrix &I, int itype)
{
    Double_t r2,det,Ixx,Iyy,Izz,Ixy,Ixz,Iyz, weight;
    Coordinate e;
    I=Matrix(0.);
    Int_t i;
    Ixx=Iyy=Izz=Ixy=Ixz=Iyz=0.;
    Double_t mtot=0;
#ifdef USEOPENMP
    if (n>=ompunbindnum) {
#pragma omp parallel default(shared) \
private(i,weight)
{
#pragma omp for schedule(dynamic) reduction(+:Ixx,Iyy,Izz,Ixy,Ixz,Iyz,mtot)
    for (i = 0; i < n; i++)
    {
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        Ixx+=(p[i].X()*p[i].X())*weight;
        Iyy+=(p[i].Y()*p[i].Y())*weight;
        Izz+=(p[i].Z()*p[i].Z())*weight;
        Ixy+=(p[i].X()*p[i].Y())*weight;
        Ixz+=(p[i].X()*p[i].Z())*weight;
        Iyz+=(p[i].Y()*p[i].Z())*weight;
        mtot+=weight;
    }
}
    I(0,0)=Ixx;I(1,1)=Iyy;I(2,2)=Izz;
    I(0,1)=I(1,0)=Ixy;
    I(0,2)=I(2,0)=Ixz;
    I(1,2)=I(2,1)=Iyz;
    }
    else {
#endif
    for (i = 0; i < n; i++)
    {
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        for (int j = 0; j < 3; j++)
        {
            for (int k = 0; k < 3; k++)
            {
                I(j, k) += (p[i].GetPosition(j) *p[i].GetPosition(k))*weight;
            }
        }
        mtot+=weight;
    }
#ifdef USEOPENMP
    }
#endif
    //det=I.Det();
    //for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) I(j, k) /= det;
    I=I*(1.0/mtot);
    e = I.Eigenvalues();
    a=e[0];b=e[1];c=e[2];
    eigenvec=I.Eigenvectors(e);
    I=I*mtot;
}

///calculate the velocity dispersion tensor
void CalcVelSigmaTensor(const Int_t n, Particle *p, Double_t &a, Double_t &b, Double_t &c, Matrix& eigenvec, Matrix &I, int itype)
{
    Double_t r2,det,Ixx,Iyy,Izz,Ixy,Ixz,Iyz, weight;
    Coordinate e;
    I=Matrix(0.);
    Int_t i;
    Ixx=Iyy=Izz=Ixy=Ixz=Iyz=0.;
    Double_t mtot=0;
#ifdef USEOPENMP
    if (n>=ompunbindnum) {
#pragma omp parallel default(shared) \
private(i,weight)
{
#pragma omp for schedule(dynamic) reduction(+:Ixx,Iyy,Izz,Ixy,Ixz,Iyz,mtot)
    for (i = 0; i < n; i++)
    {
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        //r2=p[i].Vx()*p[i].Vx()+p[i].Vy()*p[i].Vy()+p[i].Vz()*p[i].Vz();
        Ixx+=(p[i].Vx()*p[i].Vx())*weight;
        Iyy+=(p[i].Vy()*p[i].Vy())*weight;
        Izz+=(p[i].Vz()*p[i].Vz())*weight;
        Ixy+=(p[i].Vx()*p[i].Vy())*weight;
        Ixz+=(p[i].Vx()*p[i].Vz())*weight;
        Iyz+=(p[i].Vy()*p[i].Vz())*weight;
        mtot+=weight;
    }
}
    I(0,0)=Ixx;I(1,1)=Iyy;I(2,2)=Izz;
    I(0,1)=I(1,0)=Ixy;
    I(0,2)=I(2,0)=Ixz;
    I(1,2)=I(2,1)=Iyz;
    }
    else {
#endif
    for (i = 0; i < n; i++)
    {
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        for (int j = 0; j < 3; j++)
        {
            for (int k = 0; k < 3; k++)
            {
                I(j, k) += (p[i].GetVelocity(j) *p[i].GetVelocity(k))*weight;
            }
        }
        mtot+=weight;
    }
#ifdef USEOPENMP
    }
#endif
    //det=I.Det();
    //for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) I(j, k) /= det;
    I=I*(1.0/mtot);
    e = I.Eigenvalues();
    a=e[0];b=e[1];c=e[2];
    eigenvec=I.Eigenvectors(e);
    I=I*mtot;
}

///calculate the phase-space dispersion tensor
void CalcPhaseSigmaTensor(const Int_t n, Particle *p, GMatrix &eigenvalues, GMatrix& eigenvec, GMatrix &I, int itype)
{
    CalcPhaseSigmaTensor(n, p,  I, itype);
    I.Eigenvalvec(eigenvalues, eigenvec);
}

void CalcPhaseSigmaTensor(const Int_t n, Particle *p, GMatrix &I, int itype) {
    Double_t det,weight;
    Double_t Ixx,Iyy,Izz,Ixy,Ixz,Iyz;
    Double_t Ivxvx,Ivyvy,Ivzvz,Ivxvy,Ivxvz,Ivyvz;
    Double_t Ixvx,Iyvx,Izvx,Ixvy,Iyvy,Izvy,Ixvz,Iyvz,Izvz;
    I=GMatrix(6,6);
    Int_t i;
    Ixx=Iyy=Izz=Ixy=Ixz=Iyz=0.;
    Ivxvx=Ivyvy=Ivzvz=Ivxvy=Ivxvz=Ivyvz=0.;
    Ixvx=Iyvx=Izvx=Ixvy=Iyvy=Izvy=Ixvz=Iyvz=Izvz=0;
    Double_t mtot=0;
#ifdef USEOPENMP
    if (n>=ompunbindnum) {
#pragma omp parallel default(shared) \
private(i,weight)
{
#pragma omp for schedule(dynamic) reduction(+:Ixx,Iyy,Izz,Ixy,Ixz,Iyz,Ivxvx,Ivyvy,Ivzvz,Ivxvy,Ivxvz,Ivyvz,Ixvx,Iyvx,Izvx,Ixvy,Iyvy,Izvy,Ixvz,Iyvz,Izvz,mtot)
    for (i = 0; i < n; i++)
    {
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        Ixx+=(p[i].X()*p[i].X())*weight;
        Iyy+=(p[i].Y()*p[i].Y())*weight;
        Izz+=(p[i].Z()*p[i].Z())*weight;
        Ixy+=(p[i].X()*p[i].Y())*weight;
        Ixz+=(p[i].X()*p[i].Z())*weight;
        Iyz+=(p[i].Y()*p[i].Z())*weight;
        Ivxvx+=(p[i].Vx()*p[i].Vx())*weight;
        Ivyvy+=(p[i].Vy()*p[i].Vy())*weight;
        Ivzvz+=(p[i].Vz()*p[i].Vz())*weight;
        Ivxvy+=(p[i].Vx()*p[i].Vy())*weight;
        Ivxvz+=(p[i].Vx()*p[i].Vz())*weight;
        Ivyvz+=(p[i].Vy()*p[i].Vz())*weight;

        Ixvx+=(p[i].X()*p[i].Vx())*weight;
        Iyvx+=(p[i].Y()*p[i].Vx())*weight;
        Izvx+=(p[i].Z()*p[i].Vx())*weight;
        Ixvy+=(p[i].X()*p[i].Vy())*weight;
        Iyvy+=(p[i].Y()*p[i].Vy())*weight;
        Izvy+=(p[i].Z()*p[i].Vy())*weight;
        Ixvz+=(p[i].X()*p[i].Vz())*weight;
        Iyvz+=(p[i].Y()*p[i].Vz())*weight;
        Izvz+=(p[i].Z()*p[i].Vz())*weight;

        mtot+=weight;
    }
}
    I(0,0)=Ixx;I(1,1)=Iyy;I(2,2)=Izz;
    I(0,1)=I(1,0)=Ixy;
    I(0,2)=I(2,0)=Ixz;
    I(1,2)=I(2,1)=Iyz;

    I(3,3)=Ivxvx;I(4,4)=Ivyvy;I(5,5)=Ivzvz;
    I(3,4)=I(4,3)=Ivxvy;
    I(3,5)=I(5,3)=Ivxvz;
    I(4,5)=I(5,4)=Ivyvz;

    I(0,3)=I(3,0)=Ixvx;
    I(1,3)=I(3,1)=Iyvx;
    I(2,3)=I(3,2)=Izvx;
    I(0,4)=I(4,0)=Ixvy;
    I(1,4)=I(4,1)=Iyvy;
    I(2,4)=I(4,2)=Izvy;
    I(0,5)=I(5,0)=Ixvz;
    I(1,5)=I(5,1)=Iyvz;
    I(2,5)=I(5,2)=Izvz;

    }
    else {
#endif
    for (i = 0; i < n; i++)
    {
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        for (int j = 0; j < 6; j++)
        {
            for (int k = 0; k < 6; k++)
            {
                I(j, k) += (p[i].GetPhase(j) *p[i].GetPhase(k))*weight;
            }
        }
        mtot+=weight;
    }
#ifdef USEOPENMP
    }
#endif
    I=I*(1.0/mtot);
}

///calculate the weighted reduced inertia tensor assuming particles are the same mass
void CalcMTensor(Matrix& M, const Double_t q, const Double_t s, const Int_t n, Particle *p, int itype)
{
    Int_t i;
    int j,k;
    Double_t a2,Mxx,Myy,Mzz,Mxy,Mxz,Myz,weight;
    Mxx=Myy=Mzz=Mxy=Mxz=Myz=0.;
#ifdef USEOPENMP
    if (n>=ompunbindnum) {
#pragma omp parallel default(shared) \
private(i,a2,weight)
{
#pragma omp for schedule(dynamic) nowait reduction(+:Mxx,Myy,Mzz,Mxy,Mxz,Myz)
    for (i = 0; i < n; i++)
    {
        a2 = (p[i].X()*p[i].X()+p[i].Y()*p[i].Y()/q/q+p[i].Z()*p[i].Z()/s/s);
        if (a2!=0) {
        if (itype==-1) weight=1.0;
        else if (p[i].GetType()==itype) weight=1.0;
        else weight=0.;
        a2=1.0/a2*weight;
        Mxx+=p[i].X()*p[i].X()*a2;
        Myy+=p[i].Y()*p[i].Y()*a2;
        Mzz+=p[i].Z()*p[i].Z()*a2;
        Mxy+=p[i].X()*p[i].Y()*a2;
        Mxz+=p[i].X()*p[i].Z()*a2;
        Myz+=p[i].Y()*p[i].Z()*a2;
        }
    }
}
    M(0,0)=Mxx;M(1,1)=Myy;M(2,2)=Mzz;
    M(0,1)=M(1,0)=Mxy;
    M(0,2)=M(2,0)=Mxz;
    M(1,2)=M(2,1)=Myz;
    }
    else {
#endif
    for (i = 0; i < n; i++)
    {
        a2 = (p[i].X()*p[i].X()+p[i].Y()*p[i].Y()/q/q+p[i].Z()*p[i].Z()/s/s);
        if(a2!=0){
            if (itype==-1) weight=1.0;
            else if (p[i].GetType()==itype) weight=1.0;
            else weight=0.;
            a2=1.0/a2*weight;
            for (j = 0; j < 3; j++)
                for (k = 0; k < 3; k++)
                M(j, k) += p[i].GetPosition(j)*p[i].GetPosition(k)*a2;
        }
    }
#ifdef USEOPENMP
    }
#endif
}

///calculate the weighted reduced inertia tensor
void CalcMTensorWithMass(Matrix& M, const Double_t q, const Double_t s, const Int_t n, Particle *p, int itype)
{
    Int_t i;
    int j,k;
    Double_t a2,Mxx,Myy,Mzz,Mxy,Mxz,Myz,weight;
    Mxx=Myy=Mzz=Mxy=Mxz=Myz=0.;
#ifdef USEOPENMP
    if (n>=ompunbindnum) {
#pragma omp parallel default(shared) \
private(i,a2,weight)
{
#pragma omp for schedule(dynamic) nowait reduction(+:Mxx,Myy,Mzz,Mxy,Mxz,Myz)
    for (i = 0; i < n; i++)
    {
        a2 = (p[i].X()*p[i].X()+p[i].Y()*p[i].Y()/q/q+p[i].Z()*p[i].Z()/s/s);
        if (a2!=0) {
        if (itype==-1) weight=1.0;
        else if (p[i].GetType()==itype) weight=1.0;
        else weight=0.;
        a2=p[i].GetMass()/a2*weight;
        Mxx+=p[i].X()*p[i].X()*a2;
        Myy+=p[i].Y()*p[i].Y()*a2;
        Mzz+=p[i].Z()*p[i].Z()*a2;
        Mxy+=p[i].X()*p[i].Y()*a2;
        Mxz+=p[i].X()*p[i].Z()*a2;
        Myz+=p[i].Y()*p[i].Z()*a2;
        }
    }
}
    M(0,0)=Mxx;M(1,1)=Myy;M(2,2)=Mzz;
    M(0,1)=M(1,0)=Mxy;
    M(0,2)=M(2,0)=Mxz;
    M(1,2)=M(2,1)=Myz;
    }
    else {
#endif
    for (i = 0; i < n; i++)
    {
        a2 = (p[i].X()*p[i].X()+p[i].Y()*p[i].Y()/q/q+p[i].Z()*p[i].Z()/s/s);
        if(a2!=0){
        if (itype==-1) weight=1.0;
        else if (p[i].GetType()==itype) weight=1.0;
        else weight=0.;
            a2=p[i].GetMass()/a2*weight;
            for (j = 0; j < 3; j++)
                for (k = 0; k < 3; k++)
                M(j, k) += p[i].GetPosition(j)*p[i].GetPosition(k)*a2;
        }
    }
#ifdef USEOPENMP
    }
#endif
}

///rotate particles
void RotParticles(const Int_t n, Particle *p, Matrix &R)
{
    Int_t i;
    int j;
    Coordinate temp;
#ifdef USEOPENMP
    if (n>=ompunbindnum) {
#pragma omp parallel default(shared) \
private(i,j,temp)
{
#pragma omp for schedule(dynamic) nowait
    for (i=0; i<n; i++)
    {
        temp[0]=temp[1]=temp[2]=0.;
        for (j=0; j<3; j++)
        {
            temp[0] += R(0, j) * (p[i].GetPosition(j));
            temp[1] += R(1, j) * (p[i].GetPosition(j));
            temp[2] += R(2, j) * (p[i].GetPosition(j));
        }
        p[i].SetPosition(temp[0], temp[1], temp[2]);
    }
}
    }
    else {
#endif
    for (i=0; i<n; i++)
    {
        temp[0]=temp[1]=temp[2]=0.;
        for (j=0; j<3; j++)
        {
            temp[0] += R(0, j) * (p[i].GetPosition(j));
            temp[1] += R(1, j) * (p[i].GetPosition(j));
            temp[2] += R(2, j) * (p[i].GetPosition(j));
        }
        p[i].SetPosition(temp[0], temp[1], temp[2]);
    }
#ifdef USEOPENMP
    }
#endif
}

///calculate the phase-space dispersion tensor
GMatrix CalcPhaseCM(const Int_t n, Particle *p, int itype)
{
    Double_t det,weight;
    Double_t cmx,cmy,cmz,cmvx,cmvy,cmvz;
    GMatrix cm(6,1);
    Int_t i;
    cmx=cmy=cmz=cmvx=cmvy=cmvz=0;
    Double_t mtot=0;
#ifdef USEOPENMP
    if (n>=ompunbindnum) {
#pragma omp parallel default(shared) \
private(i,weight)
{
#pragma omp for schedule(dynamic) reduction(+:cmx,cmy,cmz,cmvx,cmvy,cmvz,mtot)
    for (i = 0; i < n; i++)
    {
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        cmx+=p[i].X()*weight;
        cmy+=p[i].Y()*weight;
        cmz+=p[i].Z()*weight;
        cmvx+=p[i].Vx()*weight;
        cmvy+=p[i].Vy()*weight;
        cmvz+=p[i].Vz()*weight;

        mtot+=weight;
    }
}
    cm(0,0)=cmx;
    cm(1,0)=cmy;
    cm(2,0)=cmz;
    cm(3,0)=cmvx;
    cm(4,0)=cmvy;
    cm(5,0)=cmvz;
    }
    else {
#endif
    for (i = 0; i < n; i++)
    {
        if (itype==-1) weight=p[i].GetMass();
        else if (p[i].GetType()==itype) weight=p[i].GetMass();
        else weight=0.;
        for (int j = 0; j < 6; j++) cm(j, 0) += p[i].GetPhase(j)*weight;
        mtot+=weight;
    }
#ifdef USEOPENMP
    }
#endif
    cm=cm*(1.0/mtot);
    return cm;
}

///calculate concentration. Note that we limit concentration to 1000 or so which means VmaxVvir2<=36
void GetConcentration(PropData &p)
{

    int status;
    int iter = 0, max_iter = 100;
    const gsl_root_fsolver_type *T;
    gsl_root_fsolver *s;
    double cval = 2.3;
    double VmaxVvir2= p.VmaxVvir2;
    //start point for concentration
    double x_lo = 1.9, x_hi = 1000.0;
    gsl_function F;
    if (p.VmaxVvir2<=36) {
    F.function = &mycNFW;
    F.params = &VmaxVvir2;
    T = gsl_root_fsolver_brent;
    s = gsl_root_fsolver_alloc (T);
    gsl_root_fsolver_set (s, &F, x_lo, x_hi);
    do
    {
        iter++;
        status = gsl_root_fsolver_iterate (s);
        cval = gsl_root_fsolver_root (s);
        x_lo = gsl_root_fsolver_x_lower (s);
        x_hi = gsl_root_fsolver_x_upper (s);
        status = gsl_root_test_interval (x_lo, x_hi,1.0/sqrt((double)p.num),1.0/sqrt((double)p.num));
    }
    while (status == GSL_CONTINUE && iter < max_iter);
    gsl_root_fsolver_free (s);
    p.cNFW=cval;
    }
    else {
        p.cNFW=p.gR200c/p.gRmaxvel;
    }
}

//@}

///\name Routines for manipulation of property data
//@{
///copy mass information over
void CopyMasses(const Int_t nhalos, PropData *&pold, PropData *&pnew){
    for (Int_t i=1;i<=nhalos;i++) {
        pnew[i].gNFOF=pold[i].gNFOF;
        pnew[i].gMFOF=pold[i].gMFOF;
        pnew[i].gMvir=pold[i].gMvir;
        pnew[i].gRvir=pold[i].gRvir;
        pnew[i].gM200c=pold[i].gM200c;
        pnew[i].gR200c=pold[i].gR200c;
        pnew[i].gM200m=pold[i].gM200m;
        pnew[i].gR200m=pold[i].gR200m;
        pnew[i].gMBN98=pold[i].gMBN98;
        pnew[i].gRBN98=pold[i].gRBN98;
        pnew[i].gRhalfmass=pold[i].gRhalfmass;
    }
}
///reorder mass information stored in properties data
void ReorderInclusiveMasses(const Int_t &numgroups, const Int_t &newnumgroups, Int_t *&numingroup, PropData *&pdata)
{
    PropData *pnew=new PropData[newnumgroups+1];
    PriorityQueue *pq=new PriorityQueue(newnumgroups);
    for (Int_t i = 1; i <=numgroups; i++) if (numingroup[i]>0) pq->Push(i, numingroup[i]);
    for (Int_t i = 1; i<=newnumgroups; i++) {
        Int_t groupid=pq->TopQueue(),size=pq->TopPriority();pq->Pop();
        pnew[i]=pdata[groupid];
    }
    delete pq;
    for (Int_t i = 1; i<=newnumgroups; i++) pdata[i]=pnew[i];
    delete[] pnew;
}
//@}

///\name Routines related to calculating energy of groups and sorting of particles
//@{
/*!
    Calculate the potential energy and kinetic energy relative to the velocity frame stored in gcmvel. Note that typically this is the velocity of particles within
    the inner region used to determine the centre-of-mass. BUT of course, this frame is not without its flaws, as in a chaotic mergering system, one might not be able
    to disentangle structures and the centre-of-mass need not be located at the "centre" or dense point of any of the merging structures.
    Once the energy is calculated, the total energy is stored in potential, that way it is easy to sort particles according to their binding energy.

    The overall structure of the code is a bit lengthy simple to break up calculations appropriately for OMP style parallization.
    For small groups it is more efficient to parallize across groups, whereas for large groups containing many particles, we loop over the particles
    to sum quantities.

    \todo might alter binding energy to use the velocity around the particle at the deepest point in the potential.
 */
void GetBindingEnergy(Options &opt, const Int_t nbodies, Particle *Part, Int_t ngroup, Int_t *&pfof, Int_t *&numingroup, PropData *&pdata, Int_t *&noffset)
{
#ifndef USEMPI
    int ThisTask=0;
#endif
    if (opt.iverbose) cout<<ThisTask<<" Get Energy"<<endl;
    if (opt.uinfo.cmvelreftype==POTREF && opt.iverbose==1) cout<<"Using minimum potential reference"<<endl;

    //used to access current particle
    Particle *Pval;
    Int_t i,j,k;
    //store eps2 for plummer softening to cut down number of floating point operations
    //Note could use more complicated b-spline but at least here since not evolving dynamics, plummer potential not an issue.
    Double_t eps2=opt.uinfo.eps*opt.uinfo.eps;

    //useful variables to store temporary results
    Double_t r2,v2,Ti,poti,pot;
    Double_t Tval,Potval,Efracval,Eval,Emostbound,Eunbound,imostbound,iunbound;
    Double_t Efracval_gas,Efracval_star;
    Double_t mw2=opt.MassValue*opt.MassValue;
    Double_t potmin,menc;
    Int_t npot,ipotmin;

    //used to temporarily store pids. Needed for large groups as the tree code used to calculate potential overwrites the id of particles so that once
    //finished it puts the particles back into the input order. Therefore store id values in PID  value (which can be over written)
    //also if wish to use the deepest potential as a reference, then used to store original order
    Int_t *storepid;
    if (opt.uinfo.icalculatepotential) {
    //small groups with PP calculations of potential.
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,r2,v2,poti,Ti,pot,Eval,npot,storepid,menc,potmin,ipotmin)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
    for (i=1;i<=ngroup;i++) if (numingroup[i]<ompunbindnum) {
        for (j=0;j<numingroup[i];j++) {
            for (k=j+1;k<numingroup[i];k++) {
                r2=0.;for (int n=0;n<3;n++) r2+=pow(Part[j+noffset[i]].GetPosition(n)-Part[k+noffset[i]].GetPosition(n),2.0);
                r2+=eps2;
                r2=1.0/sqrt(r2);
#ifdef NOMASS
                pot=-opt.G*r2*mw2;
#else
                pot=-opt.G*(Part[j+noffset[i]].GetMass()*Part[k+noffset[i]].GetMass())*r2;
#endif
                pdata[i].Pot+=pot;
                poti=Part[j+noffset[i]].GetPotential()+pot;Part[j+noffset[i]].SetPotential(poti);
                poti=Part[k+noffset[i]].GetPotential()+pot;Part[k+noffset[i]].SetPotential(poti);
            }
        }
    }
#ifdef USEOPENMP
}
#endif
    }//end of if calculate potential
#ifdef SWIFTINTERFACE
    else {
        for (i=1;i<=ngroup;i++) if (numingroup[i]<ompunbindnum) for (j=0;j<numingroup[i];j++) Part[j+noffset[i]].SetPotential(Part[j+noffset[i]].GetGravityPotential());
    }
#endif

        //once potential is calculated, iff using velocity around deepest potential well NOT cm
    if (opt.uinfo.cmvelreftype==POTREF) {
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,r2,v2,poti,Ti,pot,Eval,npot,storepid,menc,potmin,ipotmin)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
        for (i=1;i<=ngroup;i++) if (numingroup[i]<ompunbindnum) {
            //determine how many particles to use
            npot=max(opt.uinfo.Npotref,Int_t(opt.uinfo.fracpotref*numingroup[i]));
            //store old ordering
            storepid=new Int_t[numingroup[i]];
            for (j=0;j<numingroup[i];j++) {storepid[j]=Part[j+noffset[i]].GetPID();Part[j+noffset[i]].SetPID(j);}
            //determine position of minimum potential and by radius around this position
            potmin=Part[noffset[i]].GetPotential();ipotmin=0;
            for (j=0;j<numingroup[i];j++) if (Part[j+noffset[i]].GetPotential()<potmin) {potmin=Part[j+noffset[i]].GetPotential();ipotmin=j;}
            for (k=0;k<3;k++) pdata[i].gcm[k]=Part[ipotmin+noffset[i]].GetPosition(k);
            for (j=0;j<numingroup[i];j++) {
                for (k=0;k<3;k++) Part[j+noffset[i]].SetPosition(k,Part[j+noffset[i]].GetPosition(k)-pdata[i].gcm[k]);
            }
            gsl_heapsort(&Part[noffset[i]],numingroup[i],sizeof(Particle),RadCompare);
            //now determine kinetic frame
            pdata[i].gcmvel[0]=pdata[i].gcmvel[1]=pdata[i].gcmvel[2]=menc=0.;
            for (j=0;j<npot;j++) {
                for (k=0;k<3;k++) pdata[i].gcmvel[k]+=Part[j+noffset[i]].GetVelocity(k)*Part[j+noffset[i]].GetMass();
                menc+=Part[j+noffset[i]].GetMass();
            }
            for (j=0;j<3;j++) {pdata[i].gcmvel[j]/=menc;}
            //and restore order and original frame
            gsl_heapsort(&Part[noffset[i]],numingroup[i],sizeof(Particle),PIDCompare);
            for (j=0;j<numingroup[i];j++) Part[j+noffset[i]].SetPID(storepid[j]);
            delete[] storepid;
            for (j=0;j<numingroup[i];j++) {
                for (k=0;k<3;k++) Part[j+noffset[i]].SetPosition(k,Part[j+noffset[i]].GetPosition(k)+pdata[i].gcm[k]);
            }
        }
#ifdef USEOPENMP
}
#endif
    }

    //then calculate binding energy and store in potential
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,r2,v2,poti,Ti,pot,Eval,npot,storepid)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
    for (i=1;i<=ngroup;i++) if (numingroup[i]<ompunbindnum) {
        for (j=0;j<numingroup[i];j++) {
            v2=0.;for (int n=0;n<3;n++) v2+=pow(Part[j+noffset[i]].GetVelocity(n)-pdata[i].gcmvel[n],2.0);
#ifdef NOMASS
            Ti=0.5*v2*opt.MassValue;
#ifdef GASON
            Ti+=opt.MassValue*Part[j+noffset[i]].GetU();
#endif
#else
            Ti=0.5*Part[j+noffset[i]].GetMass()*v2;
#ifdef GASON
            Ti+=Part[j+noffset[i]].GetMass()*Part[j+noffset[i]].GetU();
#endif
#endif
            pdata[i].T+=Ti;
#ifdef NOMASS
            Part[j+noffset[i]].SetPotential(Ti+Part[j+noffset[i]].GetPotential()*mw2);
#else
            Part[j+noffset[i]].SetPotential(Ti+Part[j+noffset[i]].GetPotential());
#endif
            if(Part[j+noffset[i]].GetPotential()<0) pdata[i].Efrac+=1.0;
#ifdef GASON
            if(Part[j+noffset[i]].GetPotential()<0&&Part[j+noffset[i]].GetType()==GASTYPE) pdata[i].Efrac_gas+=1.0;
#endif
#ifdef STARON
            if(Part[j+noffset[i]].GetPotential()<0&&Part[j+noffset[i]].GetType()==STARTYPE) pdata[i].Efrac_star+=1.0;
#endif
        }
        pdata[i].Efrac/=(Double_t)numingroup[i];
#ifdef GASON
        if (pdata[i].n_gas>0)pdata[i].Efrac_gas/=(Double_t)pdata[i].n_gas;
#endif
#ifdef STARON
        if (pdata[i].n_star>0)pdata[i].Efrac_star/=(Double_t)pdata[i].n_star;
#endif
    }
#ifdef USEOPENMP
}
#endif

    if (opt.uinfo.icalculatepotential) {
    //loop for large groups with tree calculation
    for (i=1;i<=ngroup;i++) if (numingroup[i]>=ompunbindnum) {
        storepid=new Int_t[numingroup[i]];
        for (j=0;j<numingroup[i];j++) {
            storepid[j]=Part[noffset[i]+j].GetPID();
            Part[noffset[i]+j].SetPID(Part[noffset[i]+j].GetID());
        }
        //calculate potential
        Potential(opt,numingroup[i],&Part[noffset[i]]);
        for (j=0;j<numingroup[i];j++) {
            Part[noffset[i]+j].SetID(Part[noffset[i]+j].GetPID());
            Part[noffset[i]+j].SetPID(storepid[j]);
        }
        delete[] storepid;
    }
    }//end of if calculate potential
    #ifdef SWIFTINTERFACE
        else {
            for (i=1;i<=ngroup;i++) if (numingroup[i]>=ompunbindnum) for (j=0;j<numingroup[i];j++) Part[j+noffset[i]].SetPotential(Part[j+noffset[i]].GetGravityPotential());
        }
    #endif

    //if using POTREF, most computations involve sorts, so parallize over groups
    if (opt.uinfo.cmvelreftype==POTREF) {
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j,k,r2,v2,poti,Ti,pot,Eval,npot,storepid,menc,potmin,ipotmin)
{
    #pragma omp for schedule(dynamic,1) nowait
#endif
        for (i=1;i<=ngroup;i++) if (numingroup[i]>=ompunbindnum) {
            //once potential is calculated, iff using NOT cm but velocity around deepest potential well
            //determine how many particles to use
            npot=max(opt.uinfo.Npotref,Int_t(opt.uinfo.fracpotref*numingroup[i]));
            //store old ordering
            storepid=new Int_t[numingroup[i]];
            for (j=0;j<numingroup[i];j++) {storepid[j]=Part[j+noffset[i]].GetPID();Part[j+noffset[i]].SetPID(j);}
            //determine position of minimum potential and by radius around this position
            potmin=Part[noffset[i]].GetPotential();ipotmin=0;
            for (j=0;j<numingroup[i];j++) if (Part[j+noffset[i]].GetPotential()<potmin) {potmin=Part[j+noffset[i]].GetPotential();ipotmin=j;}
            for (k=0;k<3;k++) pdata[i].gcm[k]=Part[ipotmin+noffset[i]].GetPosition(k);
            for (j=0;j<numingroup[i];j++) {
                for (k=0;k<3;k++) Part[j+noffset[i]].SetPosition(k,Part[j+noffset[i]].GetPosition(k)-pdata[i].gcm[k]);
            }
            gsl_heapsort(&Part[noffset[i]],numingroup[i],sizeof(Particle),RadCompare);
            //now determine kinetic frame
            pdata[i].gcmvel[0]=pdata[i].gcmvel[1]=pdata[i].gcmvel[2]=menc=0.;
            for (j=0;j<npot;j++) {
                for (k=0;k<3;k++) pdata[i].gcmvel[k]+=Part[j+noffset[i]].GetVelocity(k)*Part[j+noffset[i]].GetMass();
                menc+=Part[j+noffset[i]].GetMass();
            }
            for (j=0;j<3;j++) {pdata[i].gcmvel[j]/=menc;}
            //and restore order and original frame
            gsl_heapsort(&Part[noffset[i]],numingroup[i],sizeof(Particle),PIDCompare);
            for (j=0;j<numingroup[i];j++) Part[j+noffset[i]].SetPID(storepid[j]);
            delete[] storepid;
            for (j=0;j<numingroup[i];j++) {
                for (k=0;k<3;k++) Part[j+noffset[i]].SetPosition(k,Part[j+noffset[i]].GetPosition(k)+pdata[i].gcm[k]);
            }
        }
#ifdef USEOPENMP
}
#endif
    }

    //finally calculate binding energy
    for (i=1;i<=ngroup;i++) if (numingroup[i]>=ompunbindnum) {
        Tval=0;Potval=0;Efracval=0;
#ifdef GASON
        Efracval_gas=0.;
#endif
#ifdef STARON
        Efracval_star=0.;
#endif
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(j,v2,Ti)
{
    #pragma omp for reduction(+:Tval,Efracval,Potval,Efracval_gas,Efracval_star)
#endif
        for (j=0;j<numingroup[i];j++) {
            v2=0.;for (int n=0;n<3;n++) v2+=pow(Part[j+noffset[i]].GetVelocity(n)-pdata[i].gcmvel[n],2.0);
#ifdef NOMASS
            Ti=0.5*v2*opt.MassValue;
#ifdef GASON
            Ti+=opt.MassValue*Part[j+noffset[i]].GetU();
#endif
            Potval+=Part[j+noffset[i]].GetPotential()*mw2;
            Part[j+noffset[i]].SetPotential(Part[j+noffset[i]].GetPotential()*mw2+Ti);
#else
            Ti=0.5*Part[j+noffset[i]].GetMass()*v2;
#ifdef GASON
            Ti+=Part[j+noffset[i]].GetMass()*Part[j+noffset[i]].GetU();
#endif
            Potval+=Part[j+noffset[i]].GetPotential();
            Part[j+noffset[i]].SetPotential(Part[j+noffset[i]].GetPotential()+Ti);
#endif
            Tval+=Ti;
            if(Part[j+noffset[i]].GetPotential()<0.0) Efracval+=1.0;
#ifdef GASON
            if(Part[j+noffset[i]].GetPotential()<0&&Part[j+noffset[i]].GetType()==GASTYPE) Efracval_gas+=1.0;
#endif
#ifdef STARON
            if(Part[j+noffset[i]].GetPotential()<0&&Part[j+noffset[i]].GetType()==STARTYPE) Efracval_star+=1.0;
#endif
        }
#ifdef USEOPENMP
}
#endif
        pdata[i].T=Tval;pdata[i].Efrac=Efracval;pdata[i].Pot=Potval;
        pdata[i].Efrac/=(Double_t)numingroup[i];
#ifdef GASON
        if (pdata[i].n_gas>0)pdata[i].Efrac_gas=Efracval_gas/(Double_t)pdata[i].n_gas;
#endif
#ifdef STARON
        if (pdata[i].n_star>0)pdata[i].Efrac_star=Efracval_star/(Double_t)pdata[i].n_star;
#endif
    }

    if (opt.iverbose) cout<<"Done."<<endl;
}


/*!
    Sort particles according to their binding energy and return a double pointer of Int_t s.
    This code first sorts particles according to their (local mpi) group id and calculates center of mass and binding energy.
*/
Int_t **SortAccordingtoBindingEnergy(Options &opt, const Int_t nbodies, Particle *Part, Int_t ngroup, Int_t *&pfof, Int_t *numingroup, PropData *pdata, Int_t ioffset)
{
#ifndef USEMPI
    int ThisTask=0;
#endif
    cout<<ThisTask<<" Sort particles to compute properties"<<ngroup<<endl;
    Int_t i,j,k;
    Int_t *noffset=new Int_t[ngroup+1];
    Int_t *storepid;

    //sort the particle data according to their group id so that one can then sort particle data
    //of a group however one sees fit.
    storepid=new Int_t[nbodies];
    for (i=0;i<nbodies;i++) {
        storepid[i]=Part[i].GetPID();
        if (pfof[Part[i].GetID()]>ioffset) Part[i].SetPID(pfof[Part[i].GetID()]);
        else Part[i].SetPID(nbodies+1);//here move all particles not in groups to the back of the particle array
    }
    qsort(Part, nbodies, sizeof(Particle), PIDCompare);
    //sort(Part.begin(),Part.end(), PIDCompareVec);
    for (i=0;i<nbodies;i++) Part[i].SetPID(storepid[Part[i].GetID()]);
    delete[] storepid;

    noffset[0]=noffset[1]=0;
    for (i=2;i<=ngroup;i++) noffset[i]=noffset[i-1]+numingroup[i-1];

    // for small groups interate over groups using openmp threads
    // for large groups interate over particles
    for (i=1;i<=ngroup;i++) pdata[i].num=numingroup[i];
    cout<<ThisTask<<" Calculate properties"<<endl;
    GetCMProp(opt, nbodies, Part, ngroup, pfof, numingroup, pdata, noffset);
    GetBindingEnergy(opt, nbodies, Part, ngroup, pfof, numingroup, pdata, noffset);
    cout<<ThisTask<<" Sort particles by binding energy"<<endl;
    //sort by energy
#ifdef USEOPENMP
#pragma omp parallel default(shared)  \
private(i,j)
{
    #pragma omp for nowait
#endif
    for (i=1;i<=ngroup;i++) {
        qsort(&Part[noffset[i]], numingroup[i], sizeof(Particle), PotCompare);
        pdata[i].gpos=Coordinate(Part[noffset[i]].GetPosition());
        pdata[i].gvel=Coordinate(Part[noffset[i]].GetVelocity());
        pdata[i].ibound=Part[noffset[i]].GetPID();
        pdata[i].iunbound=numingroup[i];
        if (numingroup[i]>0)
            for (j=0;j<numingroup[i];j++) if(Part[noffset[i]+j].GetPotential()>0) {pdata[i].iunbound=j;break;}
        Double_t x,y,z,r2;
        for (j=1;j<numingroup[i];j++) {
            x=Part[noffset[i]+j].X()-Part[noffset[i]].X();
            y=Part[noffset[i]+j].Y()-Part[noffset[i]].Y();
            z=Part[noffset[i]+j].Z()-Part[noffset[i]].Z();
            r2=x*x+y*y+z*z;
            if(pdata[i].gRmbp<r2) pdata[i].gRmbp=r2;
        }
        pdata[i].gRmbp=sqrt(pdata[i].gRmbp);
    }
#ifdef USEOPENMP
}
#endif
    //before used to store the id in pglist and then have to reset particle order so that Ids correspond to indices
    //but to reduce computing time could just store index and leave particle array unchanged but only really necessary
    //if want to have separate field and subhalo files
    Int_t **pglist=new Int_t*[ngroup+1];
    for (i=1;i<=ngroup;i++){
        pglist[i]=new Int_t[numingroup[i]+1];//here store in very last position at n+1 the unbound particle point
        if (opt.iseparatefiles) for (j=0;j<numingroup[i];j++) pglist[i][j]=Part[j+noffset[i]].GetID();
        else for (j=0;j<numingroup[i];j++) pglist[i][j]=j+noffset[i];
        if (numingroup[i]>0) pglist[i][numingroup[i]]=pdata[i].iunbound;
        else pglist[i][0]=0;
    }
    delete[] noffset;
    //reset particles back to id order
    if (opt.iseparatefiles) {
    cout<<"Reset particles to original order"<<endl;
    qsort(Part, nbodies, sizeof(Particle), IDCompare);
    //sort(Part.begin(), Part.end(), IDCompareVec);
    }
    cout<<"Done"<<endl;
    return pglist;
}
/*
   Calculate Halo properties only, assumes that information in particle PIDs is meaningless, useful when don't care about particle tracking
   and just want halo catalogs (like when analysing results from runs like PICOLA (or say 2LPT runs))
*/
void CalculateHaloProperties(Options &opt, const Int_t nbodies, Particle *Part, Int_t ngroup, Int_t *&pfof, Int_t *numingroup, PropData *pdata)
{
#ifndef USEMPI
    int ThisTask=0;
#endif
    Int_t i,j,k;
    Int_t *noffset=new Int_t[ngroup+1];

    //sort the particle data according to their group id so that one can then sort particle data
    //of a group however one sees fit.
    for (i=0;i<nbodies;i++) {
        if (pfof[Part[i].GetID()]>0) Part[i].SetPID(pfof[Part[i].GetID()]);
        else Part[i].SetPID(nbodies+1);//here move all particles not in groups to the back of the particle array
    }
    qsort(Part, nbodies, sizeof(Particle), PIDCompare);

    noffset[0]=noffset[1]=0;
    for (i=2;i<=ngroup;i++) noffset[i]=noffset[i-1]+numingroup[i-1];

    // for small groups interate over groups using openmp threads
    // for large groups interate over particles
    for (i=1;i<=ngroup;i++) pdata[i].num=numingroup[i];
    cout<<ThisTask<<" Calculate properties"<<endl;
    GetCMProp(opt, nbodies, Part, ngroup, pfof, numingroup, pdata, noffset);
    GetBindingEnergy(opt, nbodies, Part, ngroup, pfof, numingroup, pdata, noffset);
    for (i=1;i<=ngroup;i++) pdata[i].ibound=Part[noffset[i]].GetPID();
    delete[] noffset;
}
//@}

///\name Routines to get hierarhcy information
//@{
///Get total number of (sub)substructures in a (sub)structure
Int_t *GetSubstrutcureNum(Int_t ngroups)
{
    Int_t nhierarchy=1;
    StrucLevelData *ppsldata,**papsldata;
    Int_t *nsub=new Int_t[ngroups+1];
    ppsldata=psldata;
    while (ppsldata->nextlevel!=NULL){nhierarchy++;ppsldata=ppsldata->nextlevel;}
    for (Int_t i=1;i<=ngroups;i++) nsub[i]=0;
    ppsldata=psldata;
    papsldata=new StrucLevelData*[nhierarchy];
    nhierarchy=0;
    while (ppsldata!=NULL) {papsldata[nhierarchy++]=ppsldata;ppsldata=ppsldata->nextlevel;}
    for (int i=nhierarchy-1;i>=1;i--){
        //store number of substructures in level below
        for (int j=0;j<papsldata[i]->nsinlevel;j++) nsub[*(papsldata[i]->gidparenthead[j])]++;
        //then add all lower level substructures
        for (int j=0;j<papsldata[i]->nsinlevel;j++) nsub[*(papsldata[i]->gidparenthead[j])]+=nsub[*(papsldata[i]->gidhead[j])];
    }
    return nsub;
}

///Get parent structure id of substructures
///Here group ids are MPI local, that is they have not been offset to the global group id value
Int_t *GetParentID(Int_t ngroups)
{
    //initialize number of levels
    Int_t nhierarchy=1;
    //store start point of hierarchy pointer
    StrucLevelData *ppsldata,**papsldata;
    Int_t *parentgid=new Int_t[ngroups+1];
    ppsldata=psldata;
    while (ppsldata->nextlevel!=NULL){nhierarchy++;ppsldata=ppsldata->nextlevel;}
    for (Int_t i=1;i<=ngroups;i++) parentgid[i]=0;
    ppsldata=psldata;
    papsldata=new StrucLevelData*[nhierarchy];
    nhierarchy=0;
    while (ppsldata!=NULL) {papsldata[nhierarchy++]=ppsldata;ppsldata=ppsldata->nextlevel;}
    for (int i=nhierarchy-1;i>=1;i--){
        //store number of substructures in level below
        for (int j=0;j<papsldata[i]->nsinlevel;j++) parentgid[*(papsldata[i]->gidhead[j])]=*(papsldata[i]->gidparenthead[j]);
    }
    return parentgid;
}
//@}


///\name functions used to find root of concentration
//@{
double mycNFW(double c, void *params)
{
  double *p = (double*) params;
  Double_t VmaxVvir2=*p;
  return (VmaxVvir2) -0.216*c/(log(1.0+c)-c/(1.0+c));
}
double mycNFW_deriv(double c, void *params)
{
  double *p = (double*) params;
  Double_t VvirVmax2=*p;
  return 0.216*c/pow((1.0+c),2.0);
}
double mycNFW_fdf(double c, void *params, double *y, double *dy)
{
  double *p = (double*) params;
  Double_t VmaxVvir2=*p;
  Double_t conec=c/(1.0+c);
  *y=(VmaxVvir2)-0.216*c/(log(1.0+c)-conec);
  *dy=0.216*conec*conec/c;
}
//@}
