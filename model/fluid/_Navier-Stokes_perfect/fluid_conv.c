#include "fluid.h"
#include "fluid_conv.h"
#include <model/metrics/_metrics.h>
#include <model/_model.h>
#include <model/share/fluid_share.h>


/* MUSCLVARS1 extrapolate temperature
   MUSCLVARS2 extrapolate Pstar */
#define MUSCLVARS1 1
#define MUSCLVARS2 2
#define MUSCLVARS MUSCLVARS2


/* EIGENSET=1 yields the eigenvectors recommended by mathematica (positivity-preserving)  
   EIGENSET=2 yields the same eigenvalues and eigenvectors as those specified in "The Use of
   Domain Decomposition in Accelerating the Convergence of Quasi-Hyperbolic Systems, by Parent and Sislian,
   JCP, 2002. Guarantees no singular point in alpha or L but yields alpha=0 for some flux components (positivity preserving) Note: in 3D, there is a singular point in L
   EIGENSET=3 guarantees no singular point in alpha, R, or L, and yields all non-zero alphas (positivity-preserving) Note: in 3D, there is a singular point in L
   EIGENSET=11 is a mix of the eigenvectors of EIGENSET#1 and EIGENSET#2
              (only coded in 2D, and not positivity-preserving)
   EIGENSET=12 has singular point in alpha (division by Vstar) (not positivity-preserving, only coded in 2D) 
   EIGENSET=13 yields the same eigenvectors as EIGENSET=2 but with the first and second column interchanged
              (positivity-preserving, but only coded in 2D)
*/

/* When using eigenset3, rearrange metrics when a singularity occurs within the left eigenvector
   matrix. Only rearrange the metrics part of the left and right eigenvectors and leave the metrics
   of the eigenvalues as they are. */

#define EIGENSET 3


static void rearrange_metrics_EIGENSET_3(metrics_t *metrics){

#ifdef _3D
  double tol,X1,X2,X3,Xmax;
  long Xmaxindex;

  tol=5e-2;

  X1=metrics->X[0];
  X2=metrics->X[1];
  X3=metrics->X[2];
  
  Xmax=max(fabs(X1),max(fabs(X2),fabs(X3)));
  assert(Xmax!=0.0);
  Xmaxindex=0;
  if (fabs(X1)>=fabs(X2) && fabs(X1)>=fabs(X3)) Xmaxindex=0;
  if (fabs(X2)>=fabs(X1) && fabs(X2)>=fabs(X3)) Xmaxindex=1;
  if (fabs(X3)>=fabs(X1) && fabs(X3)>=fabs(X2)) Xmaxindex=2;
  if (fabs(X1+X2+X3)<Xmax*tol) {
     metrics->X[Xmaxindex]=metrics->X[Xmaxindex]*(1.0+tol-fabs(X1+X2+X3)/(Xmax));
  }
#endif
}



void find_Fstar(np_t np, gl_t *gl, long theta, flux_t Fstar){
  double Vstar,P,Omega;
  long dim;
  
  Vstar=_Vstar(np,theta);
  P=_P(np,gl);
  Omega=_Omega(np,gl);
  Fstar[0]=np.bs->U[0]*Vstar*Omega;
  for (dim=0; dim<nd; dim++)
    Fstar[1+dim]=Omega*np.bs->U[1+dim]*Vstar+Omega*_X(np,theta,dim)*P;
  Fstar[1+nd]=Omega*np.bs->U[1+nd]*Vstar+Omega*Vstar*P;
}


void find_Fstar_given_metrics(np_t np, gl_t *gl, metrics_t metrics, long theta, flux_t Fstar){
  double Vstar,P,Omega;
  long dim;
  
  Vstar=0.0e0;
  for (dim=0; dim<nd; dim++){
    Vstar+=metrics.X2[theta][dim]*_V(np,dim);
  }

  P=_P(np,gl);
  Omega=metrics.Omega;
  Fstar[0]=np.bs->U[0]*Vstar*Omega;
  for (dim=0; dim<nd; dim++)
    Fstar[1+dim]=Omega*np.bs->U[1+dim]*Vstar+Omega*metrics.X2[theta][dim]*P;
  Fstar[1+nd]=Omega*np.bs->U[1+nd]*Vstar+Omega*Vstar*P;
}



void find_dFstar_dUstar(np_t np, gl_t *gl, long theta, sqmat_t A){
  long dim;
  double g,q2,a2;
  double X1,u;
#ifdef _2DL
  double X2,v;
#endif
#ifdef _3D
  double X3,w;
#endif

  g=gl->model.fluid.gamma;
  q2=0.0;
  for (dim=0; dim<nd; dim++) q2+=sqr(_V(np,dim));
  a2=gl->model.fluid.gamma*gl->model.fluid.R*_T(np,gl);

#ifdef _2D
  u=_V(np,0);
  v=_V(np,1);
  X1=_X(np,theta,0);
  X2=_X(np,theta,1);

  A[0][0]=0.0;
  A[0][1]=X1;
  A[0][2]=X2;
  A[0][3]=0.0;

  A[1][0]=(g-3.0)*X1*u*u/2.0 - X2*u*v  + (g-1.0)*X1*v*v/2.0;
  A[1][1]=(3.0-g)*u*X1 + v*X2;
  A[1][2]=u*X2 - (g-1.0)*X1*v;
  A[1][3]=(g-1.0)*X1;

  A[2][0]=(g-3.0)*X2*v*v/2.0 - X1*u*v  + (g-1.0)*X2*u*u/2.0;
  A[2][1]=v*X1 - (g-1.0)*X2*u;
  A[2][2]=(3.0-g)*v*X2 + u*X1;
  A[2][3]=(g-1.0)*X2;

  A[3][0]=-(X1*u+X2*v)*(a2/(g-1.0) + (1.0-g/2.0)*(u*u+v*v));
  A[3][1]=(2.0*a2/(g-1.0) + v*v + (3.0-2.0*g)*u*u)*X1/2.0 + X2*(1.0-g)*u*v;
  A[3][2]=(2.0*a2/(g-1.0) + u*u + (3.0-2.0*g)*v*v)*X2/2.0 + X1*(1.0-g)*u*v;
  A[3][3]=g*(u*X1 + v*X2);

#endif

#ifdef _3D
  u=_V(np,0);
  v=_V(np,1);
  w=_V(np,2);
  X1=_X(np,theta,0);
  X2=_X(np,theta,1);
  X3=_X(np,theta,2);

  A[0][0]=0.0;
  A[0][1]=X1; 
  A[0][2]=X2;  
  A[0][3]=X3;
  A[0][4]=0;
  A[1][0]=(g-3.0)*X1*u*u/2.0 - X2*u*v - X3*u*w + (g-1.0)*X1*(v*v+w*w)/2.0;
  A[1][1]=(3.0-g)*X1*u + v*X2 + w*X3; 
  A[1][2]=(1.0-g)*X1*v + u*X2;
  A[1][3]=(1.0-g)*X1*w + u*X3;
  A[1][4]=(g-1.0)*X1;
  A[2][0]=(g-3.0)*X2*v*v/2.0 - X1*v*u - X3*v*w + (g-1.0)*X2*(u*u+w*w)/2.0;
  A[2][1]=(1.0-g)*X2*u + v*X1;
  A[2][2]=(3.0-g)*X2*v + u*X1 + w*X3;
  A[2][3]=(1.0-g)*X2*w + v*X3;
  A[2][4]=(g-1.0)*X2;
  A[3][0]=(g-3.0)*X3*w*w/2.0 - X1*w*u - X2*w*v + (g-1.0)*X3*(u*u+v*v)/2.0;
  A[3][1]=(1.0-g)*X3*u + w*X1;
  A[3][2]=(1.0-g)*X3*v + w*X2;
  A[3][3]=(3.0-g)*X3*w + u*X1 + v*X2;
  A[3][4]=(g-1.0)*X3;
  A[4][0]=-(X1*u+X2*v+X3*w)*(a2/(g-1.0) + (1.0-g/2.0)*(u*u+v*v+w*w));
  A[4][1]=(2.0*a2/(g-1.0) + v*v + w*w + (3.0-2.0*g)*u*u)*X1/2.0 + X2*(1.0-g)*u*v + X3*(1.0-g)*u*w;
  A[4][2]=(2.0*a2/(g-1.0) + u*u + w*w + (3.0-2.0*g)*v*v)*X2/2.0 + X1*(1.0-g)*v*u + X3*(1.0-g)*v*w;
  A[4][3]=(2.0*a2/(g-1.0) + u*u + v*v + (3.0-2.0*g)*w*w)*X3/2.0 + X1*(1.0-g)*w*u + X2*(1.0-g)*w*v;
  A[4][4]=g*(u*X1 + v*X2 + w*X3);

#endif
  

  
}


/*****************************************************************
 * Roe
 *****************************************************************/



void find_Fstar_from_jacvars(jacvars_t jacvars, metrics_t metrics, flux_t Fstar){
  long dim;
  double Vstar,q2;
 
  q2=0.0;
  for (dim=0; dim<nd; dim++) q2+=sqr(jacvars.V[dim]);
  
  Vstar=0.0e0;
  for (dim=0; dim<nd; dim++){
    Vstar=Vstar+metrics.X[dim]*jacvars.V[dim];
  }
  Fstar[0]=metrics.Omega*jacvars.rho*Vstar;
  for (dim=0; dim<nd; dim++){
    Fstar[1+dim]=metrics.Omega*jacvars.rho*jacvars.V[dim]*Vstar+
                   metrics.Omega*metrics.X[dim]*jacvars.a2*jacvars.rho/jacvars.gamma;
  }
  Fstar[1+nd]=metrics.Omega*jacvars.rho*Vstar*(jacvars.a2/(jacvars.gamma-1.0)+q2/2.0);
}






void find_A_from_jacvars(jacvars_t jacvars, metrics_t metrics, sqmat_t A){
  double g,a2;
  double X1,u;
#ifdef _2DL
  double X2,v;
#endif
#ifdef _3D
  double X3,w;
#endif

  g=jacvars.gamma;
  a2=jacvars.a2;

#ifdef _2D
  u=jacvars.V[0];
  v=jacvars.V[1];
  X1=metrics.X[0];
  X2=metrics.X[1];

  A[0][0]=0.0;
  A[0][1]=X1;
  A[0][2]=X2;
  A[0][3]=0.0;

  A[1][0]=(g-3.0)*X1*u*u/2.0 - X2*u*v  + (g-1.0)*X1*v*v/2.0;
  A[1][1]=(3.0-g)*u*X1 + v*X2;
  A[1][2]=u*X2 - (g-1.0)*X1*v;
  A[1][3]=(g-1.0)*X1;

  A[2][0]=(g-3.0)*X2*v*v/2.0 - X1*u*v  + (g-1.0)*X2*u*u/2.0;
  A[2][1]=v*X1 - (g-1.0)*X2*u;
  A[2][2]=(3.0-g)*v*X2 + u*X1;
  A[2][3]=(g-1.0)*X2;

  A[3][0]=-(X1*u+X2*v)*(a2/(g-1.0) + (1.0-g/2.0)*(u*u+v*v));
  A[3][1]=(2.0*a2/(g-1.0) + v*v + (3.0-2.0*g)*u*u)*X1/2.0 + X2*(1.0-g)*u*v;
  A[3][2]=(2.0*a2/(g-1.0) + u*u + (3.0-2.0*g)*v*v)*X2/2.0 + X1*(1.0-g)*u*v;
  A[3][3]=g*(u*X1 + v*X2);

#endif

#ifdef _3D
  u=jacvars.V[0];
  v=jacvars.V[1];
  w=jacvars.V[2];
  X1=metrics.X[0];
  X2=metrics.X[1];
  X3=metrics.X[2];

  A[0][0]=0.0;
  A[0][1]=X1; 
  A[0][2]=X2;  
  A[0][3]=X3;
  A[0][4]=0;
  A[1][0]=(g-3.0)*X1*u*u/2.0 - X2*u*v - X3*u*w + (g-1.0)*X1*(v*v+w*w)/2.0;
  A[1][1]=(3.0-g)*X1*u + v*X2 + w*X3; 
  A[1][2]=(1.0-g)*X1*v + u*X2;
  A[1][3]=(1.0-g)*X1*w + u*X3;
  A[1][4]=(g-1.0)*X1;
  A[2][0]=(g-3.0)*X2*v*v/2.0 - X1*v*u - X3*v*w + (g-1.0)*X2*(u*u+w*w)/2.0;
  A[2][1]=(1.0-g)*X2*u + v*X1;
  A[2][2]=(3.0-g)*X2*v + u*X1 + w*X3;
  A[2][3]=(1.0-g)*X2*w + v*X3;
  A[2][4]=(g-1.0)*X2;
  A[3][0]=(g-3.0)*X3*w*w/2.0 - X1*w*u - X2*w*v + (g-1.0)*X3*(u*u+v*v)/2.0;
  A[3][1]=(1.0-g)*X3*u + w*X1;
  A[3][2]=(1.0-g)*X3*v + w*X2;
  A[3][3]=(3.0-g)*X3*w + u*X1 + v*X2;
  A[3][4]=(g-1.0)*X3;
  A[4][0]=-(X1*u+X2*v+X3*w)*(a2/(g-1.0) + (1.0-g/2.0)*(u*u+v*v+w*w));
  A[4][1]=(2.0*a2/(g-1.0) + v*v + w*w + (3.0-2.0*g)*u*u)*X1/2.0 + X2*(1.0-g)*u*v + X3*(1.0-g)*u*w;
  A[4][2]=(2.0*a2/(g-1.0) + u*u + w*w + (3.0-2.0*g)*v*v)*X2/2.0 + X1*(1.0-g)*v*u + X3*(1.0-g)*v*w;
  A[4][3]=(2.0*a2/(g-1.0) + u*u + v*v + (3.0-2.0*g)*w*w)*X3/2.0 + X1*(1.0-g)*w*u + X2*(1.0-g)*w*v;
  A[4][4]=g*(u*X1 + v*X2 + w*X3);

#endif
  

}



double _a_from_jacvars(jacvars_t jacvars){
  double tmp;
  tmp=sqrt(jacvars.a2);
  return(tmp);
}


double _eta_from_jacvars(jacvars_t jacvars){
  return(jacvars.eta);
}


double _V_from_jacvars(jacvars_t jacvars, long dim){
  return(jacvars.V[dim]);
}



void find_Lambda_from_jacvars(jacvars_t jacvars, metrics_t metrics, sqmat_t lambda){
  double sum,Vstar;
  long flux,dim,row,col;
  double a;
  Vstar=0.0e0;
  for (dim=0; dim<nd; dim++){
    Vstar=Vstar+metrics.X[dim]*jacvars.V[dim];
  }
  for (row=0; row<nf; row++){
    for (col=0; col<nf; col++){
      lambda[row][col]=0.0e0;
    }
  }
  for (flux=0; flux<nf; flux++){
      lambda[flux][flux]=Vstar;
  }
  sum=0.0e0;
  for (dim=0; dim<nd; dim++){
    sum=sum+sqr(metrics.X[dim]);
  }
  assert(sum>=0.0e0);
  sum=sqrt(sum);
  a=_a_from_jacvars(jacvars);
  if (EIGENSET==1){
    lambda[nd][nd]=Vstar-a*sum;
    lambda[nd+1][nd+1]=Vstar+a*sum;
  } else {
    lambda[nd][nd]=Vstar+a*sum;
    lambda[nd+1][nd+1]=Vstar-a*sum;
  }
}


void find_Linv_from_jacvars(jacvars_t jacvars, metrics_t metrics, sqmat_t R){
  double g,q2,a,a2;
  double Xmag2,Vstar,Xmag;
  double X1,u;
#ifdef _2DL
  double X2,v;
#endif
#ifdef _3D
  double X3,w;
  double denom,l11,l21,l31,l12,l22,l32;
#endif

  if (EIGENSET==3) rearrange_metrics_EIGENSET_3(&metrics);


  g=jacvars.gamma;
  a2=jacvars.a2;
  a=sqrt(a2);

#ifdef _2D
  u=jacvars.V[0];
  v=jacvars.V[1];
  X1=metrics.X[0];
  X2=metrics.X[1];
  q2=sqr(u)+sqr(v);
  Xmag=sqrt(X1*X1 + X2*X2);
  Xmag2=(X1*X1 + X2*X2);
  Vstar=u*X1 + v*X2;

  if (EIGENSET==1) {
    R[0][0]=X1;
    R[0][1]=-v*X1 + u*X2;
    R[0][2]=2.0*(g - 1.0)*Xmag2;
    R[0][3]=2.0*(g - 1.0)*Xmag2;

    R[1][0]=Vstar;
    R[1][1]=X2*(u*u - v*v)/2.0 - u*v*X1;
    R[1][2]=2.0*(g - 1.0)*(u*Xmag2 - X1*a*Xmag);
    R[1][3]=2.0*(g - 1.0)*(u*Xmag2 + X1*a*Xmag);

    R[2][0]=0.0;
    R[2][1]=X1*(u*u - v*v)/2.0 + u*v*X2;
    R[2][2]=2.0*(g - 1.0)*(v*Xmag2 - X2*a*Xmag);
    R[2][3]=2.0*(g - 1.0)*(v*Xmag2 + X2*a*Xmag);

    R[3][0]=X1*(u*u - v*v)/2.0 + u*v*X2;
    R[3][1]=0.0;
    R[3][2]=2.0*a2*Xmag2 + (g - 1.0)*(q2*Xmag2 - 2.0*Vstar*a*Xmag);
    R[3][3]=2.0*a2*Xmag2 + (g - 1.0)*(q2*Xmag2 + 2.0*Vstar*a*Xmag);
  }


  if (EIGENSET==11) {
    assert(Xmag!=0.0e0);
    R[0][0]=X1;
    R[0][1]=0.0e0;
    R[0][2]=1.0e0;
    R[0][3]=1.0e0;
    R[1][0]=u*X1 + v*X2;
    R[1][1]=X2*a/Xmag;
    R[1][2]=u+a*X1/Xmag;
    R[1][3]=u-a*X1/Xmag;
    R[2][0]=0.0;
    R[2][1]=-X1*a/Xmag;
    R[2][2]=v+a*X2/Xmag;
    R[2][3]=v-a*X2/Xmag; 
    R[3][0]=X1*(u*u - v*v)/2.0 + u*v*X2;
    R[3][1]=X2*a/Xmag*u-X1*a/Xmag*v;
    R[3][2]=a*a/(g-1.0)+(u*u+v*v)/2.0+a*(u*X1+v*X2)/Xmag;
    R[3][3]=a*a/(g-1.0)+(u*u+v*v)/2.0-a*(u*X1+v*X2)/Xmag;


  }

  if (EIGENSET==12){
    R[0][0]=X2;
    R[0][1]=X1;
    R[0][2]=1.0;
    R[0][3]=1.0;
    R[1][0]=0.0;
    R[1][1]=Vstar;
    R[1][2]=u + X1/Xmag*a;
    R[1][3]=u - X1/Xmag*a;
    R[2][0]=Vstar;
    R[2][1]=0.0;
    R[2][2]=v + X2/Xmag*a;
    R[2][3]=v - X2/Xmag*a;
    R[3][0]=(v*v-u*u)*X2/2.0 + u*v*X1;
    R[3][1]=(u*u-v*v)*X1/2.0 + u*v*X2;
    R[3][2]=a*a/(-1.0 + g) + q2/2.0 + (a*Vstar)/Xmag;
    R[3][3]=a*a/(-1.0 + g) + q2/2.0 - (a*Vstar)/Xmag;
  }

  if (EIGENSET==14){
    R[0][0]=X2;
    R[0][1]=X1;
    R[0][2]=1.0e0;
    R[0][3]=1.0e0;
    R[1][0]=(-a + u)*X2;
    R[1][1]=u*X1 + a*X2;
    R[1][2]= u + (a*X1)/Xmag; 
    R[1][3]=u - (a*X1)/Xmag;
    R[2][0]=a*X1 + v*X2;
    R[2][1]=(-a + v)*X1; 
    R[2][2]=v + (a*X2)/Xmag; 
    R[2][3]=v - (a*X2)/Xmag;
    R[3][0]=a*v*X1 - a*u*X2 + (u*u*X2)/2.0 + (v*v*X2)/2.0;
    R[3][1]=(u*u*X1)/2.0 - a*v*X1 + (v*v*X1)/2.0 + a*u*X2; 
    R[3][2]=a*a/(-1.0 + g) + 1.0/2.0*q2 + (a*(u*X1 + v*X2))/Xmag; 
    R[3][3]=a*a/(-1.0 + g) + 1.0/2.0*q2 - (a*(u*X1 + v*X2))/Xmag;
   }

  if (EIGENSET==3){
    R[0][0]=1.0e0;
    R[0][1]=1.0e0;
    R[0][2]=1.0e0;
    R[0][3]=1.0e0;
    R[1][0]=u + (a*X2)/Xmag;
    R[1][1]=u - (a*X2)/Xmag;
    R[1][2]=u + (a*X1)/Xmag;
    R[1][3]=u - (a*X1)/Xmag;
    R[2][0]=v - (a*X1)/Xmag;
    R[2][1]=v + (a*X1)/Xmag;
    R[2][2]=v + (a*X2)/Xmag; 
    R[2][3]=v - (a*X2)/Xmag;
    R[3][0]=1.0/2.0*(q2 + (2.0*a*(-v*X1 + u*X2))/Xmag); 
    R[3][1]=1.0/2.0*(q2 + (2.0*a*(v*X1 - u*X2))/Xmag); 
    R[3][2]=a2/(-1.0 + g) + 1.0/2.0*q2 + (a*(u*X1 + v*X2))/Xmag;
    R[3][3]=a2/(-1.0 + g) + 1.0/2.0*q2 - (a*(u*X1 + v*X2))/Xmag;
  }


  if (EIGENSET==2) {
    R[0][0]=1.0e0;
    R[0][1]=0.0e0;
    R[0][2]=1.0e0;
    R[0][3]=1.0e0;
    R[1][0]=u;
    R[1][1]=X2*a/Xmag;
    R[1][2]=u+a*X1/Xmag;
    R[1][3]=u-a*X1/Xmag;
    R[2][0]=v;
    R[2][1]=-X1*a/Xmag;
    R[2][2]=v+a*X2/Xmag;
    R[2][3]=v-a*X2/Xmag; 
    R[3][0]=(u*u+v*v)/2.0;
    R[3][1]=X2*a/Xmag*u-X1*a/Xmag*v;
    R[3][2]=a*a/(g-1.0)+(u*u+v*v)/2.0+a*(u*X1+v*X2)/Xmag;
    R[3][3]=a*a/(g-1.0)+(u*u+v*v)/2.0-a*(u*X1+v*X2)/Xmag;
  }



  if (EIGENSET==13){
    R[0][0]=0.0e0;
    R[0][1]=1.0e0;
    R[0][2]=1.0e0;
    R[0][3]=1.0e0;
    R[1][0]=a*X2/Xmag;
    R[1][1]=u;
    R[1][2]=u + (a*X1)/Xmag; 
    R[1][3]=u - (a*X1)/Xmag;
    R[2][0]=-X1*a/Xmag;
    R[2][1]=v; 
    R[2][2]=v + (a*X2)/Xmag; 
    R[2][3]=v - (a*X2)/Xmag;
    R[3][0]=(-v*X1 + u*X2)*a/Xmag;
    R[3][1]=(u*u + v*v)/2.0; 
    R[3][2]=a2/(-1.0 + g) + 0.5*q2 + (a*(u*X1 + v*X2))/Xmag; 
    R[3][3]=a2/(-1.0 + g) + 0.5*q2 - (a*(u*X1 + v*X2))/Xmag;
  }


#endif


#ifdef _3D
  u=jacvars.V[0];
  v=jacvars.V[1];
  w=jacvars.V[2];
  X1=metrics.X[0];
  X2=metrics.X[1];
  X3=metrics.X[2];
  q2=sqr(u)+sqr(v)+sqr(w);
  Xmag=sqrt(X1*X1 + X2*X2 + X3*X3);
  Xmag2=(X1*X1 + X2*X2 + X3*X3);
  Vstar=u*X1 + v*X2 + w*X3;

  if (EIGENSET==1){
    R[0][0]=2.0*X1; 
    R[0][1]=-2.0*w*X1 + 2.0*u*X3;
    R[0][2]=-2.0*v*X1 + 2.0*u*X2;
    R[0][3]=2.0*(g - 1.0)*Xmag2;
    R[0][4]=2.0*(g - 1.0)*Xmag2;              
   
    R[1][0]=2.0*Vstar; 
    R[1][1]=-2.0*u*w*X1-2.0*v*w*X2+u*u*X3+v*v*X3-w*w*X3;  
    R[1][2]=-2.0*u*v*X1-2.0*v*w*X3+u*u*X2-v*v*X2+w*w*X2;
    R[1][3]=2.0*(g - 1.0)*(u*Xmag2 - X1*a*Xmag);
    R[1][4]=2.0*(g - 1.0)*(u*Xmag2 + X1*a*Xmag);

    R[2][0]=0.0;
    R[2][1]=0.0;
    R[2][2]=u*u*X1-(v*v+w*w)*X1+2.0*u*(v*X2+w*X3);
    R[2][3]=2.0*(g - 1.0)*(v*Xmag2 - X2*a*Xmag);
    R[2][4]=2.0*(g - 1.0)*(v*Xmag2 + X2*a*Xmag);

    R[3][0]=0.0;
    R[3][1]=u*u*X1-(v*v+w*w)*X1+2.0*u*(v*X2+w*X3);
    R[3][2]=0.0;
    R[3][3]=2.0*(g - 1.0)*(w*Xmag2 - X3*a*Xmag);
    R[3][4]=2.0*(g - 1.0)*(w*Xmag2 + X3*a*Xmag);

    R[4][0]=X1*(u*u-v*v-w*w) + 2.0*u*v*X2 + 2.0*u*w*X3; 
    R[4][1]=0.0;
    R[4][2]=0.0;
    R[4][3]=2.0*a2*Xmag2 + (g - 1.0)*(q2*Xmag2 - 2.0*Vstar*a*Xmag);
    R[4][4]=2.0*a2*Xmag2 + (g - 1.0)*(q2*Xmag2 + 2.0*Vstar*a*Xmag);
  }

  if (EIGENSET==2){
    denom = sqrt(sqr(X3 - X2) + sqr(X1 - X3) + sqr(X2 - X1))/Xmag;
    assert(denom!=0.0e0);
    l11 = (X3 - X2)/Xmag/denom;
    l21 = (X1 - X3)/Xmag/denom;
    l31 = (X2 - X1)/Xmag/denom;
    l12 = (X2*l31 - X3*l21)/Xmag;
    l22 = (X3*l11 - X1*l31)/Xmag;
    l32 = (X1*l21 - X2*l11)/Xmag;

    R[0][0]=1.0e0;
    R[0][1]=0.0e0;
    R[0][2]=0.0e0;
    R[0][3]=1.0e0;
    R[0][4]=1.0e0;
    R[1][0]=u;
    R[1][1]=l11*a;
    R[1][2]=l12*a;
    R[1][3]=u + a*X1/Xmag;
    R[1][4]=u - a*X1/Xmag;
    R[2][0]=v;
    R[2][1]=l21*a;
    R[2][2]=l22*a;
    R[2][3]=v + a*X2/Xmag; 
    R[2][4]=v - a*X2/Xmag;
    R[3][0]=w;
    R[3][1]=l31*a;
    R[3][2]=l32*a;
    R[3][3]=w + a*X3/Xmag;
    R[3][4]=w - a*X3/Xmag;
    R[4][0]=q2/2.0;
    R[4][1]=l11*a*u + l21*a*v + l31*a*w;
    R[4][2]=l12*a*u + l22*a*v + l32*a*w;
    R[4][3]=a*a/(g - 1.0) + q2/2.0 + a*Vstar/Xmag; 
    R[4][4]=a*a/(g - 1.0) + q2/2.0 - a*Vstar/Xmag;

  }

  if (EIGENSET==3){
    R[0][0]=1.0e0;
    R[0][1]=1.0e0;
    R[0][2]=1.0e0;
    R[0][3]=1.0e0;
    R[0][4]=1.0e0;
    R[1][0]=u + (a*(X2 + X3))/Xmag; 
    R[1][1]=u - (a*X2)/Xmag; 
    R[1][2]=u - (a*X3)/Xmag; 
    R[1][3]=u + (a*X1)/Xmag; 
    R[1][4]=u - (a*X1)/Xmag;
    R[2][0]=v - (a*X1)/Xmag;
    R[2][1]=v + (a*(X1 + X3))/Xmag; 
    R[2][2]=v - (a*X3)/Xmag; 
    R[2][3]=v + (a*X2)/Xmag; 
    R[2][4]=v - (a*X2)/Xmag;
    R[3][0]=w - (a*X1)/Xmag;
    R[3][1]=w - (a*X2)/Xmag; 
    R[3][2]=w + (a*(X1 + X2))/Xmag; 
    R[3][3]=w + (a*X3)/Xmag; 
    R[3][4]=w - (a*X3)/Xmag;
    R[4][0]=0.5*q2 + (a*(-v*X1 - w*X1 + u*(X2 + X3)))/Xmag; 
    R[4][1]=0.5*q2 + (a*(-(u + w)*X2 + v*(X1 + X3)))/Xmag; 
    R[4][2]=0.5*q2 + (a*(w*(X1 + X2) - (u + v)*X3))/Xmag; 
    R[4][3]=a2/(-1.0 + g) + 0.5*q2 + (a*Vstar)/Xmag; 
    R[4][4]=a2/(-1.0 + g) + 0.5*q2 - (a*Vstar)/Xmag;
  }
#endif

}


void find_M_from_jacvars ( jacvars_t jacvars, metrics_t metrics, sqmat_t M ) {
  double g, q2, a, a2;
  double X1, u;

#ifdef _2D
  double Xmag;
#endif

#ifdef _2DL
  double X2, v, sum1;
#endif
#ifdef _3D
  double X3, w;
  double X32, X22, X12, X33, X23, X13, X34, X24, X14;
  double aXmag, gm1, sum2, sum3, sum4, sum5, sum6, sum7, sum8, sum9, sum10, sum11, sum12, sum13, sum14, sum15,
    Xsum1;
  double X22pX32, X22pX322, denom1, denom2, denom3, denom4, sum20, X2X3, X2X32, X2X33, X13X2, X13X3, X1X2,
    X1X3, X22X3, X23X3, X22X32, X2pX3, X2mX3, X1X2X22pX32, X12X22pX32, X1X3X22pX32, X13X2pX3, X1X2pX3,
    X2X2mX3, X3X2mX3;
  double sum21, sum22, sum23, sum24, sum25, sum26, sum27, sum28, sum29, sum30, sum31, sum32, sum33, sum34,
    sum35, sum36, sum37, sum38, sum39, sum40, sum41;
  double sum42, sum43, sum44, sum45, sum46, sum47, sum48, sum49, sum50, sum51, sum52, sum53, sum54, sum55,
    sum56, sum57, sum58, sum59, sum60, sum61, sum62, sum63;
  double sum64, sum65, sum66, sum67, sum68, sum69, sum70, sum71, sum72, sum73, sum74;
#endif

  rearrange_metrics_EIGENSET_3 ( &metrics );

  g = jacvars.gamma;
  a2 = jacvars.a2;
  a = sqrt ( a2 );

#ifdef _2D
  u = jacvars.V[0];
  v = jacvars.V[1];
  X1 = metrics.X[0];
  X2 = metrics.X[1];
  q2 = sqr ( u ) + sqr ( v );
  Xmag = sqrt ( X1 * X1 + X2 * X2 );
  sum1 = 2.0 * a2 * ( -2.0 - g + g * g );

  M[0][0] = ( 2.0 * a2 - ( -1.0 + g ) * q2 + ( 2.0 * a * ( v * X1 - u * X2 ) ) / Xmag ) / ( 4.0 * a2 )
    + ( ( -1.0 + g ) * ( -2.0 * a2 + ( -1.0 + g ) * g * q2 ) ) / ( 2.0 * sum1 );
  M[0][1] = -( ( u - g * u - ( a * X2 ) / Xmag ) / ( 2.0 * a2 ) )
    + -( ( sqr ( -1.0 + g ) * g * u ) / ( sum1 ) );
  M[0][2] = -( ( v - g * v + ( a * X1 ) / Xmag ) / ( 2.0 * a2 ) )
    - ( ( sqr ( -1.0 + g ) * g * v ) / ( sum1 ) );
  M[0][3] = -( ( -1.0 + g ) / ( 2.0 * a2 ) )
    + ( sqr ( -1.0 + g ) * g ) / ( sum1 );
  M[1][0] = ( 2.0 * a2 - ( -1.0 + g ) * q2 + ( a * ( -2.0 * v * X1 + 2.0 * u * X2 ) ) / Xmag ) / ( 4.0 * a2 )
    + ( ( -1.0 + g ) * ( -2.0 * a2 + ( -1.0 + g ) * g * q2 ) ) / ( 2.0 * sum1 );
  M[1][1] = -( ( u - g * u + ( a * X2 ) / Xmag ) / ( 2.0 * a2 ) )
    - ( ( sqr ( -1.0 + g ) * g * u ) / ( sum1 ) );
  M[1][2] = -( ( v - g * v - ( a * X1 ) / Xmag ) / ( 2.0 * a2 ) )
    - ( ( sqr ( -1.0 + g ) * g * v ) / ( sum1 ) );
  M[1][3] = -( ( -1 + g ) / ( 2.0 * a2 ) )
    + ( sqr ( -1.0 + g ) * g ) / ( sum1 );
  M[2][0] = ( -2.0 * a * ( u * X1 + v * X2 ) + ( -1.0 + g ) * q2 * Xmag ) / ( 4.0 * a2 * Xmag )
    + ( ( -1.0 + g ) * ( 2.0 * a2 - ( -1.0 + g ) * g * q2 ) ) / ( 2.0 * sum1 );
  M[2][1] = ( u - g * u + ( a * X1 ) / Xmag ) / ( 2.0 * a2 )
    + ( sqr ( -1.0 + g ) * g * u ) / ( sum1 );
  M[2][2] = ( v - g * v + ( a * X2 ) / Xmag ) / ( 2.0 * a2 )
    + ( sqr ( -1.0 + g ) * g * v ) / ( sum1 );
  M[2][3] = ( -1 + g ) / ( 2.0 * a2 )
    - ( ( sqr ( -1.0 + g ) * g ) / ( sum1 ) );
  M[3][0] = ( 2.0 * a * ( u * X1 + v * X2 ) + ( -1.0 + g ) * q2 * Xmag ) / ( 4.0 * a2 * Xmag )
    + ( ( -1.0 + g ) * ( 2.0 * a2 - ( -1.0 + g ) * g * q2 ) ) / ( 2.0 * sum1 );
  M[3][1] = ( u - g * u - ( a * X1 ) / Xmag ) / ( 2.0 * a2 )
    + ( sqr ( -1.0 + g ) * g * u ) / ( sum1 );
  M[3][2] = ( v - g * v - ( a * X2 ) / Xmag ) / ( 2.0 * a2 )
    + ( sqr ( -1.0 + g ) * g * v ) / ( sum1 );
  M[3][3] = ( -1.0 + g ) / ( 2.0 * a2 )
    - ( ( sqr ( -1.0 + g ) * g ) / ( sum1 ) );

#endif

#ifdef _3D
  u = jacvars.V[0];
  v = jacvars.V[1];
  w = jacvars.V[2];
  X1 = metrics.X[0];
  X2 = metrics.X[1];
  X3 = metrics.X[2];
  q2 = sqr ( u ) + sqr ( v ) + sqr ( w );

  X32 = powint ( X3, 2 );
  X22 = powint ( X2, 2 );
  X12 = powint ( X1, 2 );

  X33 = powint ( X3, 3 );
  X23 = powint ( X2, 3 );
  X13 = powint ( X1, 3 );

  X34 = powint ( X3, 4 );
  X24 = powint ( X2, 4 );
  X14 = powint ( X1, 4 );

  aXmag = a * sqrt ( X12 + X22 + X32 );
  a2 = a * a;
  gm1 = -1.0 + g;

  sum1 = -1.0 - 4.0 * g + 2.0 * g * g;
  sum2 = -5.0 - 5.0 * g + 4.0 * g * g;
  sum3 = -4.0 - g + 2.0 * g * g;
  sum4 = 5.0 + 5.0 * g - 4.0 * g * g;
  sum5 = -13.0 - 7.0 * g + 8.0 * g * g;
  sum6 = 10.0 + 10.0 * g - 8.0 * g * g;
  sum7 = 8.0 + 2.0 * g - 4.0 * g * g;
  sum8 = -3.0 - 2.0 * g + 2.0 * g * g;
  sum9 = 7.0 + 3.0 * g - 4.0 * g * g;

  sum10 = 26.0 + 14.0 * g - 16.0 * g * g;
  sum11 = 2.0 + 3.0 * g - 2.0 * g * g;
  sum12 = 6.0 + 4.0 * g - 4.0 * g * g;
  sum13 = -2.0 - 3.0 * g + 2.0 * g * g;
  sum14 = -7.0 - 3.0 * g + 4.0 * g * g;
  sum15 = -4.0 - 6.0 * g + 4.0 * g * g;

  Xsum1 = ( X1 + X2 + X3 ) * sqr ( X12 + X22 + X32 );
  X22pX32 = ( X22 + X32 );
  X22pX322 = sqr ( X22pX32 );

  denom1 = ( 6.0 * a2 * ( sum8 ) * Xsum1 );
  denom2 = ( 4.0 * a2 * ( sum8 ) * Xsum1 );
  denom3 = ( 2.0 * a2 * ( sum8 ) );
  denom4 = ( a2 * ( sum8 ) );
  sum20 = 2.0 * a2 * ( sum1 ) * Xsum1 + 3.0 * ( gm1 ) * ( q2 ) * Xsum1;

  X2X3 = X2 * X3;
  X2X32 = X2 * X32;
  X2X33 = X2 * X33;

  X13X2 = X13 * X2;
  X13X3 = X13 * X3;

  X1X2 = X1 * X2;
  X1X3 = X1 * X3;
  X22X3 = X22 * X3;
  X23X3 = X23 * X3;
  X22X32 = X22 * X32;
  X2pX3 = X2 + X3;
  X2mX3 = X2 - X3;

  X1X2X22pX32 = X1X2 * X22pX32;
  X12X22pX32 = X12 * X22pX32;
  X1X3X22pX32 = X1X3 * X22pX32;
  X13X2pX3 = X13 * X2pX3;
  X1X2pX3 = X1 * X2pX3;
  X2X2mX3 = X2 * X2mX3;
  X3X2mX3 = X3 * X2mX3;

  sum43 = 3.0 * ( gm1 ) * X2X3;
  sum21 = X3X2mX3 * ( ( sum2 ) * X22 - sum43 + ( sum2 ) * X32 );
  sum22 = ( sum2 ) * X2X3;
  sum23 = ( sum2 ) * X2X32;
  sum24 = ( sum3 ) * X32;
  sum25 = ( sum2 ) * X13X2pX3;
  sum26 = ( -sum2 ) * X14;
  sum27 = ( sum6 ) * X22;
  sum28 = -6.0 * ( gm1 ) * Xsum1;
  sum29 = sum25 + 2.0 * ( sum2 ) * X12X22pX32 + ( sum5 ) * X22pX322;
  sum30 = sum26 + 2.0 * ( -sum2 ) * X13X2 - ( sum5 ) * X1X2X22pX32;
  sum31 = sum26 + 2.0 * ( -sum2 ) * X13X3 - ( sum5 ) * X1X3X22pX32;
  sum32 = 2.0 * ( -sum3 ) * X13X3 - ( -sum4 ) * X1X3X22pX32;
  sum33 = 2.0 * ( sum3 ) * X12X22pX32;
  sum34 = ( -sum4 ) * X1X2X22pX32;
  sum35 = 2.0 * ( sum3 ) * X13X2;
  sum36 = 2.0 * ( sum13 ) * X22X32;
  sum37 = ( sum14 ) * X1X3X22pX32;
  sum38 = 2.0 * ( sum3 ) * X13X3;
  sum39 = 2.0 * ( sum13 ) * X12X22pX32;
  sum40 = 2.0 * ( -sum3 ) * X22X3;
  sum41 = 2.0 * ( -sum3 ) * X22X32;
  sum42 = 2.0 * ( -sum2 ) * X32;
  sum44 = 2.0 * ( sum2 ) * X22;
  sum45 = ( 2.0 * sum3 ) * X22;
  sum46 = 2.0 * ( gm1 ) * X32;
  sum47 = 2.0 * ( -sum2 ) * X22X3;
  sum48 = 2.0 * ( -sum3 ) * X2X32;
  sum49 = ( 2.0 * sum3 ) * X23;
  sum50 = 2.0 * ( sum11 ) * X13X2;
  sum51 = 2.0 * ( sum3 ) * X33;
  sum52 = 2.0 * ( -sum2 ) * X33;
  sum53 = 2.0 * ( sum11 ) * X2X33;
  sum54 = 2.0 * ( sum11 ) * X13X3;
  sum55 = 2.0 * ( sum13 ) * X32;
  sum56 = 2.0 * ( sum11 ) * X23X3;
  sum57 = 2.0 * ( gm1 ) * X22;
  sum58 = 2.0 * ( -sum8 ) * X34;
  sum59 = 2.0 * ( sum11 ) * X22X32;
  sum60 = 2.0 * ( sum3 ) * X2X32;
  sum61 = ( gm1 ) * X22pX322;
  sum62 =
    ( sum12 ) * X14 - ( sum14 ) * X13X2pX3 - sum33 + sum61 - X1X2pX3 * ( ( sum15 ) * X22 + sum43 + sum55 );
  sum63 = ( sum12 ) * X14 - sum39 - sum61 - X1X2pX3 * ( sum45 - sum43 + 2.0 * sum24 );
  sum64 = 4.0 * a2 * ( gm1 ) * Xsum1 - 3.0 * ( gm1 ) * ( q2 ) * Xsum1;
  sum65 = ( sum5 ) * X23 + ( -sum40 ) + 2.0 * sum23 + ( -sum4 ) * X33;
  sum66 = ( sum2 ) * X1X3X22pX32;
  sum67 = ( gm1 ) * X14;
  sum68 = ( gm1 ) * X24;
  sum69 = ( gm1 ) * X34;
  sum70 = X1 * ( sum49 + ( -sum5 ) * X22X3 + sum23 + sum52 );
  sum71 = X13 * ( ( sum2 ) * X2 + ( -sum5 ) * X3 );
  sum72 = ( sum4 ) * X22X3 + ( sum5 ) * X2X32 + ( -sum51 );
  sum73 = -sum67 + ( -sum35 ) + ( sum12 ) * X24 + ( sum4 ) * X23X3;
  sum74 =
    sum67 + sum50 + ( sum12 ) * X24 + ( sum9 ) * X23X3 + sum41 + sum53 + sum69 - ( sum14 ) * X1X2X22pX32 +
    X12 * ( ( sum7 ) * X22 + ( sum9 ) * X2X3 + sum46 );

  M[0][0] = ( sum20 +
              aXmag * ( u * ( -sum29 -
                              X1X2pX3 * ( ( 2.0 * sum3 ) * X22pX32 - sum43 ) ) +
                        w * ( -sum31 +
                              X12 * ( sum44 + ( sum4 ) * X2X3 + 2.0 * sum24 ) +
                              X2X2mX3 * ( ( sum2 ) * X22 -
                                          sum43 + ( sum2 ) * X32 ) ) +
                        v * ( -sum30 - sum21 + X12 * ( sum45 - sum22 + ( -sum42 ) ) ) ) ) / denom1;

  M[0][1] = ( u * sum28 + aXmag * ( sum29 + X1X2pX3 * ( sum45 - sum43 + 2.0 * sum24 ) ) ) / denom1;

  M[0][2] = ( v * sum28 + aXmag * ( sum30 + X12 * ( ( sum7 ) * X22 + sum22 + sum42 ) + sum21 ) ) / denom1;

  M[0][3] = ( w * sum28 +
              aXmag * ( sum31 +
                        X12 * ( sum27 + sum22 +
                                2.0 * ( -sum24 ) ) -
                        X2X2mX3 * ( ( sum2 ) * X22 - sum43 + ( sum2 ) * X32 ) ) ) / denom1;

  M[0][4] = ( gm1 ) / denom4;

  M[1][0] = ( sum20 +
              aXmag * ( v * ( ( -sum5 ) * X14 +
                              ( -sum35 ) - sum34 +
                              X12 * ( sum27 - sum22 +
                                      2.0 * ( -sum5 ) * X32 ) +
                              X3 * ( ( -sum2 ) * X23 + sum47 +
                                     sum48 + ( -sum5 ) * X33 ) ) +
                        u * ( X13 * ( ( sum5 ) * X2 + ( -sum2 ) * X3 ) +
                              sum33 + ( -sum4 ) * X22pX322 +
                              X1 * ( 2.0 * ( sum2 ) * X23 + sum72 ) ) +
                        w * ( -sum26 +
                              sum32 +
                              X12 * ( sum44 + ( sum5 ) * X2X3 + 2.0 * sum24 ) +
                              X2 * ( ( sum2 ) * X23 + ( -sum47 ) + sum60 + ( sum5 ) * X33 ) ) ) ) / denom1;

  M[1][1] = ( u * sum28 +
              aXmag * ( X13 * ( ( -sum5 ) * X2 + ( sum2 ) * X3 ) -
                        sum33 - ( sum2 ) * X22pX322 + X1 * ( ( sum6 ) * X23 - sum72 ) ) ) / denom1;

  M[1][2] = ( v * sum28 +
              aXmag * ( ( sum5 ) * X14 +
                        sum35 + ( sum2 ) * X1X2X22pX32 +
                        X12 * ( sum44 + sum22 +
                                2.0 * ( sum5 ) * X32 ) +
                        X3 * ( ( sum2 ) * X23 + ( -sum47 ) + sum60 + ( sum5 ) * X33 ) ) ) / denom1;

  M[1][3] = ( w * sum28 +
              aXmag * ( sum26 +
                        sum38 + sum66 +
                        X12 * ( sum27 + ( -sum5 ) * X2X3 +
                                2.0 * ( -sum24 ) ) +
                        X2 * ( ( -sum2 ) * X23 + sum47 + sum48 + ( -sum5 ) * X33 ) ) ) / denom1;

  M[1][4] = ( gm1 ) / denom4;

  M[2][0] = ( sum20 +
              aXmag * ( w * ( ( -sum5 ) * X14 +
                              sum32 +
                              X12 * ( ( sum10 ) * X22 + ( sum4 ) * X2X3 + sum42 ) +
                              X2 * ( ( -sum5 ) * X23 + sum40 +
                                     2.0 * ( -sum23 ) + ( -sum2 ) * X33 ) ) -
                        u * ( sum71 -
                              sum33 - ( -sum4 ) * X22pX322 +
                              sum70 ) +
                        v * ( -sum26 +
                              ( -sum35 ) - sum34 +
                              X12 * ( sum45 + ( sum5 ) * X2X3 + ( -sum42 ) ) +
                              X3 * ( ( sum5 ) * X23 +
                                     ( -sum40 ) + 2.0 * sum23 + ( -sum4 ) * X33 ) ) ) ) / denom1;

  M[2][1] = ( u * sum28 + aXmag * ( sum71 - sum33 - ( sum2 ) * X22pX322 + sum70 ) ) / denom1;

  M[2][2] = ( v * sum28 +
              aXmag * ( sum26 +
                        sum35 + ( sum2 ) * X1X2X22pX32 +
                        X12 * ( ( sum7 ) * X22 + ( -sum5 ) * X2X3 +
                                sum42 ) + X3 * ( ( -sum65 ) ) ) ) / denom1;

  M[2][3] = ( w * sum28 +
              aXmag * ( ( sum5 ) * X14 +
                        sum38 + sum66 +
                        X12 * ( 2.0 * ( sum5 ) * X22 + sum22 + ( -sum42 ) ) + X2 * ( sum65 ) ) ) / denom1;

  M[2][4] = ( gm1 ) / denom4;

  M[3][0] = ( sum64 +
              aXmag * ( v * ( sum74 ) +
                        w * ( sum67 + sum68 +
                              sum54 +
                              sum56 +
                              sum41 + ( sum9 ) * X2X33 +
                              sum58 - sum37 +
                              X12 * ( sum57 + ( sum9 ) * X2X3 +
                                      2.0 * ( -sum24 ) ) ) + u * ( sum62 ) ) ) / denom2;

  M[3][1] = ( -u * sum28 + aXmag * ( -sum62 ) ) / denom2;

  M[3][2] = ( -v * sum28 - aXmag * ( sum74 ) ) / denom2;

  M[3][3] = ( -w * sum28 -
              aXmag * ( sum67 + sum68 +
                        sum54 + sum56 +
                        sum41 + ( sum9 ) * X2X33 +
                        sum58 - sum37 + X12 * ( sum57 + ( sum9 ) * X2X3 + 2.0 * ( -sum24 ) ) ) ) / denom2;

  M[3][4] = -( ( 3.0 * ( gm1 ) ) / denom3 );

  M[4][0] = ( sum64 +
              aXmag * ( v * ( -sum73 +
                              sum36 +
                              2.0 * ( sum3 ) * X2X33 + sum69 + sum34 +
                              X12 * ( ( sum15 ) * X22 + sum22 +
                                      sum46 ) ) +
                        w * ( sum67 + sum68 +
                              sum38 + 2.0 * ( sum3 ) * X23X3 +
                              sum36 + ( -sum4 ) * X2X33 +
                              ( -sum58 ) + ( -sum4 ) * X1X3X22pX32 +
                              X12 * ( sum57 + sum22 + sum55 ) ) + u * ( +sum25 - sum63 ) ) ) / denom2;

  M[4][1] = ( -u * sum28 + aXmag * ( -( sum2 ) * X13X2pX3 + sum63 ) ) / denom2;

  M[4][2] = ( -v * sum28 +
              aXmag * ( sum73
                        + sum59 +
                        2.0 * ( -sum3 ) * X2X33 - sum69 - ( sum2 ) * X1X2X22pX32 +
                        X12 * ( ( -sum15 ) * X22 - sum22 - sum46 ) ) ) / denom2;

  M[4][3] = ( -w * sum28 +
              aXmag * ( -sum67 - sum68 +
                        2.0 * ( -sum3 ) * X13X3 + 2.0 * ( -sum3 ) * X23X3 + sum59 + ( -sum2 ) * X2X33 +
                        sum58 - sum66 + X12 * ( -sum57 - sum22 + 2.0 * ( sum11 ) * X32 ) ) ) / denom2;

  M[4][4] = -( ( 3.0 * ( gm1 ) ) / denom3 );

#endif
}


void find_Minv_from_jacvars(jacvars_t jacvars, metrics_t metrics, sqmat_t Minv){
  double g,q2,a,a2;
  double Xmag;
  double X1,u,zeta;
#ifdef _2DL
  double X2,v;
#endif
#ifdef _3D
  double X3,w,Vstar;
#endif

  rearrange_metrics_EIGENSET_3(&metrics);


  g=jacvars.gamma;
  a2=jacvars.a2;
  a=sqrt(a2);

#ifdef _2D
  u=jacvars.V[0];
  v=jacvars.V[1];
  X1=metrics.X[0];
  X2=metrics.X[1];
  q2=sqr(u)+sqr(v);
  Xmag=sqrt(X1*X1 + X2*X2);

  zeta=(2.0-sqr(g-1.0))/(2.0*(g-1.0));
  Minv[0][0]=1.0e0;
  Minv[0][1]=1.0e0;
  Minv[0][2]=1.0e0;
  Minv[0][3]=1.0e0;
  Minv[1][0]=u + (a*X2)/Xmag;
  Minv[1][1]=u - (a*X2)/Xmag;
  Minv[1][2]=u + (a*X1)/Xmag;
  Minv[1][3]=u - (a*X1)/Xmag;
  Minv[2][0]=v - (a*X1)/Xmag;
  Minv[2][1]=v + (a*X1)/Xmag;
  Minv[2][2]=v + (a*X2)/Xmag; 
  Minv[2][3]=v - (a*X2)/Xmag;
  Minv[3][0]=1.0/2.0*(a2+q2 + (2.0*a*(-v*X1 + u*X2))/Xmag); 
  Minv[3][1]=1.0/2.0*(a2+q2 + (2.0*a*(v*X1 - u*X2))/Xmag); 
  Minv[3][2]=zeta*a2 + 1.0/2.0*q2 + (a*(u*X1 + v*X2))/Xmag;
  Minv[3][3]=zeta*a2 + 1.0/2.0*q2 - (a*(u*X1 + v*X2))/Xmag;


#endif


#ifdef _3D
  u=jacvars.V[0];
  v=jacvars.V[1];
  w=jacvars.V[2];
  X1=metrics.X[0];
  X2=metrics.X[1];
  X3=metrics.X[2];
  q2=sqr(u)+sqr(v)+sqr(w);
  Xmag=sqrt(X1*X1 + X2*X2 + X3*X3);
  Vstar=u*X1 + v*X2 + w*X3;
  zeta = (-2.0*g*g + 4.0*g + 1.0)/(3.0*g - 3.0);


  Minv[0][0]=1.0e0;
  Minv[0][1]=1.0e0;
  Minv[0][2]=1.0e0;
  Minv[0][3]=1.0e0;
  Minv[0][4]=1.0e0;
  Minv[1][0]=u + (a*(X2 + X3))/Xmag; 
  Minv[1][1]=u - (a*X2)/Xmag; 
  Minv[1][2]=u - (a*X3)/Xmag; 
  Minv[1][3]=u + (a*X1)/Xmag; 
  Minv[1][4]=u - (a*X1)/Xmag;
  Minv[2][0]=v - (a*X1)/Xmag;
  Minv[2][1]=v + (a*(X1 + X3))/Xmag; 
  Minv[2][2]=v - (a*X3)/Xmag; 
  Minv[2][3]=v + (a*X2)/Xmag; 
  Minv[2][4]=v - (a*X2)/Xmag;
  Minv[3][0]=w - (a*X1)/Xmag;
  Minv[3][1]=w - (a*X2)/Xmag; 
  Minv[3][2]=w + (a*(X1 + X2))/Xmag; 
  Minv[3][3]=w + (a*X3)/Xmag; 
  Minv[3][4]=w - (a*X3)/Xmag;
  Minv[4][0]=0.5*q2 + (a*(-v*X1 - w*X1 + u*(X2 + X3)))/Xmag; 
  Minv[4][1]=0.5*q2 + (a*(-(u + w)*X2 + v*(X1 + X3)))/Xmag; 
  Minv[4][2]=0.5*q2 + (a*(w*(X1 + X2) - (u + v)*X3))/Xmag; 
  Minv[4][3]=a2/(g-1.0) + 0.5*q2 + (a*Vstar)/Xmag; 
  Minv[4][4]=a2/(g-1.0) + 0.5*q2 - (a*Vstar)/Xmag;

  Minv[4][0]+=0.5*(a2+sqr(a*X1/Xmag)); 
  Minv[4][1]+=0.5*(a2+sqr(a*X2/Xmag)); 
  Minv[4][2]+=0.5*(a2+sqr(a*X3/Xmag)); 
  Minv[4][3]+=zeta*a2-a2/(g-1.0); 
  Minv[4][4]+=zeta*a2-a2/(g-1.0);

#endif

}




void find_Ustar_from_jacvars(jacvars_t jacvars,  metrics_t metrics, flux_t Ustar){
  long dim,flux;
  double q2;
  Ustar[0]=jacvars.rho;
  q2=0.0;
  for (dim=0; dim<nd; dim++){
    Ustar[1+dim]=jacvars.rho*jacvars.V[dim];
    q2+=sqr(jacvars.V[dim]);
  }
  Ustar[nd+1]=jacvars.rho*(0.5*q2+jacvars.a2/jacvars.gamma/(jacvars.gamma-1.0));
  for (flux=0; flux<nf; flux++) Ustar[flux]=Ustar[flux]*metrics.Omega;
}

void find_LUstar_from_jacvars(jacvars_t jacvars,  metrics_t metrics, flux_t LUstar){
  flux_t Ustar;
  sqmat_t L;
  bool FOUND;
#ifdef _2D
  double Omega,rho,u,v,X1,X2,Vstar,g;
#endif
#ifdef _3D
  double Omega,rho,u,v,w,X1,X2,X3,Xmag2,q2,g;
#endif

  if (EIGENSET==3) rearrange_metrics_EIGENSET_3(&metrics);


  FOUND=FALSE;
  

#ifdef _2D
  g=jacvars.gamma;
  rho=jacvars.rho;
  Omega=metrics.Omega;
  u=jacvars.V[0];
  v=jacvars.V[1];
  X1=metrics.X[0];
  X2=metrics.X[1];
  Vstar=u*X1 + v*X2;
  if (EIGENSET==2){
    LUstar[0]=Omega*(-1.0+g)*rho/(g);
    LUstar[1]=0.0;
    LUstar[2]=Omega*rho/(2.0*g);
    LUstar[3]=Omega*rho/(2.0*g);
    FOUND=TRUE;
  }
  if (EIGENSET==3){
    LUstar[0]=Omega*(-1.0+g)*rho/(2.0*g);
    LUstar[1]=Omega*(-1.0+g)*rho/(2.0*g);
    LUstar[2]=Omega*rho/(2.0*g);
    LUstar[3]=Omega*rho/(2.0*g);
    FOUND=TRUE;
  }
  if (EIGENSET==12){
    LUstar[0]=Omega*(-1.0+g)*rho*v/g/notzero(Vstar,1e-50);
    LUstar[1]=Omega*(-1.0+g)*rho*u/g/notzero(Vstar,1e-50);
    LUstar[2]=Omega*rho/2.0/g;
    LUstar[3]=Omega*rho/2.0/g;
    FOUND=TRUE;
  }
  if (EIGENSET==14){
    LUstar[0]=Omega*(-1.0+g)*rho/g/notzero(X1+X2,1e-50);
    LUstar[1]=Omega*(-1.0+g)*rho/g/notzero(X1+X2,1e-50);
    LUstar[2]=Omega*rho/2.0/g;
    LUstar[3]=Omega*rho/2.0/g;
    FOUND=TRUE;
  }
  if (EIGENSET==13){
    LUstar[0]=0.0e0;
    LUstar[1]=Omega*(-1.0+g)*rho/(g);
    LUstar[2]=Omega*rho/(2.0*g);
    LUstar[3]=Omega*rho/(2.0*g);
    FOUND=TRUE;
  }
#endif

#ifdef _3D
  g=jacvars.gamma;
  rho=jacvars.rho;
  Omega=metrics.Omega;
  u=jacvars.V[0];
  v=jacvars.V[1];
  w=jacvars.V[2];
  X1=metrics.X[0];
  X2=metrics.X[1];
  X3=metrics.X[2];
  q2=sqr(u)+sqr(v)+sqr(w);
  Xmag2=(X1*X1 + X2*X2 + X3*X3);
  if (EIGENSET==1){
    LUstar[0]=Omega*((-1.0 + g)*rho*q2)/(
 2.0*g*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)));
    LUstar[1]=Omega*((-1.0 + g)*rho*w)/( 
 g*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)));
    LUstar[2]=Omega*((-1.0 + g)*rho*v)/(
 g*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)));
    LUstar[3]=Omega*rho/(4.0*(-1.0 + g)*g*Xmag2);
    LUstar[4]=Omega*rho/(4.0*(-1.0 + g)*g*Xmag2);
    FOUND=TRUE;
  }
  if (EIGENSET==2){
    LUstar[0]=Omega*(-1.0+g)*rho/(g);
    LUstar[1]=0.0;
    LUstar[2]=0.0;
    LUstar[3]=Omega*rho/(2.0*g);
    LUstar[4]=Omega*rho/(2.0*g);
    FOUND=TRUE;
  }
  if (EIGENSET==3){
    LUstar[0]=Omega*((-1.0 + g)*rho)/(3.0*g);
    LUstar[1]=Omega*((-1.0 + g)*rho)/(3.0*g);
    LUstar[2]=Omega*((-1.0 + g)*rho)/(3.0*g);
    LUstar[3]=Omega*rho/(2.0*g);
    LUstar[4]=Omega*rho/(2.0*g);
    FOUND=TRUE;
  }
#endif

  if (!FOUND){
    find_L_from_jacvars(jacvars, metrics, L);
    find_Ustar_from_jacvars(jacvars, metrics, Ustar);
    multiply_matrix_and_vector(L,Ustar,LUstar);
  }
}



void find_MUstar_from_jacvars(jacvars_t jacvars,  metrics_t metrics, flux_t MUstar){
  double Omega,rho,g;

  g=jacvars.gamma;
  rho=jacvars.rho;
  Omega=metrics.Omega;
  
#ifdef _2D
  MUstar[0]=Omega*(-1.0+g)*rho/(2.0*g);
  MUstar[1]=Omega*(-1.0+g)*rho/(2.0*g);
  MUstar[2]=Omega*rho/(2.0*g);
  MUstar[3]=Omega*rho/(2.0*g);
#endif

#ifdef _3D
  MUstar[0]=Omega*((-1.0 + g)*rho)/(3.0*g);
  MUstar[1]=Omega*((-1.0 + g)*rho)/(3.0*g);
  MUstar[2]=Omega*((-1.0 + g)*rho)/(3.0*g);
  MUstar[3]=Omega*rho/(2.0*g);
  MUstar[4]=Omega*rho/(2.0*g);
#endif
}



void find_L_from_jacvars(jacvars_t jacvars, metrics_t metrics, sqmat_t L){
  double g,q2,a,a2;
  double Xmag2,Vstar,Xmag;
  double X1,u;
#ifdef _2DL
  double X2,v;
#endif
#ifdef _3D
  double X3,w;
  double denom,numer;
#endif


  if (EIGENSET==3) rearrange_metrics_EIGENSET_3(&metrics);

  g=jacvars.gamma;
  a2=jacvars.a2;
  a=sqrt(a2);

#ifdef _2D
  u=jacvars.V[0];
  v=jacvars.V[1];
  X1=metrics.X[0];
  X2=metrics.X[1];
  q2=sqr(u)+sqr(v);
  Xmag=sqrt(X1*X1 + X2*X2);
  Xmag2=(X1*X1 + X2*X2);
  Vstar=u*X1 + v*X2;

  if (EIGENSET==1){
    L[0][0]=(-(-1.0 + g)*q2*q2*Xmag2 + 
      2.0*a2*(4.0*u*v*X1*X2 + u*u*(X1*X1 - X2*X2) + 
      v*v*(-X1*X1 + X2*X2)))/notzero(2.0*a2*(u*u*X1 - v*v*X1 + 2.0*u*v*X2)*Xmag2,1e-50);
    L[0][1]=(-2.0*a2*v*X1*X2 + (-1.0 + g)*u*u*u*Xmag2 + 
      u*(2.0*a2*X2*X2 + (-1.0 + g)*v*v*Xmag2))/notzero(a2*(u*u*X1 - 
      v*v*X1 + 2.0*u*v*X2)*Xmag2,1e-50);
    L[0][2]=(2.0*a2*X1*(v*X1 - u*X2) 
      +(-1.0 + g)*v*q2*Xmag2)/notzero(a2*(u*u*X1 - v*v*X1 + 2.0*u*v*X2)*Xmag2,1e-50);

    L[0][3]=-(((-1.0 + g)*q2)/notzero(a2*(u*u*X1 - v*v*X1 + 2.0*u*v*X2),1e-50));

    L[1][0]= (2.0*a2*u*X1*X2 + 2.0*a2*v*X2*X2 - (-1.0 + g)*u*u*v*Xmag2 - 
      (-1.0 + g)*v*v*v*Xmag2)/notzero(a2*(u*u*X1 - v*v*X1 + 2.0*u*v*X2)*Xmag2,1e-50);
    L[1][1]=-((2.0*(a2*X1*X2 - (-1.0 + g)*u*v*Xmag2))/notzero(a2*(u*u*X1 - v*v*X1 + 2.0*u*v*X2)*Xmag2,1e-50));
    L[1][2]=(2.0*(a2*X1*X1 + (-1.0 + g)*v*v*Xmag2))/notzero(a2*(u*u*X1 - v*v*X1 + 
      2.0*u*v*X2)*Xmag2,1e-50);
    L[1][3]=-((2.0*(-1.0 + g)*v)/notzero(a2*(u*u*X1 - v*v*X1 + 2.0*u*v*X2),1e-50));

    L[2][0]=(2.0*a*Vstar + (-1.0 + g)*q2*Xmag)/notzero(8.0*a2*(-1.0 + g)*Xmag2*Xmag,1e-50);
    L[2][1]=(-a*X1 - (-1.0 + g)*u*Xmag)/notzero(4.0*a2*(-1.0 + g)*Xmag2*Xmag,1e-50);
    L[2][2]=(-a*X2 - (-1.0 + g)*v*Xmag)/notzero(4.0*a2*(-1.0 + g)*Xmag*Xmag2,1e-50);
    L[2][3]=1.0/notzero(4.0*a2*Xmag2,1e-50);

    L[3][0]=(-2.0*a*Vstar + (-1.0 + g)*q2*Xmag)/notzero(8.0*a2*(-1.0 + g)*Xmag2*Xmag,1e-50);
    L[3][1]=(a*X1 - (-1.0 + g)*u*Xmag)/notzero(4.0*a2*(-1.0 + g)*Xmag*Xmag2,1e-50);
    L[3][2]=(a*X2 - (-1.0 + g)*v*Xmag)/notzero(4.0*a2*(-1.0 + g)*Xmag*Xmag2,1e-50);
    L[3][3]=1.0/notzero(4.0*a2*Xmag2,1e-50);
  }
  
  if (EIGENSET==2) {
    L[0][0]=(2.0*a*a - (-1.0 + g)*(u*u + v*v))/(2.0*a*a);
    L[0][1]=((-1.0 + g)*u)/(a*a);
    L[0][2]=((-1.0 + g)*v)/(a*a);
    L[0][3]=(1.0 - g)/(a*a);

    L[1][0]=(v*X1 - u*X2)/(a*Xmag);
    L[1][1]=X2/(a*Xmag);
    L[1][2]=-X1/(a*Xmag);
    L[1][3]=0.0;

    L[2][0]=(-2.0*a*(u*X1 + v*X2) + (-1.0 + g)*(u*u + v*v)*Xmag)/(4.0*a*a*Xmag);
    L[2][1]=(u - g*u + (a*X1)/Xmag)/(2.0*a*a);
    L[2][2]=(v - g*v + (a*X2)/Xmag)/(2.0*a*a);
    L[2][3]=(-1.0 + g)/(2.0*a*a);

    L[3][0]=(2.0*a*(u*X1 + v*X2) + (-1.0 + g)*(u*u + v*v)*Xmag)/(4.0*a*a*Xmag);
    L[3][1]=(u - g*u - (a*X1)/Xmag)/(2.0*a*a);
    L[3][2]=(v - g*v - (a*X2)/Xmag)/(2.0*a*a);
    L[3][3]=(-1.0 + g)/(2.0*a*a);
  }

  if (EIGENSET==3){
    L[0][0]=(2.0*a2 - (-1.0 + g)*q2 + (2.0*a*(v*X1 - u*X2))/Xmag)/(4.0*a2);
    L[0][1]=-((u - g*u - (a*X2)/Xmag)/(2.0*a2));
    L[0][2]=-((v - g*v + (a*X1)/Xmag)/(2.0*a2));
    L[0][3]=-((-1.0 + g)/(2.0*a2));
    L[1][0]=(2.0* a2 - (-1.0 + g)*q2 + (a*(-2.0*v*X1 + 2.0*u*X2))/Xmag)/(4.0*a2);
    L[1][1]=-((u - g*u + (a*X2)/Xmag)/(2.0*a2));
    L[1][2]=-((v - g*v - (a*X1)/Xmag)/(2.0*a2));
    L[1][3]=-((-1 + g)/(2.0*a2));
    L[2][0]=(-2.0*a*(u*X1 + v*X2) + (-1.0 + g)*q2*Xmag)/(4.0*a2*Xmag);
    L[2][1]=(u - g*u + (a*X1)/Xmag)/(2.0*a2);
    L[2][2]=(v - g*v + (a*X2)/Xmag)/(2.0*a2);
    L[2][3]=(-1 + g)/(2.0*a2);
    L[3][0]=(2.0*a*(u*X1 + v*X2) + (-1.0 + g)*q2*Xmag)/(4.0*a2*Xmag);
    L[3][1]=(u - g*u - (a*X1)/Xmag)/(2.0*a2);
    L[3][2]=(v - g*v - (a*X2)/Xmag)/(2.0*a2);
    L[3][3]=(-1.0 + g)/(2.0*a2);
  }


  if (EIGENSET==11) {
L[0][0]=(2.0* a*a - (-1.0 + g)*(u*u + v*v))/notzero(2.0*a*a*X1,1e-50);
L[0][1]=((-1.0 + g)*u)/notzero(a*a*X1,1e-50);
L[0][2]=((-1 + g)*v)/notzero(a*a*X1,1e-50);
L[0][3]=(1.0 - g)/notzero(a*a*X1,1e-50);

L[1][0]=(1.0/notzero(2.0*a*a*a*X1*Xmag,1e-50))*(-2.0*a*a*u*X1*X2- 
    2.0* a*a*v*X2*X2 + (-1.0 + g)*u*u*v*Xmag2 + (-1.0 + g)*v*v*v*Xmag2);
L[1][1]=(a*a*X1*X2 - (-1.0 + g)*u*v*Xmag2)/notzero(a*a*a*X1*Xmag,1e-50);
L[1][2]=(-a*a*X1*X1 - (-1.0 + g)*v*v*Xmag2)/notzero(a*a*a*X1*Xmag,1e-50);
L[1][3]=((-1.0 + g)*v*Xmag)/notzero(a*a*a*X1,1e-50);

L[2][0]=(-2.0* a*(u*X1 + v*X2) + (-1.0 + g)*(u*u + v*v)*Xmag)/(4.0* a*a* Xmag);
L[2][1]=(u - g*u + (a*X1)/Xmag)/(2.0*a*a);
L[2][2]=(v - g*v + (a*X2)/Xmag)/(2.0*a*a);
L[2][3]=(-1.0 + g)/(2.0* a*a);

L[3][0]=(2.0*a*(u*X1 + v*X2) + (-1.0 + g)*q2*Xmag)/(4.0*a*a*Xmag);
L[3][1]=(u - g*u - (a*X1)/Xmag)/(2.0*a*a);
L[3][2]=(v - g*v - (a*X2)/Xmag)/(2.0*a*a);
L[3][3]=(-1.0 + g)/(2.0*a*a);
  }

  if (EIGENSET==12) {

L[0][0]=-(-2.0* a2* u*X1*X2 - 
      2.0* a2* v*X2*X2 + (-1.0 + g)*u*u*v*Xmag2 + (-1.0 + 
         g)*v*v*v* Xmag2)/notzero(2.0*a2*Vstar*Xmag2,1e-50);
L[0][1]=(-a2* X1 *X2 + (-1.0 + g)*u*v*Xmag2)/notzero(
  a2*Vstar*Xmag2,1e-50);
L[0][2]=(a2* X1*X1 + (-1.0 + g)*v*v*Xmag2)/notzero(
  a2*Vstar*Xmag2,1e-50);
L[0][3]=-(((-1.0 + g)*v)/notzero(
   a2* Vstar,1e-50));
L[1][0]=(2.0*a2* X1*Vstar - (-1.0 + g)*u*q2*Xmag2)/notzero(
  2.0*a2*Vstar*Xmag2,1e-50);
L[1][1]= (a2* X2*X2 + (-1.0 + g)*u*u*Xmag2)/notzero(a2*Vstar*Xmag2,1e-50);
L[1][2]=(-a2*X1*X2 + (-1.0 + g)*u*v*Xmag2)/notzero(a2*Vstar* Xmag2,1e-50);
L[1][3]= -(((-1.0 + g)*u)/notzero(a2* Vstar,1e-50));
L[2][0]=(1.0/(4.0*a2*Xmag2))*(-2.0* a* u* X1*Xmag + (-1.0 + g)*u*u*Xmag2 + 
    v*(-2.0* a* X2* Xmag + (-1.0 + g)*v*Xmag2));
L[2][1]= (a*X1*Xmag - (-1.0 + g)*u*Xmag2)/(2.0*a2*Xmag2);
L[2][2]= (a*X2*Xmag - (-1.0 + g)*v*Xmag2)/(2.0*a2*Xmag2);
L[2][3]=(-1.0 + g)/(2.0*a2);
L[3][0]=(1.0/(4.0* a2* Xmag2))*(2.0* a* u* X1*Xmag + (-1.0 + g)*u*u*Xmag2 + 
    v*(2.0* a* X2*Xmag + (-1.0 + g)*v*Xmag2));
L[3][1]= -((a*X1*Xmag + (-1.0 + g)*u*Xmag2)/(2.0*a2*Xmag2));
L[3][2]= -((a*X2*Xmag + (-1.0 + g)*v*Xmag2)/(2.0*a2*Xmag2));
L[3][3]=(-1.0 + g)/(2.0* a2);


  }

  if (EIGENSET==14){
    L[0][0]=(2.0* a *X1* (-v *X1 + u *X2) + 
  2.0* a2*Xmag2 - (-1.0 + g)*q2*Xmag2)/(
  2.0* a2*(X1*X1*X1 + X1*X1*X2 + X1*X2*X2 + 
    X2*X2*X2));
    L[0][1]=(-a*X1*X2 + (-1.0 + g)*u*Xmag2)/(
 a2*(X1*X1*X1 + X1*X1*X2 + X1*X2*X2 + X2*X2*X2));
    L[0][2]=(a*X1*X1 + (-1.0 + g)*v*Xmag2)/(
 a2*(X1*X1*X1 + X1*X1*X2 + X1*X2*X2 + X2*X2*X2));
    L[0][3]=(1.0 - g)/(a2* (X1 + X2));

    L[1][0]=(2.0* a *X2 *(v* X1 - u* X2) + 
  2.0* a2* Xmag2 - (-1.0 + g)*q2*Xmag2)/(
  2.0* a2* (X1*X1*X1 + X1*X1*X2 + X1*X2*X2 + X2*X2*X2));
    L[1][1]= (a*X2*X2 + (-1.0 + g)*u*Xmag2)/(
 a2* (X1*X1*X1 + X1*X1*X2 + X1*X2*X2 + 
    X2*X2*X2));
    L[1][2]=(-a*X1*X2 + (-1.0 + g)*v*Xmag2)/(
 a2* (X1*X1*X1 + X1*X1*X2 + X1*X2*X2 + X2*X2*X2));
    L[1][3]=(1.0 - g)/(
 a2*(X1 + X2));

    L[2][0]=(1.0/(
 4.0* a2* Xmag2))*(-2.0* a* u* X1* Xmag + (-1.0 + g)*u*u*Xmag2 + 
   v*(-2.0*a*X2*Xmag + (-1.0 + g)*v*Xmag2));
    L[2][1]= (a*X1*Xmag - (-1.0 + g)*u*Xmag2)/(
 2.0* a2* Xmag2);
    L[2][2]= (a*X2*Xmag - (-1.0 + g)*v*Xmag2)/(
 2.0* a2* Xmag2);
    L[2][3]= (-1.0 + g)/(2.0* a2);

    L[3][0]=(1.0/(
 4.0* a2* Xmag2))*(2.0* a* u* X1* Xmag + (-1.0 + g)*u*u*Xmag2 + 
   v*(2.0* a* X2* Xmag + (-1.0 + g)*v*Xmag2));
    L[3][1]= -((a*X1*Xmag + (-1.0 + g)*u*Xmag2)/(
  2.0* a2*Xmag2));
    L[3][2]= -((a*X2*Xmag + (-1.0 + g)*v*Xmag2)/(
  2.0* a2*Xmag2));
    L[3][3]= (-1.0 + g)/(2.0*a2);

  }
  

  if (EIGENSET==13){
    L[0][0]=(v*X1 - u*X2)/a/Xmag;
    L[0][1]=X2/Xmag/a;
    L[0][2]=-(X1/Xmag/a);
    L[0][3]=0.0;
    L[1][0]=(2.0*a2 - (-1.0 + g)*q2)/(2.0*a2);
    L[1][1]=((-1.0 + g)*u)/a2;
    L[1][2]=((-1.0 + g)*v)/a2;
    L[1][3]=(1.0 - g)/a2;
    L[2][0]=(1.0/(4.0* a2*Xmag))*(-2.0* a* Vstar + (-1.0 + g)*q2*Xmag);
    L[2][1]=(u-g*u+a*X1/Xmag)/(2.0*a2);
    L[2][2]=(v-g*v+a*X2/Xmag)/(2.0*a2);
    L[2][3]=(-1.0 + g)/(a2);
    L[3][0]=(1.0/(4.0* a2* Xmag))*(2.0* a* Vstar + (-1.0 + g)*q2*Xmag);
    L[3][1]=(u-g*u-a*X1/Xmag)/(2.0*a2);
    L[3][2]=(v-g*v-a*X2/Xmag)/(2.0*a2);
    L[3][3]=(-1.0 + g)/(2.0*a2);

  }

#endif



#ifdef _3D
  u=jacvars.V[0];
  v=jacvars.V[1];
  w=jacvars.V[2];
  X1=metrics.X[0];
  X2=metrics.X[1];
  X3=metrics.X[2];
  q2=sqr(u)+sqr(v)+sqr(w);
  Xmag=sqrt(X1*X1 + X2*X2 + X3*X3);
  Xmag2=(X1*X1 + X2*X2 + X3*X3);
  Vstar=u*X1 + v*X2 + w*X3;

  if (EIGENSET==1){
L[0][0]=(-(-1.0 + g)*q2*q2*Xmag2 + 
   2.0*a2*(4.0*v*w*X2*X3 + 4.0*u*X1*(v*X2 + w*X3) + 
      u*u*(X1*X1 - X2*X2 - X3*X3) - w*w*(X1*X1 + X2*X2 - X3*X3) - 
      v*v*(X1*X1 - X2*X2 + X3*X3)))/notzero(4.0*a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[0][1]=(-2.0*a2*X1*(v*X2 + w*X3) + (-1.0 + g)*u*u*u*Xmag2 + 
   u*(2.0*a2*(X2*X2 + X3*X3) + (-1.0 + g)*v*v*Xmag2 + (-1.0 + 
         g)*w*w*Xmag2))/notzero(2.0*a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[0][2]=((-1.0 + g)*v*q2*Xmag2 + 
   2.0*a2*(-X2*(u*X1 + w*X3) + v*(X1*X1 + X3*X3)))/notzero(2.0*a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[0][3]=(2.0*a2*(w*(X1*X1 + X2*X2) - (u*X1 + v*X2)*X3) + (-1.0 + g)*w*q2*Xmag2)/notzero(2.0*a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[0][4]=-(((-1.0 + g)*q2)/notzero(2.0*a2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50));

L[1][0]=(w*w*w*X1*X1 - g*w*w*w*X1*X1 + w*w*w*X2*X2 - g*w*w*w*X2*X2 + 2.0*a2*u*X1*X3 + 
   2.0*a2*v*X2*X3 + 2.0*a2*w*X3*X3 + w*w*w*X3*X3 - 
   g*w*w*w*X3*X3 - (-1.0 + g)*u*u*w*Xmag2 - (-1.0 + 
      g)*v*v*w*Xmag2)/notzero(2.0*a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[1][1]=(-a2*X1*X3 + (-1.0 + g)*u*w*Xmag2)/notzero(a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[1][2]=(-a2*X2*X3 + (-1.0 + g)*v*w*Xmag2)/notzero(a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[1][3]=(a2*(X1*X1 + X2*X2) + (-1.0 + g)*w*w*Xmag2)/notzero(a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[1][4]=(w - g*w)/notzero(a2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[2][0]=(2.0*a2*u*X1*X2 + 
   2.0*a2*w*X2*X3 - (-1.0 + g)*u*u*v*Xmag2 - (-1.0 + 
      g)*v*v*v*Xmag2 + 
   v*(2.0*a2*X2*X2 - (-1 + g)*w*w*Xmag2))/notzero(2.0*a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[2][1]=(-a2*X1*X2 + (-1.0 + g)*u*v*Xmag2)/notzero(a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[2][2]=(a2*(X1*X1 + X3*X3) + (-1.0 + g)*v*v*Xmag2)/notzero(a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[2][3]=(-a2*X2*X3 + (-1.0 + g)*v*w*Xmag2)/notzero(a2*Xmag2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);

L[2][4]=(v - g*v)/notzero(a2*(u*u*X1 - (v*v + w*w)*X1 + 2.0*u*(v*X2 + w*X3)),1e-50);


L[3][0]=(2.0*a*Vstar + (-1.0 + g)*q2*Xmag)/(8.0*a2*(-1.0 + g)*Xmag*Xmag2);

L[3][1]=(-a*X1 - (-1.0 + g)*u*Xmag)/(4.0*a2*(-1.0 + g)*Xmag*Xmag2);

L[3][2]=(-a*X2 - (-1.0 + g)*v*Xmag)/(4.0*a2*(-1.0 + g)*Xmag*Xmag2);

L[3][3]=(-a*X3 - (-1.0 + g)*w*Xmag)/(4.0*a2*(-1.0 + g)*Xmag*Xmag2);

L[3][4]=1.0/(4.0*a2*Xmag2);


L[4][0]=(-2.0*a*Vstar + (-1.0 + g)*q2*Xmag)/(8.0*a2*(-1.0 + g)*Xmag2*Xmag);

L[4][1]=(a*X1 - (-1.0 + g)*u*Xmag)/(4.0*a2*(-1.0 + g)*Xmag*Xmag2);

L[4][2]=(a*X2 - (-1.0 + g)*v*Xmag)/(4.0*a2*(-1.0 + g)*Xmag*Xmag2);

L[4][3]=(a*X3 - (-1.0 + g)*w*Xmag)/(4.0*a2*(-1.0 + g)*Xmag*Xmag2);

L[4][4]=1.0/(4.0*a2*Xmag2);
  }



  if (EIGENSET==2){
    L[0][0]=(2.0*a*a - (-1.0 + g)*q2)/(2.0*a2);
    L[0][1]=((-1.0 + g)*u)/a2;
    L[0][2]=((-1.0 + g)*v)/a2;
    L[0][3]=((-1.0 + g)*w)/a2;
    L[0][4]=(1.0 - g)/a2;

    denom=(sqrt(2.0)*a*sqrt(X1*X1 + X2*X2 - X2*X3 + X3*X3 - X1*(X2 + X3)));
    assert(denom!=0.0);
    L[1][0]=(w*(X1 - X2) + u*(X2 - X3) + v*(-X1 + X3))/denom;
    L[1][1]=(-X2 + X3)/denom;
    L[1][2]=(X1 - X3) /denom;
    L[1][3]=(-X1 + X2)/denom;
    L[1][4]=0.0;
    
    denom=(sqrt(2.0)*a*(X1*X1*X1*X1 + X2*X2*X2*X2 - X2*X2*X2*X3 + 2.0*X2*X2*X3*X3 - X2*X3*X3*X3 + 
                 X3*X3*X3*X3 - X1*X1*X1*(X2 + X3) + X1*X1*(2.0*X2*X2 - X2*X3 + 2.0*X3*X3) - 
                 X1*(X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3)));
    assert(denom!=0.0);
    numer=sqrt(X1*X1 + X2*X2 - X2*X3 + X3*X3 - X1*(X2 + X3));
    L[2][0]=-(Xmag* numer 
                      * (w*(X1*X1 + X2*(X2 - X3) - X1*X3) + 
                         v*(X1*X1 - X1*X2 + X3*(-X2 + X3)) + 
                         u*(X2*X2 + X3*X3 - X1*(X2 + X3))))
               /denom;
    L[2][1]=(Xmag*(X2*X2 + X3*X3 - X1*(X2 + X3))*numer)/denom;
    L[2][2]=(Xmag*(X1*X1 - X1*X2 + X3*(-X2 + X3))*numer)/denom;
    L[2][3]=((X1*X1 + X2*(X2 - X3) - X1*X3)*Xmag*numer)/denom;
    L[2][4]=0.0e0;
   
    L[3][0]=(-2.0*a*u*X1*Xmag - 2.0*a*v*X2*Xmag + (-1 + g)*u*u*Xmag2 + (-1 + g)*v*v*Xmag2 + 
    w*(-2.0*a*X3*Xmag + (-1 + g)*w*Xmag2))/(4.0*a*a*Xmag2);
    L[3][1]=(a*X1*Xmag - (-1 + g)*u*Xmag2)/(2.0*a*a*Xmag2);
    L[3][2]=(a*X2*Xmag - (-1 + g)*v*Xmag2)/(2.0*a*a*Xmag2);
    L[3][3]=(a*X3*Xmag - (-1 + g)*w*Xmag2)/(2.0*a*a*Xmag2);
    L[3][4]=(-1 + g)/(2.0*a*a);

    L[4][0]=(2.0*a*u*X1*Xmag + 2.0*a*v*X2*Xmag + (-1 + g)*u*u*Xmag2 + (-1 + g)*v*v*Xmag2 + 
    w*(2.0*a*X3*Xmag + (-1 + g)*w*Xmag2))/(4.0*a*a*Xmag2);
    L[4][1]=-((a*X1*Xmag + (-1 + g)*u*Xmag2)/(2.0*a*a*Xmag2));
    L[4][2]=-((a*X2*Xmag + (-1 + g)*v*Xmag2)/(2.0*a*a*Xmag2));
    L[4][3]=-((a*X3*Xmag + (-1 + g)*w*Xmag2)/(2.0*a*a*Xmag2));
    L[4][4]=(-1 + g)/(2.0*a*a);

  }

  

  if (EIGENSET==3) {
    L[0][0]=(2.0*a2*(X1*X1*X1 + X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + 
        X1*(X2*X2 + X3*X3)) - (-1.0 + g)*q2*(X1*X1*X1 + X2*X2*X2 + 
        X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + 
        X1*(X2*X2 + X3*X3)) + 2.0*a*Xmag*(w*(X1*X1 + X2*(X2 - X3) + 2.0*X1*X3) + 
        v*(X1*X1 + 2.0*X1*X2 + X3*(-X2 + X3)) - u*(X1*(X2 + X3) + 2.0*(X2*X2 + X3*X3))))
        /(6.0*a2*(X1 + X2 + X3)*Xmag2);
    L[0][1]= (a*Xmag* (X1*(X2 + X3) + 2.0*(X2*X2 + X3*X3)) + (-1.0 + g)*u*(X1*X1*X1 + 
        X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + 
        X1*(X2*X2 + X3*X3)))/(3.0* a2* (X1 + X2 + X3)*Xmag2);
    L[0][2]= (-a*Xmag*(X1*X1 + 2.0*X1*X2 + X3*(-X2 + X3)) + (-1.0 + g)*v*(X1*X1*X1 + 
        X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + 
        X1*(X2*X2 + X3*X3)))/(3.0*a2* (X1 + X2 + X3)*Xmag2);
    L[0][3]= (-a*(X1*X1 + X2*(X2 - X3) + 2.0* X1* X3)*Xmag + (-1 + g)*w*(X1*X1*X1 + 
        X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + 
        X1*X1*(X2 + X3) + X1*(X2*X2 + X3*X3)))/(3.0*a2*(X1 + X2 + X3)*Xmag2);
    L[0][4]=-((-1.0 + g)/(3.0* a2));

    L[1][0]=-(-2.0* a2*(X1*X1*X1 + X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + 
         X1*X1*(X2 + X3) + X1*(X2*X2 + X3*X3)) + (-1.0 + g)*q2*(X1*X1*X1 + X2*X2*X2 
         + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + X1*(X2*X2 + X3*X3)) + 
         2.0*a*Xmag*(-w*(X1*X1 + X2*X2 - X1*X3 + 2.0*X2*X3) - 
         u*(2.0*X1*X2 + X2*X2 - X1*X3 + X3*X3) + 
         v*(2.0*X1*X1 + X1*X2 + X3*(X2 + 2.0*X3))))/(6.0*a2*(X1 + X2 + X3)*Xmag2);
    L[1][1]= (-a*Xmag*(2.0*X1*X2 + X2*X2 - X1*X3 + X3*X3) + (-1.0 + g)*u*(X1*X1*X1 + 
        X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + 
        X1*(X2*X2 + X3*X3)))/(3.0*a2*(X1 + X2 + X3)*Xmag2);
    L[1][2]= (a*Xmag*(2.0*X1*X1 + X1*X2 + X3*(X2 + 2.0*X3)) + (-1.0 + g)*v*(X1*X1*X1 + 
        X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + 
        X1*(X2*X2 + X3*X3)))/(3.0*a2*(X1 + X2 + X3)*Xmag2);
    L[1][3]= (-a*Xmag*(X1*X1 - X1*X3 + X2*(X2 + 2.0*X3)) + (-1.0 + g)*w*(X1*X1*X1 + 
        X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + 
        X1*(X2*X2 + X3*X3)))/(3.0*a2*(X1 + X2 + X3)*Xmag2);
    L[1][4]=-((-1.0 + g)/(3.0*a2));
   
    L[2][0]=(2.0*a2*(X1*X1*X1 + X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + 
        X1*X1*(X2 + X3) + X1*(X2*X2 + X3*X3)) - (-1.0 + g)*q2*(X1*X1*X1 + X2*X2*X2 
        + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + X1*(X2*X2 + X3*X3)) + 
        2.0*a*Xmag*(u*(-X1*X2 + X2*X2 + 2.0*X1*X3 + X3*X3) - 
        w*(2.0*X1*X1 + X1*X3 + X2*(2.0*X2 + X3)) + v*(X1*X1 - X1*X2 + X3*(2.0*X2 + X3))))
        /(6.0*a2*(X1 + X2 + X3)*Xmag2);
    L[2][1]=(a*(X1*X2 - X2*X2 - 2.0*X1*X3 - X3*X3)*Xmag + (-1.0 + g)*u
        *(X1*X1*X1 + X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + 
        X1*X1*(X2 + X3) + X1*(X2*X2 + X3*X3)))/(3.0* a2*(X1 + X2 + X3)*Xmag2);
    L[2][2]=(-a*Xmag*(X1*X1 - X1*X2 + X3*(2.0*X2 + X3)) + (-1.0 + g)*v
        *(X1*X1*X1 + X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + 
        X1*(X2*X2 + X3*X3)))/(3.0*a2*(X1 + X2 + X3)*Xmag2);
    L[2][3]=(a*Xmag*(2.0*X1*X1 + X1*X3 + X2*(2.0*X2 + X3)) + (-1.0 + g)*w*(X1*X1*X1 + 
        X2*X2*X2 + X2*X2*X3 + X2*X3*X3 + X3*X3*X3 + X1*X1*(X2 + X3) + 
        X1*(X2*X2 + X3*X3)))/(3.0*a2*(X1 + X2 + X3)*Xmag2);
    L[2][4]=-((-1.0 + g)/(3.0*a2));
  
    L[3][0]=(-2.0*a*u*X1*Xmag - 2.0*a*v*X2*Xmag + (-1.0 + g)*u*u*Xmag2 + (-1.0 + 
        g)*v*v*Xmag2 + w*(-2.0*a*X3*Xmag + (-1.0 + g)*w*Xmag2))/(4.0*a2*Xmag2);
    L[3][1]=(a*X1*Xmag - (-1.0 + g)*u*Xmag2)/(2.0* a2*Xmag2);
    L[3][2]=(a*X2*Xmag - (-1.0 + g)*v*Xmag2)/(2.0* a2*Xmag2);
    L[3][3]=(a*X3*Xmag - (-1.0 + g)*w*Xmag2)/(2.0* a2*Xmag2);
    L[3][4]=(-1.0 + g)/(2.0*a2);

    L[4][0]=(2.0*a*u*X1*Xmag + 2.0*a*v*X2*Xmag + (-1.0 + g)*u*u*Xmag2 + 
        (-1.0 + g)*v*v*Xmag2 + w*(2.0*a*X3*Xmag + (-1.0 + g)*w*Xmag2))/(4.0*a2*Xmag2);
    L[4][1]= -((a*X1*Xmag + (-1.0 + g)*u*Xmag2)/(2.0*a2*Xmag2));
    L[4][2]= -((a*X2*Xmag + (-1.0 + g)*v*Xmag2)/(2.0*a2*Xmag2));
    L[4][3]= -((a*X3*Xmag + (-1.0 + g)*w*Xmag2)/(2.0*a2*Xmag2));
    L[4][4]=(-1.0 + g)/(2.0*a2);


  }
#endif
}







void find_jacvars_at_interface_Roe_average(jacvars_t jacvarsL, jacvars_t jacvarsR, gl_t *gl, long theta, jacvars_t *jacvars){
  long dim;
  double sqrtrhoR,sqrtrhoL,HL,HR,H,q2,kL,kR,rho;

  assert(jacvarsR.rho>=0.0);
  assert(jacvarsL.rho>=0.0);
  sqrtrhoR=sqrt(jacvarsR.rho);
  sqrtrhoL=sqrt(jacvarsL.rho);
  kL=sqrtrhoL/(sqrtrhoL+sqrtrhoR);
  kR=sqrtrhoR/(sqrtrhoL+sqrtrhoR);
    
  rho=sqrt(jacvarsL.rho*jacvarsR.rho);

  jacvars->rho=rho;

  q2=0.0e0;
  for (dim=0; dim<nd; dim++){
    jacvars->V[dim]=kR*jacvarsR.V[dim]+kL*jacvarsL.V[dim];
    q2=q2+sqr(jacvars->V[dim]);
  }
  HL=jacvarsL.a2/(gl->model.fluid.gamma-1.0);
  HR=jacvarsR.a2/(gl->model.fluid.gamma-1.0);
  for (dim=0; dim<nd; dim++){
    HL+=0.5*sqr(jacvarsL.V[dim]);
    HR+=0.5*sqr(jacvarsR.V[dim]);
  }
  H=kR*HR+kL*HL;

  jacvars->a2=(gl->model.fluid.gamma-1.0)*(H-q2*0.5);
  jacvars->eta=0.5*(jacvarsL.eta+jacvarsR.eta);
  jacvars->gamma=gl->model.fluid.gamma;

  set_jacvars_eigenconditioning_constants(gl,jacvars);

}




void find_jacvars_at_interface_arith_average(jacvars_t jacvarsL, jacvars_t jacvarsR, gl_t *gl, long theta, jacvars_t *jacvars){
  long dim;
  double kL,kR;

  kL=0.5;
  kR=0.5;
  jacvars->rho=kL*jacvarsL.rho+kR*jacvarsR.rho;

  for (dim=0; dim<nd; dim++){
    jacvars->V[dim]=kR*jacvarsR.V[dim]+kL*jacvarsL.V[dim];
  }

  jacvars->a2=kL*jacvarsL.a2+kR*jacvarsR.a2;

  jacvars->eta=0.5*(jacvarsL.eta+jacvarsR.eta);
  jacvars->gamma=gl->model.fluid.gamma;

  set_jacvars_eigenconditioning_constants(gl,jacvars);
}





void find_jacvars(np_t np, gl_t *gl, metrics_t metrics, long theta, jacvars_t *jacvars){
  long dim;
  double rho;

  assert_np(np,is_node_resumed(np));
    
  rho=_rho(np);
  jacvars->rho=rho;
  for (dim=0; dim<nd; dim++){
    jacvars->V[dim]=_V(np,dim);
  }
  jacvars->a2=gl->model.fluid.gamma*gl->model.fluid.R*_T(np,gl);
  jacvars->eta=gl->model.fluid.eta;
  jacvars->gamma=gl->model.fluid.gamma;

  set_jacvars_eigenconditioning_constants(gl,jacvars);
}


void find_musclvars(np_t np, gl_t *gl, flux_t musclvars){
  long dim;
  musclvars[0]=_rho(np);
  for (dim=0; dim<nd; dim++) musclvars[1+dim]=_V(np,dim);
  switch (MUSCLVARS){
    case MUSCLVARS1:
      musclvars[1+nd]=_T(np,gl);
    break;
    case MUSCLVARS2:
      musclvars[1+nd]=_P(np,gl);
    break;
  }
}


bool is_musclvar_a_charged_mass_fraction(long flux){
  return(FALSE);
}


bool is_musclvar_a_mass_fraction(long flux){
  return(FALSE);
}


void find_Ustar_from_musclvars(flux_t musclvars, metrics_t metrics, gl_t *gl, flux_t Ustar){
  double rho,q2,T,P;
  long dim;
  dim_t V;
  bool REFORMAT;

  REFORMAT=FALSE;
  rho=musclvars[0];
  reformat_rho(gl, &rho, "_muscl", &REFORMAT);

  switch (MUSCLVARS){
    case MUSCLVARS1:
      T=musclvars[1+nd];
      reformat_T(gl, &T, "_muscl", &REFORMAT);
    break;
    case MUSCLVARS2:
      P=musclvars[1+nd];
      reformat_P(gl, &P, "_muscl", &REFORMAT);
      T=P/rho/gl->model.fluid.R;
    break;
  }

  Ustar[0]=musclvars[0]*metrics.Omega;
  for (dim=0; dim<nd; dim++) V[dim]=musclvars[1+dim];

  q2=0.0e0;
  for (dim=0; dim<nd; dim++){
    Ustar[1+dim]=metrics.Omega*rho*V[dim];
    q2=q2+sqr(V[dim]);
  }

  Ustar[1+nd]=metrics.Omega*rho*(0.5e0*q2+gl->model.fluid.R*T/(gl->model.fluid.gamma-1.0));
}




void find_jacvars_from_musclvars(flux_t musclvars, metrics_t metrics, gl_t *gl, long theta, jacvars_t *jacvars){
  long dim;
  bool REFORMAT;
  double P,T,rho;
 
  REFORMAT=FALSE;
  rho=musclvars[0];
  reformat_rho(gl, &rho, "_muscl", &REFORMAT);

  switch (MUSCLVARS){
    case MUSCLVARS1:
      T=musclvars[1+nd];
      reformat_T(gl, &T, "_muscl", &REFORMAT);
    break;
    case MUSCLVARS2:
      P=musclvars[1+nd];
      reformat_P(gl, &P, "_muscl", &REFORMAT);
      T=P/rho/gl->model.fluid.R;
    break;
  }
    
  jacvars->rho=rho;
  for (dim=0; dim<nd; dim++){
    jacvars->V[dim]=musclvars[1+dim];
  }
  jacvars->a2=gl->model.fluid.gamma*gl->model.fluid.R*T;
  jacvars->eta=gl->model.fluid.eta;
  jacvars->gamma=gl->model.fluid.gamma;

  set_jacvars_eigenconditioning_constants(gl,jacvars);
}


void find_musclvars_from_jacvars(jacvars_t jacvars, gl_t *gl, flux_t musclvars){
  long dim;
  bool REFORMAT;
    
  REFORMAT=FALSE;
  musclvars[0]=jacvars.rho;
  reformat_rho(gl, &(musclvars[0]), "_muscl", &REFORMAT);
  for (dim=0; dim<nd; dim++){
    musclvars[1+dim]=jacvars.V[dim];
  }

  switch (MUSCLVARS){
    case MUSCLVARS1:
      musclvars[1+nd]=jacvars.a2/(gl->model.fluid.gamma*gl->model.fluid.R);
      reformat_T(gl, &(musclvars[1+nd]), "_muscl", &REFORMAT);
    break;
    case MUSCLVARS2:
      musclvars[1+nd]=musclvars[0]*jacvars.a2/(gl->model.fluid.gamma);
      reformat_P(gl, &(musclvars[1+nd]), "_muscl", &REFORMAT);
    break;
  }
}


void find_jacvars_from_U(flux_t U, metrics_t metrics, gl_t *gl, long theta, jacvars_t *jacvars){
  long dim;
  double rho,ekin,eint,T;

  rho=U[0];  
  jacvars->rho=rho;
  for (dim=0; dim<nd; dim++){
    jacvars->V[dim]=U[1+dim]/rho;
  }
  ekin=0.0e0;
  for (dim=0; dim<nd; dim++){
    ekin=ekin+0.5e0*sqr(U[1+dim]/rho);
  }
  eint=U[1+nd]/rho-ekin; 
  T=eint/gl->model.fluid.R*(gl->model.fluid.gamma-1.0);

  jacvars->a2=gl->model.fluid.gamma*gl->model.fluid.R*T;
  jacvars->eta=gl->model.fluid.eta;
  jacvars->gamma=gl->model.fluid.gamma;

  set_jacvars_eigenconditioning_constants(gl,jacvars);
}




void find_Ustar_given_metrics(np_t np, gl_t *gl, metrics_t metrics, flux_t Ustar){
  long flux;
  for (flux=0; flux<nf; flux++)
    Ustar[flux]=np.bs->U[flux]*metrics.Omega;
}





double _Pe_from_jacvars(jacvars_t jacvars, metrics_t metrics){
  double Pe,Vstar,Xmag2;
  long dim;
  Vstar=0.0;
  Xmag2=0.0;
  for (dim=0; dim<nd; dim++){
    Vstar+=jacvars.V[dim]*metrics.X[dim];
    Xmag2+=sqr(metrics.X[dim]);
  }
  Pe=jacvars.rho/max(1.0e-20,jacvars.eta)*fabs(Vstar)/Xmag2;
  return(Pe);
}



// the Uprime vector is composed of rho, V[1..nd], e
void find_Uprime(np_t np, gl_t *gl, flux_t Uprime){
  long flux,dim;
  double rho,q2;

  rho=np.bs->U[0];

  // set all flux components to those of U
  for (flux=0; flux<nf; flux++) Uprime[flux]=np.bs->U[flux];

  /* rearrange the momentum flux components */
  for (dim=0; dim<nd; dim++) Uprime[1+dim]=np.bs->U[1+dim]/rho;

  /* rearrange the total energy flux component */
  q2=0.0e0;
  for (dim=0; dim<nd; dim++){
    q2+=sqr(np.bs->U[1+dim]/rho);
  }
//  T=(np[l].bs->U[1+nd]/rho-0.5e0*q2)*(gl->model.fluid.gamma-1.0)/gl->model.fluid.R;
  Uprime[1+nd]=(np.bs->U[1+nd]-0.5e0*rho*q2)/rho;
}


// the Uprime vector is composed of rho, V[1..nd], e
void find_Uprime_from_U(flux_t U, flux_t Uprime){
  long flux,dim;
  double rho,q2;

  rho=U[0];

  for (flux=0; flux<nf; flux++) Uprime[flux]=U[flux];

  for (dim=0; dim<nd; dim++) Uprime[1+dim]=U[1+dim]/rho;

  q2=0.0e0;
  for (dim=0; dim<nd; dim++){
    q2+=sqr(U[1+dim]/rho);
  }
  Uprime[1+nd]=(U[1+nd]-0.5e0*rho*q2)/rho;
}


void find_Uprime_from_jacvars(jacvars_t jacvars, flux_t Uprime){
  long dim;
  Uprime[0]=jacvars.rho;
  for (dim=0; dim<nd; dim++) Uprime[1+dim]=jacvars.V[dim];
  Uprime[1+nd]=jacvars.a2/(jacvars.gamma*(jacvars.gamma-1.0));
}


void find_dUstar_dUprime_from_jacvars(jacvars_t jacvars, metrics_t metrics, sqmat_t dUdUprime){
  long dim,row,col;
  dim_t V;
  double rho,e;

  for (row=0; row<nf; row++){
    for (col=0; col<nf; col++){
      dUdUprime[row][col]=0.0e0;
    }
    dUdUprime[row][row]=1.0e0*metrics.Omega;
  }

  for (dim=0; dim<nd; dim++){
    V[dim]=jacvars.V[dim];
  }
  rho=jacvars.rho;
  e=jacvars.a2/(jacvars.gamma*(jacvars.gamma-1.0));
  // make adjustments to momentum rows
  for (dim=0; dim<nd; dim++){
    dUdUprime[1+dim][1+dim]=rho*metrics.Omega;
    dUdUprime[1+dim][0]=V[dim]*metrics.Omega;
  }


  // now make adjustments for the total energy row
  // recall that rho*et=rho*e + 0.5*rho*q^2
  //                   =rho*e + 0.5*rho*u*u + 0.5*rho*v*v
  //                   =(rho*e) + 0.5*rho*u^2 + 0.5*rho*v^2   
  dUdUprime[nd+1][nd+1]=rho*metrics.Omega;
  dUdUprime[nd+1][0]=e*metrics.Omega;
  for (dim=0; dim<nd; dim++) {
    dUdUprime[nd+1][1+dim]=rho*V[dim]*metrics.Omega;
    dUdUprime[nd+1][0]+=0.5*sqr(V[dim])*metrics.Omega;
  }
}


/* phiL and phiR must be determinative properties (positive)  */
/* static double _f_Parent_average(double phiL, double phiR, double Mtheta){
  double phi;
  // make sure phiL and phiR are positive
  // blend of geometric average and min functions: works best so far
  phiL=max(1.0e-99, phiL);
  phiR=max(1.0e-99, phiR);
  if (Mtheta<1.0) {
    phi=Mtheta*min(phiL,phiR)+(1.0-Mtheta)*2.0/(1.0/phiL+1.0/phiR);
  } else {
    phi=min(phiL,phiR);
  }
  return(phi);
} */



/* phiL and phiR must be determinative properties (positive)  */
static double _f_Parent_average(double phiL, double phiR, double Mtheta){
  double phi;
  phiL=max(1.0e-99, phiL);
  phiR=max(1.0e-99, phiR);
  phi=min(phiL,phiR);
  return(phi);
}

/*
static double maxmag(double arg1, double arg2){
  double ret;
  if (fabs(arg1)>fabs(arg2)) ret=arg1; else ret=arg2;
  return(ret);
}*/


void find_jacvars_at_interface_Parent_average_test(jacvars_t jacvarsL, jacvars_t jacvarsR, gl_t *gl, long theta, metrics_t metrics, jacvars_t *jacvars){
  long dim;
  double Vstar,a2;


  Vstar=0.0;
  for (dim=0; dim<nd; dim++){
    jacvars->V[dim]=0.5*jacvarsR.V[dim]+0.5*jacvarsL.V[dim];
    //jacvars->V[dim]=maxmag(jacvarsR.V[dim],jacvarsL.V[dim]);
    Vstar+=jacvars->V[dim]*metrics.X[dim];
  }
  a2=min(jacvarsL.a2,jacvarsR.a2);

  jacvars->a2=a2;
  jacvars->rho=min(jacvarsL.rho,jacvarsR.rho);


  jacvars->eta=0.5*(jacvarsL.eta+jacvarsR.eta);

  jacvars->gamma=gl->model.fluid.gamma;

  set_jacvars_eigenconditioning_constants(gl,jacvars);
}


void find_jacvars_at_interface_Parent_average(jacvars_t jacvarsL, jacvars_t jacvarsR, gl_t *gl, long theta, metrics_t metrics, jacvars_t *jacvars){
  long dim;
  double Vtheta2,Vstar,Xmag2,a2,Mtheta;


  Vstar=0.0;
  Xmag2=0.0;
  for (dim=0; dim<nd; dim++){
    //jacvars->V[dim]=0.5*jacvarsR.V[dim]+0.5*jacvarsL.V[dim];
    jacvars->V[dim]=minmod(jacvarsR.V[dim],jacvarsL.V[dim]);

    Vstar+=jacvars->V[dim]*metrics.X[dim];
    Xmag2+=sqr(metrics.X[dim]);
  }
  Vtheta2=sqr(Vstar)/Xmag2;
  a2=0.5*jacvarsL.a2+0.5*jacvarsR.a2;

  Mtheta=sqrt(Vtheta2/a2);
  jacvars->a2=_f_Parent_average(jacvarsL.a2,jacvarsR.a2,Mtheta);
  jacvars->rho=_f_Parent_average(jacvarsL.rho,jacvarsR.rho,Mtheta);

  jacvars->eta=0.5*(jacvarsL.eta+jacvarsR.eta);
  jacvars->gamma=gl->model.fluid.gamma;

  set_jacvars_eigenconditioning_constants(gl,jacvars);
}
