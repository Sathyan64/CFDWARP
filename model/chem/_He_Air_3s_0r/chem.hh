#define _CHEM_HEAIR3S0R
#define _CHEM_METHOD "Helium Air 3 species 0 reaction"

#define ns 3
#define ncs 0
#define CHEM_NEUTRAL TRUE

#define specO2 0
#define specN2 1

/*
Species ordering:
1. Electrons
2. Negative ions
3. Positive ions
4. Neutrals

If there are is no electron species, give speceminus a rank of -1
*/

const static long smap[ns] = {
  SMAP_O2,
  SMAP_N2,
  SMAP_He
};


#define SPECIES_ELECTRON 0
#define SPECIES_NEUTRAL  1
#define SPECIES_IONPLUS  2
#define SPECIES_IONMINUS 3


const static long speciestype[ns] = {
  SPECIES_NEUTRAL,
  SPECIES_NEUTRAL,
  SPECIES_NEUTRAL
};
