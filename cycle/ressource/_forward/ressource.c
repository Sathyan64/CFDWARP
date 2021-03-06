#include <cycle/ressource/_ressource.h>
#include <cycle/share/cycle_share.h>




void add_Sstar_residual(long theta, long ls, long le, np_t *np, gl_t *gl, double fact, double fact_trapezoidal){
  long l;
  long flux;
  flux_t S;

  if (theta==0) {
    for (l=ls; l!=_l_plus_one(le,gl,theta); l=_l_plus_one(l,gl,theta)){
      find_Sstar(np,gl,l,S);
      for (flux=0; flux<nf; flux++) np[l].wk->Res[flux]-=fact*S[flux];
#ifdef _RESTIME_STORAGE_TRAPEZOIDAL
      for (flux=0; flux<nf; flux++) np[l].bs->Res_trapezoidal[flux]-=fact_trapezoidal*S[flux];
#endif
    }
  }
}


